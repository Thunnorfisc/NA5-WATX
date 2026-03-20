/* Start Header
***********************************************************************/

/*! \file   server.hpp
    \author Loh Boon Cheong, Timothy
    \par    email: loh.b@digipen.edu
    \date   20th March, 2026
    \brief  Copyright (C) 2026 DigiPen Institute of Technology

    Reproduction or diclosure of this file or its contents without the prior
    written consent of DigiPen Institute of Technology is prohibited. */

    /* End Header
    ***********************************************************************/
#define _WINSOCK_DEPRECATED_NO_WARNINGS

#include "server.hpp"

#include <mutex>
#include <cmath>
#include <deque>
#include <chrono>
#include <string>
#include <format>
#include <vector>
#include <ostream>
#include <cstring>
#include <cassert>
#include <cstdint>
#include <utility>
#include <fstream>
#include <iostream>
#include <algorithm>
#include <filesystem>
#include <string_view>
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#include <Windows.h>
#include <winsock2.h>
#include <WS2tcpip.h>
#include <iphlpapi.h>

#pragma comment(lib, "Ws2_32.lib")

namespace
{
    std::mutex s_ostreamMutex;
    void threadSafeOStream(std::ostream& os, std::string_view msg)
    {
        std::lock_guard lock(s_ostreamMutex);
        os << msg << '\n';
    }
    std::string wsaErrorStr()
    {
        int err = WSAGetLastError();
        char* msg = nullptr;

        FormatMessageA(
            FORMAT_MESSAGE_ALLOCATE_BUFFER |
            FORMAT_MESSAGE_FROM_SYSTEM |
            FORMAT_MESSAGE_IGNORE_INSERTS,
            nullptr,
            err,
            0,
            (LPSTR)&msg,
            0,
            nullptr
        );

        std::string result = msg ? msg : "Unknown error";
        LocalFree(msg);
        return result;
    }
    bool isRecoverableWSAError(int err)
    {
        switch (err)
        {
            // --- Non-fatal / expected conditions ---
        case WSAEWOULDBLOCK:     // no data available (non-blocking socket)
        case WSAEINTR:           // interrupted call
        case WSAETIMEDOUT:       // timeout (common for UDP)
        case WSAECONNRESET:      // UDP: ICMP port unreachable
        case WSAENETRESET:       // connection dropped temporarily
        case WSAENOBUFS:         // buffer pressure, can retry
        case WSAEINPROGRESS:     // async still in progress
        case WSAEALREADY:        // operation already ongoing
            return true;

            // --- Everything else: treat as fatal ---
        default:
            return false;
        }
    }
}

Server::Server()
{
    constexpr int winsockMajorVersion = 2;
    constexpr int winsockMinorVersion = 2;
    WSADATA wsaData;
    int iResult{};
    iResult = WSAStartup(
        MAKEWORD(winsockMajorVersion,
            winsockMinorVersion)
        , &wsaData);

    if (iResult != 0)
    {
        std::string err = std::format("WSAStartup failed: {}", iResult);
        throw std::runtime_error(err);
    }

    _socket = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (_socket == INVALID_SOCKET) 
        throw std::runtime_error(std::format("socket() failed, unable to set up udp socket: {}", wsaErrorStr()));

    sockaddr_in addr{};
    addr.sin_family = AF_INET; // ipv4
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(0);

    if (bind(_socket, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) == SOCKET_ERROR) 
        throw std::runtime_error(std::format("bind() failed, unable to set up udp socket: {}", wsaErrorStr()));

    // get port
    sockaddr_in boundAddr{};
    int boundAddrLen = sizeof(boundAddr);
    if (getsockname(_socket, reinterpret_cast<sockaddr*>(&boundAddr), &boundAddrLen) == SOCKET_ERROR)
        throw std::runtime_error(std::format("getsockname() failed, unable to setup udp socket: {}", wsaErrorStr()));

    _portHostOrder = ntohs(boundAddr.sin_port);

    // get machine host name
    char hostName[256]{};
    if (gethostname(hostName, sizeof(hostName)) == SOCKET_ERROR)
        throw std::runtime_error(std::format("gethostname() failed, unable to setup udp socket: {}", wsaErrorStr()));

    addrinfo hints{};
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_DGRAM;
    hints.ai_protocol = IPPROTO_UDP;

    addrinfo* result = nullptr;
    if (getaddrinfo(hostName, nullptr, &hints, &result) != 0)
        throw std::runtime_error(std::format("getaddrinfo() failed, unable to setup udp socket"));

    char ipStr[INET_ADDRSTRLEN]{};
    auto* ipv4 = reinterpret_cast<sockaddr_in*>(result->ai_addr);
    inet_ntop(AF_INET, &(ipv4->sin_addr), ipStr, sizeof(ipStr));
    _ip = ipStr;
    freeaddrinfo(result);

    std::cout << "Listening on " << _ip << ":" << _portHostOrder << '\n';
}
Server::~Server() 
{
    _stopSource.request_stop();
    if (_thread.joinable()) _thread.join();

    if (_socket != INVALID_SOCKET) closesocket(_socket);
    WSACleanup();
}

void Server::startListening()
{
    if (_thread.joinable()) throw std::runtime_error("startAcceptingClients() failed, thread is already assigned");
    _thread = std::thread([this]()
        {
            actualStartListening(_stopSource.get_token());
        });
}

bool Server::isListeningThreadFinished() noexcept
{
    return _threadFinished;
}

void Server::handle_reqRegister(std::span<const char> udpPacketWithoutMID, sockaddr_in* sa)
{
    char ipStr[INET_ADDRSTRLEN]{};
    inet_ntop(AF_INET, &sa->sin_addr, ipStr, INET_ADDRSTRLEN);
    auto port = ntohs(sa->sin_port);
    std::string ipStrAndPort = std::format("{}:{}", ipStr, port);
    if (!udpPacketWithoutMID.empty())
    {
        threadSafeOStream(std::cerr,
            std::format("Client {}: REQ_REGISTER received {} bytes but expected 0 bytes in its payload",
                ipStrAndPort, udpPacketWithoutMID.size()));
        return;
    }
    auto sessionIdHostOrder = getNextSessionIdHostOrder();
    auto sessionIdNetworkOrder = htonl(sessionIdHostOrder);
    std::vector<char> sendPacket;
    sendPacket.resize(5);
    sendPacket[0] = static_cast<char>(MessageType::RSP_REGISTER);
    std::memcpy(sendPacket.data() + 1, &sessionIdNetworkOrder, sizeof(sessionIdNetworkOrder));
    bool success = false;
    for (int i = 0; i < _maxRetry; i++)
    {
        int sentBytes = sendto(_socket, sendPacket.data(), static_cast<int>(sendPacket.size()), 0,
            reinterpret_cast<sockaddr*>(sa), sizeof(*sa));
        if (sentBytes == SOCKET_ERROR)
        {
            if (isRecoverableWSAError(WSAGetLastError())) continue;
            else
            {
                success = false; // not recoverable
                break;
            }
        }
        else
        {
            // not socket error, so ok alrd
            success = true;
            break;
        }
    }
    if (success)
    {
        auto [_ignore,succeed] = _sessionIdToIpPort.emplace(std::make_pair(sessionIdHostOrder, ipStrAndPort));
        assert(succeed && "Session id registration has logic error");
        threadSafeOStream(std::cout, std::format("Client: {} connected", ipStrAndPort));
    }
    else
    {
        threadSafeOStream(std::cerr, std::format("Client {}: Unable to register",ipStrAndPort));
    }
}

void Server::handle_reqUnregister(std::span<const char> udpPacketWithoutMID, sockaddr_in* sa)
{
    const std::size_t expectedBytes = 4;
    char ipStr[INET_ADDRSTRLEN]{};
    inet_ntop(AF_INET, &sa->sin_addr, ipStr, INET_ADDRSTRLEN);
    auto port = ntohs(sa->sin_port);
    std::string ipStrAndPort = std::format("{}:{}", ipStr, port);
    if (udpPacketWithoutMID.size() != expectedBytes)
    {
        threadSafeOStream(std::cerr,
            std::format("Client {}: REQ_UNREGISTER received {} bytes but expected {} bytes in its payload",
                ipStrAndPort,udpPacketWithoutMID.size(), expectedBytes));
        return;
    }
    SessionId sessionIdNetworkOrder;
    std::memcpy(&sessionIdNetworkOrder, udpPacketWithoutMID.data(), sizeof(sessionIdNetworkOrder));
    SessionId sessionIdHostOrder = ntohl(sessionIdNetworkOrder);

    if (!destroySessionIdHostOrder(sessionIdHostOrder, ipStrAndPort))
    {
        threadSafeOStream(std::cerr,
            std::format("Client {}: Request to destroy session id {} but not found on server side",
                ipStrAndPort,sessionIdHostOrder));
        return;
    }
    threadSafeOStream(std::cout, std::format("Client: {} disconnected", ipStrAndPort));
    return;
}

void Server::actualStartListening(std::stop_token st) noexcept
{
    std::vector<char> udpPacket;
    udpPacket.resize(_maxUdpSizeBytes);
    while (!st.stop_requested())
    {
        fd_set readSet;
        FD_ZERO(&readSet);
        FD_SET(_socket, &readSet);

        timeval timeout{};
        timeout.tv_sec = 0;
        timeout.tv_usec = 100000; // 100 ms
        int ready = select(
            0,
            &readSet,
            nullptr,
            nullptr,
            &timeout
        );

        if (ready == SOCKET_ERROR)
        {
            threadSafeOStream(std::cerr,std::format("select() failed: {}", wsaErrorStr()));
            goto end;
        }
        else if (ready == 0) continue; // timeout, no data, check stop token again

        if (FD_ISSET(_socket, &readSet))
        {
            sockaddr_in from{};
            int fromLen = sizeof(from);

            int bytesReceived = recvfrom(
                _socket,
                udpPacket.data(),
                static_cast<int>(udpPacket.size()),
                0,
                reinterpret_cast<sockaddr*>(&from),
                &fromLen
            );
            if (bytesReceived == SOCKET_ERROR)
            {
                threadSafeOStream(std::cerr, std::format("recvfrom() failed: {}", wsaErrorStr()));
                goto end;
            }
            assert(bytesReceived != 0 && "Bytes received should not be zero");
            assert(!udpPacket.empty() && "Udp packet shouldn't be empty here");
            MessageType message = static_cast<MessageType>(udpPacket[0]);
            auto it = _messageTypeFns.find(message);
            if (it != _messageTypeFns.end())
                (this->*it->second)(std::span<const char>(udpPacket).subspan(1,bytesReceived - 1), &from);
        }
    }
    end:
    _threadFinished = true;
}

Server::SessionId Server::getNextSessionIdHostOrder()
{
    assert(_nextSessionIdHostOrder > 0 && "Ran out of session ids!");
    return _nextSessionIdHostOrder++;
}

bool Server::destroySessionIdHostOrder(SessionId id, std::string ipPort)
{
    auto it3 = _sessionIdToIpPort.find(id);
    if (it3 == _sessionIdToIpPort.end())
    {
        threadSafeOStream(std::cerr,
            std::format("Client {}: Requested to destroy a session id that is not active!!!!!",ipPort));
        return false;
    }
    if (it3->second != ipPort)
    {
        threadSafeOStream(std::cerr,
            std::format("Client {}: Requested to destroy a session id that is not theirs.",ipPort));
        return false;
    }
    _sessionIdToIpPort.erase(it3);
    return true;
}