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
        std::string err = std::format("[Server] WSAStartup failed: {}", iResult);
        throw std::runtime_error(err);
    }

    _socket = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (_socket == INVALID_SOCKET) 
        throw std::runtime_error(std::format("[Server] socket() failed, unable to set up udp socket: {}", wsaErrorStr()));

    sockaddr_in addr{};
    addr.sin_family = AF_INET; // ipv4
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(0);

    if (bind(_socket, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) == SOCKET_ERROR) 
        throw std::runtime_error(std::format("[Server] bind() failed, unable to set up udp socket: {}", wsaErrorStr()));

    // get port
    sockaddr_in boundAddr{};
    int boundAddrLen = sizeof(boundAddr);
    if (getsockname(_socket, reinterpret_cast<sockaddr*>(&boundAddr), &boundAddrLen) == SOCKET_ERROR)
        throw std::runtime_error(std::format("[Server] getsockname() failed, unable to setup udp socket: {}", wsaErrorStr()));

    _portHostOrder = ntohs(boundAddr.sin_port);

    // get machine host name
    char hostName[256]{};
    if (gethostname(hostName, sizeof(hostName)) == SOCKET_ERROR)
        throw std::runtime_error(std::format("[Server] gethostname() failed, unable to setup udp socket: {}", wsaErrorStr()));

    addrinfo hints{};
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_DGRAM;
    hints.ai_protocol = IPPROTO_UDP;

    addrinfo* result = nullptr;
    if (getaddrinfo(hostName, nullptr, &hints, &result) != 0)
        throw std::runtime_error(std::format("[Server] getaddrinfo() failed, unable to setup udp socket"));

    char ipStr[INET_ADDRSTRLEN]{};
    auto* ipv4 = reinterpret_cast<sockaddr_in*>(result->ai_addr);
    inet_ntop(AF_INET, &(ipv4->sin_addr), ipStr, sizeof(ipStr));
    _ip = ipStr;
    freeaddrinfo(result);
    threadSafeOStream(std::cout, std::format("[Server]: {}:{}", _ip, _portHostOrder));
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
    if (_thread.joinable()) throw std::runtime_error("[Server] startAcceptingClients() failed, thread is already assigned");
    _thread = std::thread([this]()
        {
            actualStartListening(_stopSource.get_token());
        });
}

bool Server::isListeningThreadFinished() noexcept
{
    return _threadFinished;
}

void Server::sendCanvasDrawState(const CanvasDrawState& cds)
{
    std::vector<char> msg;
    switch (cds._type)
    {
        using enum MessageType;
    case PF_START_STROKE:
    {
        // 1 for id
        // 8 for header (seq + session id)
        // 13 for actual payload
        msg.resize(1 + 8 + 13);
        msg[0] = static_cast<char>(cds._type);
        auto sequenceNumberNetworkOrder = htonl(cds._sqNumberHostOrder);
        //std::memcpy(msg.data() + 1, ) // SESSION ID FILL IN LATER
        std::memcpy(msg.data() + 5, &sequenceNumberNetworkOrder, sizeof(sequenceNumberNetworkOrder));

        std::uint32_t idNetworkOrder;
        std::uint16_t mxNetworkOrder;
        std::uint16_t myNetworkOrder;
        //std::uint8_t r;
        //std::uint8_t g;
        //std::uint8_t b;
        //std::uint8_t a;
        //std::uint8_t t;
        std::memcpy(&idNetworkOrder, cds._msg.data(), sizeof(idNetworkOrder));
        std::memcpy(&mxNetworkOrder, cds._msg.data() + 4, sizeof(mxNetworkOrder));
        std::memcpy(&myNetworkOrder, cds._msg.data() + 6, sizeof(myNetworkOrder));
        //std::memcpy(&r, cds._msg.data() + 8, 1);
        //std::memcpy(&g, cds._msg.data() + 9, 1);
        //std::memcpy(&b, cds._msg.data() + 10, 1);
        //std::memcpy(&a, cds._msg.data() + 11, 1);
        //std::memcpy(&t, cds._msg.data() + 12, 1);
        idNetworkOrder = htonl(idNetworkOrder);
        mxNetworkOrder = htons(mxNetworkOrder);
        myNetworkOrder = htons(myNetworkOrder);

        std::memcpy(msg.data() + 9, &idNetworkOrder, sizeof(idNetworkOrder));
        std::memcpy(msg.data() + 13, &mxNetworkOrder, sizeof(mxNetworkOrder));
        std::memcpy(msg.data() + 15, &myNetworkOrder, sizeof(myNetworkOrder));
        std::memcpy(msg.data() + 17, cds._msg.data() + 8, 5);
        break;
    }
    case PF_ADD_POINT:
    {
        // 1 for id
        // 8 for header (seq + session id)
        // 4 for actual payload
        msg.resize(1 + 8 + 4);
        msg[0] = static_cast<char>(cds._type);
        auto sequenceNumberNetworkOrder = htonl(cds._sqNumberHostOrder);
        //std::memcpy(msg.data() + 1, ) // SESSION ID FILL IN LATER
        std::memcpy(msg.data() + 5, &sequenceNumberNetworkOrder, sizeof(sequenceNumberNetworkOrder));
        
        std::uint16_t mxNetworkOrder;
        std::uint16_t myNetworkOrder;

        std::memcpy(&mxNetworkOrder, cds._msg.data(), sizeof(mxNetworkOrder));
        std::memcpy(&myNetworkOrder, cds._msg.data() + 2, sizeof(myNetworkOrder));

        mxNetworkOrder = htons(mxNetworkOrder);
        myNetworkOrder = htons(myNetworkOrder);

        std::memcpy(msg.data() + 9, &mxNetworkOrder, sizeof(mxNetworkOrder));
        std::memcpy(msg.data() + 11, &myNetworkOrder, sizeof(myNetworkOrder));
        break;
    }
    case PF_END_STROKE:
    {
        // 1 for id
        // 8 for header (seq + session id)
        msg.resize(1 + 8);
        msg[0] = static_cast<char>(cds._type);
        auto sequenceNumberNetworkOrder = htonl(cds._sqNumberHostOrder);
        //std::memcpy(msg.data() + 1, ) // SESSION ID FILL IN LATER
        std::memcpy(msg.data() + 5, &sequenceNumberNetworkOrder, sizeof(sequenceNumberNetworkOrder));
        break;
    }
    default:
    {
        assert(false && "Send canvas draw state is only meant for certain message types");
    }
    }
    for (const auto& [sessionIdHostOrder, client] : _sessionIdToClient)
    {
        // update session id
        SessionId sessionIdNetworkOrder = htonl(sessionIdHostOrder);
        std::memcpy(msg.data() + 1, &sessionIdNetworkOrder, sizeof(sessionIdNetworkOrder));
    for (int attempt = 0; attempt < _maxRetry; ++attempt)
    {

        int sentBytes = sendto(
            _socket,
            msg.data(),
            static_cast<int>(msg.size()),
            0,
            reinterpret_cast<const sockaddr*>(&client.sa),
            sizeof(client.sa)
        );

        if (sentBytes == SOCKET_ERROR)
        {
            const int err = WSAGetLastError();
            if (isRecoverableWSAError(err))
            {
                continue;
            }

            threadSafeOStream(
                std::cerr,
                std::format("[Server] sendto() failed: {}", wsaErrorStr())
            );
            return;
        }
        else
        {
            return; // success
        }
    }
    threadSafeOStream(std::cerr,
        std::format("[Server] Failed to send canvas state to client {}",client.ipPort));
    }
}

Server::RegCdsFnId Server::registerCdsFn(std::function<void(SessionId sessionIdHostOrder,const CanvasDrawState&)> fn)
{
    std::lock_guard lock(_cdsFnsMutex);
    _cdsFns.emplace(_nextRegCdsFnId, fn);
    return _nextRegCdsFnId++;
}

void Server::deregisterCdsFn(RegCdsFnId id)
{
    std::lock_guard lock(_cdsFnsMutex);
    auto it = _cdsFns.find(id);
    if (it == _cdsFns.end())
    {
        threadSafeOStream(std::cerr,
            std::format("[Server] Requested to deregister callback function but id is not found: {}", id));
        return;
    }
    _cdsFns.erase(it);
    return;
}

void Server::handle_pfStartStroke(std::span<const char> udpPacketWithoutMID, sockaddr_in* sa)
{
    assert(udpPacketWithoutMID.size() == 21 && "Start stroke size is wrong");
    SessionId sessionIdHostOrder;
    SequenceNumber sequenceNumberHostOrder;
    std::memcpy(&sessionIdHostOrder, udpPacketWithoutMID.data(), sizeof(sessionIdHostOrder));
    std::memcpy(&sequenceNumberHostOrder, udpPacketWithoutMID.data() + 4, sizeof(sequenceNumberHostOrder));
    
    sessionIdHostOrder = ntohl(sessionIdHostOrder);
    sequenceNumberHostOrder = ntohl(sequenceNumberHostOrder);
    // TODO : VALIDATE SEQUENCE NUMBER

    std::uint32_t idHostOrder;
    std::uint16_t mxHostOrder, myHostOrder;
    std::uint8_t r, g, b, a, t;

    std::memcpy(&idHostOrder, udpPacketWithoutMID.data() + 8, sizeof(idHostOrder));
    std::memcpy(&mxHostOrder, udpPacketWithoutMID.data() + 12, sizeof(mxHostOrder));
    std::memcpy(&myHostOrder, udpPacketWithoutMID.data() + 14, sizeof(myHostOrder));

    idHostOrder = ntohl(idHostOrder);
    mxHostOrder = ntohs(mxHostOrder);
    myHostOrder = ntohs(myHostOrder);

    std::memcpy(&r, udpPacketWithoutMID.data() + 16, sizeof(r));
    std::memcpy(&g, udpPacketWithoutMID.data() + 17, sizeof(g));
    std::memcpy(&b, udpPacketWithoutMID.data() + 18, sizeof(b));
    std::memcpy(&a, udpPacketWithoutMID.data() + 19, sizeof(a));
    std::memcpy(&t, udpPacketWithoutMID.data() + 20, sizeof(t));

    CanvasDrawState cds;
    cds._sqNumberHostOrder = sequenceNumberHostOrder;
    cds._type = MessageType::PF_START_STROKE;
    cds._msg.resize(13);
    
    std::memcpy(cds._msg.data(), &idHostOrder, sizeof(idHostOrder));
    std::memcpy(cds._msg.data() + 4, &mxHostOrder, sizeof(mxHostOrder));
    std::memcpy(cds._msg.data() + 6, &myHostOrder, sizeof(myHostOrder));
    std::memcpy(cds._msg.data() + 8, &r, sizeof(r));
    std::memcpy(cds._msg.data() + 9, &g, sizeof(g));
    std::memcpy(cds._msg.data() + 10, &b, sizeof(b));
    std::memcpy(cds._msg.data() + 11, &a, sizeof(a));
    std::memcpy(cds._msg.data() + 12, &t, sizeof(t));
    handle_CanvasDrawingCommand(cds, sessionIdHostOrder);
}

void Server::handle_pfAddPoint(std::span<const char> udpPacketWithoutMID, sockaddr_in* sa)
{
    assert(udpPacketWithoutMID.size() == 12 && "Add point size is wrong");
    SessionId sessionIdHostOrder;
    SequenceNumber sequenceNumberHostOrder;
    std::memcpy(&sessionIdHostOrder, udpPacketWithoutMID.data(), sizeof(sessionIdHostOrder));
    std::memcpy(&sequenceNumberHostOrder, udpPacketWithoutMID.data() + 4, sizeof(sequenceNumberHostOrder));

    sessionIdHostOrder = ntohl(sessionIdHostOrder);
    sequenceNumberHostOrder = ntohl(sequenceNumberHostOrder);

    std::uint16_t mxHostOrder, myHostOrder;
    std::memcpy(&mxHostOrder, udpPacketWithoutMID.data() + 8, sizeof(mxHostOrder));
    std::memcpy(&myHostOrder, udpPacketWithoutMID.data() + 10, sizeof(myHostOrder));
    mxHostOrder = ntohs(mxHostOrder);
    myHostOrder = ntohs(myHostOrder);

    CanvasDrawState cds;
    cds._sqNumberHostOrder = sequenceNumberHostOrder;
    cds._type = MessageType::PF_ADD_POINT;
    cds._msg.resize(4);

    std::memcpy(cds._msg.data(), &mxHostOrder, sizeof(mxHostOrder));
    std::memcpy(cds._msg.data() + 2, &myHostOrder, sizeof(myHostOrder));
    handle_CanvasDrawingCommand(cds, sessionIdHostOrder);
}

void Server::handle_pfEndStroke(std::span<const char> udpPacketWithoutMID, sockaddr_in* sa)
{
    assert(udpPacketWithoutMID.size() == 8 && "End stroke size is wrong");
    SessionId sessionIdHostOrder;
    SequenceNumber sequenceNumberHostOrder;
    std::memcpy(&sessionIdHostOrder, udpPacketWithoutMID.data(), sizeof(sessionIdHostOrder));
    std::memcpy(&sequenceNumberHostOrder, udpPacketWithoutMID.data() + 4, sizeof(sequenceNumberHostOrder));

    sessionIdHostOrder = ntohl(sessionIdHostOrder);
    sequenceNumberHostOrder = ntohl(sequenceNumberHostOrder);

    CanvasDrawState cds;
    cds._sqNumberHostOrder = sequenceNumberHostOrder;
    cds._type = MessageType::PF_END_STROKE;
    handle_CanvasDrawingCommand(cds, sessionIdHostOrder);
}

void Server::handle_CanvasDrawingCommand(CanvasDrawState cds, SessionId sessionIdHostOrder)
{
    std::lock_guard lock(_cdsFnsMutex);
    for (const auto& [_ignore, fns] : _cdsFns) fns(sessionIdHostOrder, cds);
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
            std::format("[Server] Client {}: REQ_REGISTER received {} bytes but expected 0 bytes in its payload",
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
        auto [_ignore,succeed] = _sessionIdToClient.emplace(
            std::make_pair(
                sessionIdHostOrder,
                Client{ .ipPort = ipStrAndPort, .sa = *sa }
            ));
        assert(succeed && "Session id registration has logic error");
        threadSafeOStream(std::cout, std::format("[Server] Client: {} connected", ipStrAndPort));
    }
    else
    {
        threadSafeOStream(std::cerr, std::format("[Server] Client {}: Unable to register",ipStrAndPort));
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
            std::format("[Server] Client {}: REQ_UNREGISTER received {} bytes but expected {} bytes in its payload",
                ipStrAndPort,udpPacketWithoutMID.size(), expectedBytes));
        return;
    }
    SessionId sessionIdNetworkOrder;
    std::memcpy(&sessionIdNetworkOrder, udpPacketWithoutMID.data(), sizeof(sessionIdNetworkOrder));
    SessionId sessionIdHostOrder = ntohl(sessionIdNetworkOrder);

    if (!destroySessionIdHostOrder(sessionIdHostOrder, ipStrAndPort))
    {
        threadSafeOStream(std::cerr,
            std::format("[Server] Client {}: Request to destroy session id {} but not found on server side",
                ipStrAndPort,sessionIdHostOrder));
        return;
    }
    threadSafeOStream(std::cout, std::format("[Server] Client: {} disconnected", ipStrAndPort));
    return;
}

void Server::handle_pfInputState(std::span<const char> udpPacketWithoutMID, sockaddr_in* sa)
{
    SessionId sessionIdHostOrder;
    SequenceNumber sqNumberHostOrder;
    InputBits inputBitsHostOrder;
    MousePosition mousePositionHostOrder;

    std::memcpy(&sessionIdHostOrder, udpPacketWithoutMID.data(), sizeof(sessionIdHostOrder));
    std::memcpy(&sqNumberHostOrder, udpPacketWithoutMID.data() + 4, sizeof(sqNumberHostOrder));
    std::memcpy(&inputBitsHostOrder, udpPacketWithoutMID.data() + 8, sizeof(inputBitsHostOrder));
    std::memcpy(mousePositionHostOrder.data(), udpPacketWithoutMID.data() + 12,
        mousePositionHostOrder.size() * sizeof(mousePositionHostOrder[0]));

    sessionIdHostOrder = ntohl(sessionIdHostOrder);
    sqNumberHostOrder = ntohl(sqNumberHostOrder);
    inputBitsHostOrder = ntohl(inputBitsHostOrder);
    mousePositionHostOrder[0] = ntohs(mousePositionHostOrder[0]);
    mousePositionHostOrder[1] = ntohs(mousePositionHostOrder[1]);
}

void Server::actualStartListening(std::stop_token st) noexcept
{
    std::vector<char> udpPacket;
    udpPacket.resize(MaxUdpPacketBytes);
    while (!st.stop_requested())
    {
        fd_set readSet;
        FD_ZERO(&readSet);
        FD_SET(_socket, &readSet);

        timeval timeout{};
        timeout.tv_sec = 0;
        timeout.tv_usec = static_cast<int>(_recvTimeOut * 1000); // 100 ms
        int ready = select(
            0,
            &readSet,
            nullptr,
            nullptr,
            &timeout
        );

        if (ready == SOCKET_ERROR)
        {
            threadSafeOStream(std::cerr,std::format("[Server] select() failed: {}", wsaErrorStr()));
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
                threadSafeOStream(std::cerr, std::format("[Server] recvfrom() failed: {}", wsaErrorStr()));
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

SessionId Server::getNextSessionIdHostOrder()
{
    assert(_nextSessionIdHostOrder != InvalidSessionId && "Ran out of session ids!");
    return _nextSessionIdHostOrder++;
}

bool Server::destroySessionIdHostOrder(SessionId id, std::string ipPort)
{
    auto it3 = _sessionIdToClient.find(id);
    if (it3 == _sessionIdToClient.end())
    {
        threadSafeOStream(std::cerr,
            std::format("[Server] Client {}: Requested to destroy a session id that is not active!!!!!",ipPort));
        return false;
    }
    if (it3->second.ipPort != ipPort)
    {
        threadSafeOStream(std::cerr,
            std::format("[Server] Client {}: Requested to destroy a session id that is not theirs.",ipPort));
        return false;
    }
    _sessionIdToClient.erase(it3);
    return true;
}