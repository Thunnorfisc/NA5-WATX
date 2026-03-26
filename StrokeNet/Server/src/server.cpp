/* Start Header
***********************************************************************/

/*! \file   server.cpp
    \author Loh Boon Cheong, Timothy
    \par    email: loh.b@digipen.edu
    \co-author Xavier Koh Zhi Kuang
    \par    email: z.koh@digipen.edu
    \co-author William Wibisana Dumanauw
    \par    email: williamwibisana.d@digipen.edu
    \date   20th March, 2026
    \brief  Copyright (C) 2026 DigiPen Institute of Technology

    Reproduction or diclosure of this file or its contents without the prior
    written consent of DigiPen Institute of Technology is prohibited. */

/* End Header
***********************************************************************/
#define _WINSOCK_DEPRECATED_NO_WARNINGS

#include "server.hpp"
#include "shared_protocol.hpp"

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
#include <fstream>
#include <algorithm>
#include <execution>
#include <filesystem>
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#include <Windows.h>
#include <winsock2.h>
#include <WS2tcpip.h>
#include <iphlpapi.h>

#undef min

#pragma comment(lib, "Ws2_32.lib")

std::mutex s_ostreamMutex;
void log(std::ostream& os, std::string_view msg)
{
    std::lock_guard lock(s_ostreamMutex);
    os << msg << '\n';
}

// ============================================================
// Constructor
// ============================================================

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
    addr.sin_port = htons(ServerUdpPort);

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
    _userStore.load();

    // make it non-blocking
    u_long mode = 1;
    ioctlsocket(_socket, FIONBIO, &mode);

    log(std::cout, std::format("[Server]: {}:{}", _ip, _portHostOrder));
}

// ============================================================
// Destructor
// ============================================================

Server::~Server() 
{
    _stopSource.request_stop();
    if (_thread.joinable()) _thread.join();

    if (_socket != INVALID_SOCKET) closesocket(_socket);
    WSACleanup();
}

// ============================================================
// Start listening
// ============================================================

void Server::startListening()
{
    if (_thread.joinable()) throw std::runtime_error("[Server] startAcceptingClients() failed, thread is already assigned");
    _thread = std::thread([this]()
        {
            actualStartListening(_stopSource.get_token());
        });
}

// ============================================================
// Is listening thread finished
// ============================================================

bool Server::isListeningThreadFinished() noexcept
{
    return _threadFinished;
}

// ============================================================
// REQ_LOGIN
// ============================================================

void Server::handle_reqLogin(std::span<const char> udpPacketWithoutMID, sockaddr_in* sa)
{
    assert((udpPacketWithoutMID.size() == PacketSize::REQ_LOGIN - 1) &&
        "Size of REQ_LOGIN packet received is wrong");

    char ipStr[INET_ADDRSTRLEN]{};
    inet_ntop(AF_INET, &sa->sin_addr, ipStr, INET_ADDRSTRLEN);
    auto port = ntohs(sa->sin_port);
    std::string ipStrAndPort = std::format("{}:{}", ipStr, port);

    // check if this ip:port is already registered
    {
        std::lock_guard lock(_clientStorageMutex);
        for (const auto& [_ignore, client] : _sessionIdToClient)
        {
            if (client.ipPort == ipStrAndPort)
            {
                log(std::cout,
                    std::format("[Server] Client {} is already logged in, ignoring REQ_LOGIN", ipStrAndPort));
                return;
            }
        }
    }

    ByteReader rdr{ .buffer = udpPacketWithoutMID };
    auto userBuf = rdr.read<std::array<char, MAX_USERNAME_LEN>>();
    auto passBuf = rdr.read<std::array<char, MAX_PASSWORD_LEN>>();

    std::string username(userBuf.data(), strnlen(userBuf.data(), MAX_USERNAME_LEN));
    std::string password(passBuf.data(), strnlen(passBuf.data(), MAX_PASSWORD_LEN));

    LoginStatus status;
    SessionId sessionIdHostOrder = InvalidSessionId;

    if (_userStore.authenticate(username, password))
    {
        sessionIdHostOrder = getNextSessionIdHostOrder();

        // place it into the list of players allowed to draw
        std::unique_lock lock(_gameMut);
        _listOfPlayersAllowedToDraw.push_back(sessionIdHostOrder);
        lock.unlock();

        status = LoginStatus::SUCCESS;
    }
    else
    {
        status = LoginStatus::INVALID_CREDENTIALS;
        log(std::cout,
            std::format("[Server] Client {}: Login failed for username '{}'", ipStrAndPort, username));
    }

    // build RSP_LOGIN_AND_CREATE_ACCOUNT
    auto sessionIdNetworkOrder = htonl(sessionIdHostOrder);
    std::vector<char> sendPacket;
    sendPacket.resize(PacketSize::RSP_LOGIN_AND_CREATE_ACCOUNT);
    ByteWriter wrt{ .buffer = sendPacket };
    wrt.write(static_cast<char>(MessageType::RSP_LOGIN_AND_CREATE_ACCOUNT));
    wrt.write(sessionIdNetworkOrder);
    wrt.write(static_cast<char>(status));

    bool success = sendWithRetry(sendPacket, *sa);

    if (success && status == LoginStatus::SUCCESS)
    {
        {
            std::lock_guard lock(_clientStorageMutex);
            auto [_ignore, succeed] = _sessionIdToClient.emplace(
                std::make_pair(
                    sessionIdHostOrder,
                    Client{ .ipPort = ipStrAndPort,.username = username, .sa = *sa }
                ));
            assert(succeed && "Session id registration has logic error");
        }
        log(std::cout,
            std::format("[Server] Client {}: '{}' logged in successfully", ipStrAndPort, username));
    }
    else if (!success)
    {
        log(std::cerr,
            std::format("[Server] Client {}: Unable to send RSP_LOGIN_AND_CREATE_ACCOUNT", ipStrAndPort));
    }
}

// ============================================================
// REQ_CREATE_ACCOUNT
// ============================================================

void Server::handle_reqCreateAccount(std::span<const char> udpPacketWithoutMID, sockaddr_in* sa)
{
    assert((udpPacketWithoutMID.size() == PacketSize::REQ_CREATE_ACCOUNT - 1) &&
        "Size of REQ_CREATE_ACCOUNT packet received is wrong");

    char ipStr[INET_ADDRSTRLEN]{};
    inet_ntop(AF_INET, &sa->sin_addr, ipStr, INET_ADDRSTRLEN);
    auto port = ntohs(sa->sin_port);
    std::string ipStrAndPort = std::format("{}:{}", ipStr, port);

    ByteReader rdr{ .buffer = udpPacketWithoutMID };
    auto userBuf = rdr.read<std::array<char, MAX_USERNAME_LEN>>();
    auto passBuf = rdr.read<std::array<char, MAX_PASSWORD_LEN>>();

    std::string username(userBuf.data(), strnlen(userBuf.data(), MAX_USERNAME_LEN));
    std::string password(passBuf.data(), strnlen(passBuf.data(), MAX_PASSWORD_LEN));

    LoginStatus status;

    if (username.empty())
    {
        status = LoginStatus::USERNAME_TOO_LONG;
        log(std::cout,
            std::format("[Server] Client {}: Account creation failed, empty username", ipStrAndPort));
    }
    else if (_userStore.createAccount(username, password))
    {
        status = LoginStatus::SUCCESS;
        log(std::cout,
            std::format("[Server] Client {}: Account '{}' created successfully", ipStrAndPort, username));
    }
    else
    {
        status = LoginStatus::USERNAME_TAKEN;
        log(std::cout,
            std::format("[Server] Client {}: Account creation failed, '{}' already exists", ipStrAndPort, username));
    }

    // respond with RSP_LOGIN_AND_CREATE_ACCOUNT (session id is invalid — they still need to login after creating)
    auto sessionIdNetworkOrder = htonl(InvalidSessionId);
    std::vector<char> sendPacket;
    sendPacket.resize(PacketSize::RSP_LOGIN_AND_CREATE_ACCOUNT);
    ByteWriter wrt{ .buffer = sendPacket };
    wrt.write(static_cast<char>(MessageType::RSP_LOGIN_AND_CREATE_ACCOUNT));
    wrt.write(sessionIdNetworkOrder);
    wrt.write(static_cast<char>(status));

    bool success = sendWithRetry(sendPacket, *sa);

    if (!success)
    {
        log(std::cerr,
            std::format("[Server] Client {}: Unable to send RSP_LOGIN_AND_CREATE_ACCOUNT for account creation", ipStrAndPort));
    }
}

// ============================================================
// FAF_DISCONNECT
// ============================================================

void Server::handle_fafDisconnect(std::span<const char> udpPacketWithoutMID, sockaddr_in* sa)
{
    assert((udpPacketWithoutMID.size() == PacketSize::FAF_DISCONNECT - 1) &&
        "Size of FAF_DISCONNECT packet received is wrong");
    char ipStr[INET_ADDRSTRLEN]{};
    inet_ntop(AF_INET, &sa->sin_addr, ipStr, INET_ADDRSTRLEN);
    auto port = ntohs(sa->sin_port);
    std::string ipStrAndPort = std::format("{}:{}", ipStr, port);

    ByteReader rdr{ .buffer = udpPacketWithoutMID };
    auto sessionIdHostOrder = ntohl(rdr.read<SessionId>());
    if (!destroySessionIdHostOrder(sessionIdHostOrder, ipStrAndPort))
    {
        log(std::cerr,
            std::format("[Server] Client {}: Request to destroy session id {} but not found on server side",
                ipStrAndPort,sessionIdHostOrder));
        return;
    }

    std::unique_lock lock(_gameMut);
    // delete from list of players allowed to draw
    auto it = std::ranges::find(_listOfPlayersAllowedToDraw, sessionIdHostOrder);
    if (it != _listOfPlayersAllowedToDraw.end())
    {
        _listOfPlayersAllowedToDraw.erase(it);

        if (!_listOfPlayersAllowedToDraw.empty()) _currentAllowedToDrawIndex %= _listOfPlayersAllowedToDraw.size();
        else _currentAllowedToDrawIndex = 0;
    }
    else
    {
        log(std::cerr,
            std::format("[Server] Tried to remove client {} from the list of players allowed to draw, but not able to find",sessionIdHostOrder));
    }
    lock.unlock();

    log(std::cout, std::format("[Server] Client: {} disconnected", ipStrAndPort));
    return;
}

// ============================================================
// NTF_RCV_CLEAR_CANVAS
// ============================================================

void Server::handle_ntfRcvClearCanvas(std::span<const char> udpPacketWithoutMID, sockaddr_in* sa)
{
    ByteReader rdr{ .buffer = udpPacketWithoutMID };
    auto sessionIdHostOrder = ntohl(rdr.read<SessionId>());
    auto clearIdHostOrder = ntohl(rdr.read<std::uint32_t>());

    std::lock_guard lock(_pendingNtfClearCanvasMutex);
    _pendingNtfClearCanvases.erase(NtfKey{ sessionIdHostOrder, clearIdHostOrder });
}

// ============================================================
// NTF_RCV_MSG
// ============================================================

void Server::handle_ntfRcvMsg(std::span<const char> udpPacketWithoutMID, sockaddr_in* sa)
{
    ByteReader rdr{ .buffer = udpPacketWithoutMID };
    auto sessionIdHostOrder = ntohl(rdr.read<SessionId>());
    auto msgIdHostOrder = ntohl(rdr.read<std::uint32_t>());

    std::lock_guard lock(_pendingNtfMsgMutex);
    _pendingNtfMsg.erase(NtfKey{ sessionIdHostOrder, msgIdHostOrder });
}

// ============================================================
// NTF_RCV_UPDATE_SCOREBOARD
// ============================================================

void Server::handle_ntfRcvUpdateScoreboard(std::span<const char> udpPacketWithoutMID, sockaddr_in* sa)
{
    assert((udpPacketWithoutMID.size() == PacketSize::NTF_RCV_UPDATE_SCOREBOARD - 1) &&
        "Size of NTF_RCV_UPDATE_SCOREBOARD packet received is wrong");
    ByteReader rdr{ .buffer = udpPacketWithoutMID };
    auto sessionIdHostOrder = ntohl(rdr.read<SessionId>());
    auto scoreIdHostOrder = ntohl(rdr.read<std::uint32_t>());

    std::lock_guard lock(_pendingNtfUpdateScoreboardMutex);
    _pendingNtfUpdateScoreboards.erase(NtfKey{ sessionIdHostOrder, scoreIdHostOrder });
}


// ============================================================
// REQ_START_STROKE
// ============================================================

void Server::handle_reqStartStroke(std::span<const char> udpPacketWithoutMID, sockaddr_in* sa)
{
    assert((udpPacketWithoutMID.size() == PacketSize::REQ_START_STROKE - 1) &&
        "Size of REQ_START_STROKE packet received is wrong");

    std::unique_lock lock(_gameMut);
    if (!_gameRunning)
    {
        log(std::cerr,
            std::format("[Server] Received REQ_START_STROKE but game is not started"));
        return;
    }

    if (_listOfPlayersAllowedToDraw.empty())
    {
        log(std::cerr,
            std::format("[Server] Received REQ_START_STROKE but list of allowed players to draw is empty"));
        return;
    }
    if (_currentAllowedToDrawIndex >= _listOfPlayersAllowedToDraw.size())
    {
        _currentAllowedToDrawIndex = 0;
    }

    ByteReader rdr{ .buffer = udpPacketWithoutMID };
    auto sessionIdHostOrder = ntohl(rdr.read<SessionId>());
    
    // check if session id exists
    auto it = _sessionIdToClient.find(sessionIdHostOrder);
    if (it == _sessionIdToClient.end())
    {
        // no session id exists, ignore
        log(std::cerr,
            std::format("[Server] Received REQ_START_STROKE from unknown client session id: {}", sessionIdHostOrder));
        lock.unlock();
        return;
    }

    // check if session id is allowed to draw
    if (_listOfPlayersAllowedToDraw[_currentAllowedToDrawIndex] != sessionIdHostOrder)
    {
        // not allowed to draw, ignore
        log(std::cerr,
            std::format("[Server] Received REQ_START_STROKE from client session id {} but not allowed to draw", sessionIdHostOrder));
        lock.unlock();
        return;
    }
    lock.unlock();

    auto strokeIdHostOrder = ntohl(rdr.read<std::uint32_t>());
    if (it->second.currentStrokeId.has_value())
    {
        // ignore this request to start stroke, the client has already started their stroke
        log(std::cerr,
            std::format("[Server] Received REQ_START_STROKE from client session id {} but the client already has started the current stroke {}",
                sessionIdHostOrder, strokeIdHostOrder));
        return;
    }

    auto mousePosNetworkOrder = rdr.read<MousePosition>();
    auto rgbat = rdr.read<std::array<char, 5>>();

    // validated its good REQ_START_STROKE packet, send back RSP_START_STROKE
    std::array<char, PacketSize::RSP_START_STROKE> rspmsg;
    ByteWriterN rspwrt{ .buffer = rspmsg };
    rspwrt.write(static_cast<char>(MessageType::RSP_START_STROKE));
    rspwrt.write(htonl(sessionIdHostOrder));
    rspwrt.write(htonl(strokeIdHostOrder));

    bool successRsp = sendWithRetry(rspmsg, *sa);

    // if not able to send rsp_start_stroke, just return, its ok. wtv
    if (!successRsp)
    {
        log(std::cerr,
            std::format("[Server] Unable to send RSP_START_STROKE for client session id {}", sessionIdHostOrder));
        return;
    }

    // set the currentStrokeId
    it->second.currentStrokeId = strokeIdHostOrder;

    // just send back to all clients
    std::array<char, PacketSize::SVR_START_STROKE> svrmsg;
    ByteWriterN svrwrt{ .buffer = svrmsg };
    svrwrt.write(static_cast<char>(MessageType::SVR_START_STROKE));
    svrwrt.write(std::uint32_t{}); // dummy
    svrwrt.write(mousePosNetworkOrder);
    svrwrt.write(rgbat);
    broadcastPacket(svrmsg,
        [&](auto& pkt, SessionId sid)
        {
            SessionId networkSID = htonl(sid);
            std::memcpy(pkt.data() + sizeof(MessageType::SVR_START_STROKE), &networkSID, sizeof(networkSID));
        });
}

// ============================================================
// REQ_END_STROKE
// ============================================================

void Server::handle_reqEndStroke(std::span<const char> udpPacketWithoutMID, sockaddr_in* sa)
{
    assert((udpPacketWithoutMID.size() == PacketSize::REQ_END_STROKE - 1) &&
        "Size of REQ_END_STROKE packet received is wrong");
    std::unique_lock lock(_gameMut);
    if (!_gameRunning)
    {
        log(std::cerr,
            std::format("[Server] Received REQ_END_STROKE but game is not started"));
        return;
    }
    if (_listOfPlayersAllowedToDraw.empty())
    {
        log(std::cerr,
            std::format("[Server] Received REQ_END_STROKE but list of allowed players to draw is empty"));
        return;
    }
    if (_currentAllowedToDrawIndex >= _listOfPlayersAllowedToDraw.size())
    {
        _currentAllowedToDrawIndex = 0;
    }
    ByteReader rdr{ .buffer = udpPacketWithoutMID };
    auto sessionIdHostOrder = ntohl(rdr.read<SessionId>());

    // check if session id exists
    auto it = _sessionIdToClient.find(sessionIdHostOrder);
    if (it == _sessionIdToClient.end())
    {
        // no session id exists, ignore
        log(std::cerr,
            std::format("[Server] Received REQ_END_STROKE from unknown client session id: {}", sessionIdHostOrder));
        lock.unlock();
        return;
    }

    // check if session id is allowed to draw
    if (_listOfPlayersAllowedToDraw[_currentAllowedToDrawIndex] != sessionIdHostOrder)
    {
        // not allowed to draw, ignore
        log(std::cerr,
            std::format("[Server] Received REQ_END_STROKE from client session id {} but not allowed to draw", sessionIdHostOrder));
        lock.unlock();
        return;
    }
    lock.unlock();

    auto strokeIdHostOrder = ntohl(rdr.read<std::uint32_t>());
    if (!it->second.currentStrokeId.has_value())
    {
        // ignore this request to end stroke, there isnt an active stroke to end stroke for
        log(std::cerr,
            std::format("[Server] Received REQ_END_STROKE from client session id {} but the client does not have a current stroke",
                sessionIdHostOrder));
        return;
    }

    // validated its good REQ_END_STROKE packet, send back RSP_END_STROKE
    std::array<char, PacketSize::RSP_END_STROKE> rspmsg;
    ByteWriterN rspwrt{ .buffer = rspmsg };
    rspwrt.write(static_cast<char>(MessageType::RSP_END_STROKE));
    rspwrt.write(htonl(sessionIdHostOrder));
    rspwrt.write(htonl(strokeIdHostOrder));
    bool successRsp = sendWithRetry(rspmsg, *sa);

    // if not able to send REQ_END_STROKE, just return, its ok. wtv
    if (!successRsp)
    {
        log(std::cerr,
            std::format("[Server] Unable to send REQ_END_STROKE for client session id {}", sessionIdHostOrder));
        return;
    }

    // set client's stroke id to nullopt
    it->second.currentStrokeId = std::nullopt;

    // just send back to all clients
    std::array<char, PacketSize::SVR_END_STROKE> svrmsg;
    ByteWriterN svrwrt{ .buffer = svrmsg };
    svrwrt.write(static_cast<char>(MessageType::SVR_END_STROKE));
    svrwrt.write(std::uint32_t{}); // dummy
    broadcastPacket(svrmsg,
        [&](auto& pkt, SessionId sid)
        {
            SessionId networkSID = htonl(sid);
            std::memcpy(pkt.data() + sizeof(MessageType::SVR_END_STROKE), &networkSID, sizeof(networkSID));
        });
}

void Server::handle_reqMsg(std::span<const char> udpPacketWithoutMID, sockaddr_in* sa)
{
    ByteReader rdr{ .buffer = udpPacketWithoutMID };
    auto sessionIdHostOrder = ntohl(rdr.read<SessionId>());
    // check if session id exists
    auto it = _sessionIdToClient.find(sessionIdHostOrder);
    if (it == _sessionIdToClient.end())
    {
        // no session id exists, ignore
        log(std::cerr,
            std::format("[Server] Received REQ_MSG from unknown client session id: {}", sessionIdHostOrder));
        return;
    }

    auto msgIdHostOrder = ntohl(rdr.read<std::uint32_t>());
    auto msgLength = rdr.read<std::uint8_t>();
    auto actualmsg = rdr.readBytes(msgLength);

    // CHECK IF ITS NOT CURRENT DRAWER + IF ACTUAL MSG IS THE GUESS, THEN SEND BACK
    // "USER GUESSED THE WORD" @TODOOOOOOOOOOOOOOOOOOOOOOOOOOOOOOOOOOOOOOOOOOOOOOOOOOOOOOOOOOOOOOOOOOOOOOOO




    // validated its good REQ_MSG packet, send back RSP_MSG
    std::array<char, PacketSize::RSP_MSG> rspmsg;
    ByteWriterN rspwrt{ .buffer = rspmsg };
    rspwrt.write(static_cast<char>(MessageType::RSP_MSG));
    rspwrt.write(htonl(sessionIdHostOrder));
    rspwrt.write(htonl(msgIdHostOrder));
    bool successRsp = sendWithRetry(rspmsg, *sa);

    auto name = it->second.username;
    auto nameLength = static_cast<std::uint8_t>(name.length());
    if (nameLength > 15) // 15 is arbitrary here
    {
        name = name.substr(0, 12); // trunc it
        name += "...";
        nameLength = 15;
    }

    // Now NTF all clients for msg
    auto now = std::chrono::steady_clock::now();
    std::lock_guard ntfLock(_pendingNtfClearCanvasMutex);
    for (const auto& [ssiho, client] : _sessionIdToClient)
    {
        std::vector<char> svrmsg;
        svrmsg.resize(PacketSize::NTF_MSG_WITHOUT_BUFFER + msgLength + nameLength);
        ByteWriter svrwrt{ .buffer = svrmsg };
        svrwrt.write(static_cast<char>(MessageType::NTF_MSG));
        svrwrt.write(htonl(ssiho));
        svrwrt.write(htonl(_messageIdServer));
        svrwrt.write(msgLength);
        svrwrt.writeSpan(actualmsg);
        svrwrt.write(nameLength);
        svrwrt.writeSpan(name);

        // Send immediately once
        sockaddr_in clientSa = client.sa;
        sendto(_socket, svrmsg.data(), static_cast<int>(svrmsg.size()), 0,
            reinterpret_cast<sockaddr*>(&clientSa), sizeof(clientSa));

        // Add to pending for retry
        _pendingNtfClearCanvases[NtfKey{ ssiho, _messageIdServer }] = PendingNTF{
            ._data = std::move(svrmsg),
            ._clientAddr = client.sa,
            ._targetSessionId = ssiho,
            ._ntfId = _messageIdServer,
            ._nextSendTime = now + std::chrono::milliseconds(100),
            ._giveUpTime = now + std::chrono::seconds(2),
        };
    }
    _messageIdServer++;
}

// ============================================================
// REQ_CLEAR_CANVAS
// ============================================================

void Server::handle_reqClearCanvas(std::span<const char> udpPacketWithoutMID, sockaddr_in* sa)
{
    assert(udpPacketWithoutMID.size() == PacketSize::REQ_CLEAR_CANVAS - 1);

    std::unique_lock lock(_gameMut);
    if (!_gameRunning) return;
    if (_listOfPlayersAllowedToDraw.empty())
    {
        log(std::cerr,
            std::format("[Server] Received REQ_CLEAR_CANVAS but list of allowed players to draw is empty"));
        return;
    }
    if (_currentAllowedToDrawIndex >= _listOfPlayersAllowedToDraw.size())
    {
        _currentAllowedToDrawIndex = 0;
    }
    ByteReader rdr{ .buffer = udpPacketWithoutMID };
    auto sessionIdHostOrder = ntohl(rdr.read<SessionId>());

    auto it = _sessionIdToClient.find(sessionIdHostOrder);
    if (it == _sessionIdToClient.end()) { lock.unlock(); return; }

    // Only the current drawer can clear
    if (_listOfPlayersAllowedToDraw[_currentAllowedToDrawIndex] != sessionIdHostOrder)
    {
        log(std::cerr,
            std::format("[Server] Received REQ_CLEAR_CANVAS from client session id {} but not allowed to draw", sessionIdHostOrder));
        lock.unlock(); return;
    }
    lock.unlock();

    auto clearIdHostOrder = ntohl(rdr.read<std::uint32_t>());

    // Send RSP_CLEAR_CANVAS back to the requester
    std::array<char, PacketSize::RSP_CLEAR_CANVAS> rspmsg;
    ByteWriterN rspwrt{ .buffer = rspmsg };
    rspwrt.write(static_cast<char>(MessageType::RSP_CLEAR_CANVAS));
    rspwrt.write(htonl(sessionIdHostOrder));
    rspwrt.write(htonl(clearIdHostOrder));

    bool success = false;
    for (int i = 0; i < _maxRetry; i++)
    {
        int sentBytes = sendto(_socket, rspmsg.data(), static_cast<int>(rspmsg.size()), 0,
            reinterpret_cast<sockaddr*>(sa), sizeof(*sa));
        if (sentBytes == SOCKET_ERROR)
        {
            if (isRecoverableWSAError(WSAGetLastError())) continue;
            else
            {
                success = false;
                break;
            }
        }
        else
        {
            success = true;
            break;
        }
    }

    // if not able to send REQ_CLEAR_CANVAS, just return, its ok. wtv
    if (!success)
    {
        log(std::cerr,
            std::format("[Server] Unable to send REQ_CLEAR_CANVAS for client session id {}", sessionIdHostOrder));
        return;
    }

    // End any active stroke first
    if (it->second.currentStrokeId.has_value())
        it->second.currentStrokeId = std::nullopt;

    // Now NTF all clients to clear their canvas
    auto now = std::chrono::steady_clock::now();
    std::lock_guard ntfLock(_pendingNtfClearCanvasMutex);
    for (const auto& [ssiho, client] : _sessionIdToClient)
    {
        std::vector<char> ntfPkt(PacketSize::NTF_CLEAR_CANVAS);
        ByteWriter ntfWrt{ .buffer = ntfPkt };
        ntfWrt.write(static_cast<char>(MessageType::NTF_CLEAR_CANVAS));
        ntfWrt.write(htonl(ssiho));       // target's session ID
        ntfWrt.write(htonl(clearIdHostOrder));

        // Send immediately once
        sockaddr_in clientSa = client.sa;
        sendto(_socket, ntfPkt.data(), static_cast<int>(ntfPkt.size()), 0,
            reinterpret_cast<sockaddr*>(&clientSa), sizeof(clientSa));

        // Add to pending for retry
        _pendingNtfClearCanvases[NtfKey{ ssiho, clearIdHostOrder }] = PendingNTF{
            ._data = std::move(ntfPkt),
            ._clientAddr = client.sa,
            ._targetSessionId = ssiho,
            ._ntfId = clearIdHostOrder,
            ._nextSendTime = now + std::chrono::milliseconds(100),
            ._giveUpTime = now + std::chrono::seconds(2),
        };
    }
}

// ============================================================
// FAF_EXTEND_STROKE
// ============================================================

void Server::handle_fafExtendStroke(std::span<const char> udpPacketWithoutMID, sockaddr_in* sa)
{
    assert((udpPacketWithoutMID.size() == PacketSize::FAF_EXTEND_STROKE - 1) &&
        "Size of FAF_EXTEND_STROKE packet received is wrong");
    std::unique_lock lock(_gameMut);
    if (!_gameRunning)
    {
        log(std::cerr,
            std::format("[Server] Received FAF_EXTEND_STROKE but game is not started"));
        return;
    }
    if (_listOfPlayersAllowedToDraw.empty())
    {
        log(std::cerr,
            std::format("[Server] Received FAF_EXTEND_STROKE but list of allowed players to draw is empty"));
        return;
    }
    if (_currentAllowedToDrawIndex >= _listOfPlayersAllowedToDraw.size())
    {
        _currentAllowedToDrawIndex = 0;
    }
    ByteReader rdr{ .buffer = udpPacketWithoutMID };
    auto sessionIdHostOrder = ntohl(rdr.read<SessionId>());

    // check if session id exists
    auto it = _sessionIdToClient.find(sessionIdHostOrder);
    if (it == _sessionIdToClient.end())
    {
        // no session id exists, ignore
        log(std::cerr,
            std::format("[Server] Received FAF_EXTEND_STROKE from unknown client session id: {}", sessionIdHostOrder));
        lock.unlock();
        return;
    }

    // check if session id is allowed to draw
    if (_listOfPlayersAllowedToDraw[_currentAllowedToDrawIndex] != sessionIdHostOrder)
    {
        // not allowed to draw, ignore
        log(std::cerr,
            std::format("[Server] Received FAF_EXTEND_STROKE from client session id {} but not allowed to draw", sessionIdHostOrder));
        lock.unlock();
        return;
    }
    lock.unlock();

    auto strokeIdHostOrder = ntohl(rdr.read<std::uint32_t>());
    if (!it->second.currentStrokeId.has_value())
    {
        // ignore this request to extend stroke, there isnt an active stroke to extend stroke for
        log(std::cerr,
            std::format("[Server] Received FAF_EXTEND_STROKE from client session id {} but the client does not have a current stroke",
                sessionIdHostOrder));
        return;
    }

    auto mousePosNetworkOrder = rdr.read<MousePosition>();

    // just send back to all clients
    std::array<char, PacketSize::SVR_EXTEND_STROKE> svrmsg;
    ByteWriterN svrwrt{ .buffer = svrmsg };
    svrwrt.write(static_cast<char>(MessageType::SVR_EXTEND_STROKE));
    svrwrt.write(std::uint32_t{}); // dummy
    svrwrt.write(mousePosNetworkOrder);
    broadcastPacket(svrmsg,
        [&](auto& pkt, SessionId sid)
        {
            SessionId networkSID = htonl(sid);
            std::memcpy(pkt.data() + sizeof(MessageType::SVR_EXTEND_STROKE), &networkSID, sizeof(networkSID));
        });
}

// ============================================================
// Start listening
// ============================================================

void Server::actualStartListening(std::stop_token st) noexcept
{
    std::vector<char> udpPacket;
    udpPacket.resize(MaxUdpPacketBytes);
    while (!st.stop_requested())
    {
        tickPendingNtf(_pendingNtfClearCanvasMutex, _pendingNtfClearCanvases, "NTF_CLEAR_CANVAS");
        tickPendingNtf(_pendingNtfMsgMutex, _pendingNtfMsg, "NTF_MSG");
        tickPendingNtf(_pendingNtfUpdateScoreboardMutex, _pendingNtfUpdateScoreboards, "NTF_UPDATE_SCOREBOARD");

        fd_set readSet;
        FD_ZERO(&readSet);
        FD_SET(_socket, &readSet);

        timeval timeout{};
        timeout.tv_sec = 0;
        timeout.tv_usec = static_cast<int>(_recvTimeOut * 1'000'000.0);

        int ready = select(0, &readSet, nullptr, nullptr, &timeout);

        if (ready == SOCKET_ERROR)
        {
            if (!isRetryableSelectError(WSAGetLastError()))
            {
                log(std::cerr, "[Server] listen select() failed");
                goto end;
            }
            else continue;
        }

        if (ready == 0) continue;

        while (true)
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
                if (WSAGetLastError() == WSAEWOULDBLOCK) break; // fully drained
                log(std::cerr, std::format("[Server] recvfrom() failed: {}", wsaErrorStr()));
                break;
            }
            else if (bytesReceived == 0) continue; // move on with our lives
            MessageType message = static_cast<MessageType>(udpPacket[0]);
            auto it = _messageTypeFns.find(message);
            if (it != _messageTypeFns.end())
                (this->*it->second)(std::span<const char>(udpPacket).subspan(1,bytesReceived - 1), &from);
        }
    }
    end:
    _threadFinished = true;
}

// ============================================================
// Get next session id
// ============================================================

SessionId Server::getNextSessionIdHostOrder()
{
    assert(_nextSessionIdHostOrder != InvalidSessionId && "Ran out of session ids!");
    std::lock_guard lock(_clientStorageMutex);
    return _nextSessionIdHostOrder++;
}

// ============================================================
// Destroy session id
// ============================================================

bool Server::destroySessionIdHostOrder(SessionId id, std::string ipPort)
{
    std::lock_guard lock(_clientStorageMutex);
    auto it3 = _sessionIdToClient.find(id);
    if (it3 == _sessionIdToClient.end())
    {
        log(std::cerr,
            std::format("[Server] Client {}: Requested to destroy a session id that is not active!!!!!",ipPort));
        return false;
    }
    if (it3->second.ipPort != ipPort)
    {
        log(std::cerr,
            std::format("[Server] Client {}: Requested to destroy a session id that is not theirs.",ipPort));
        return false;
    }
    _sessionIdToClient.erase(it3);
    return true;
}

void Server::tickPendingNtf(std::mutex& mut, std::unordered_map<NtfKey, PendingNTF, NtfKeyHash>& map, std::string_view name)
{
    auto now = std::chrono::steady_clock::now();
    std::lock_guard lock(mut);
    for (auto it = map.begin(); it != map.end();)
    {
        if (now >= it->second._giveUpTime)
        {
            log(std::cerr, std::format("[Server] {} to session {} timed out", name, it->first.sessionId));
            it = map.erase(it);
            continue;
        }
        if (now >= it->second._nextSendTime)
        {
            sockaddr_in sa = it->second._clientAddr;
            sendto(_socket, it->second._data.data(), static_cast<int>(it->second._data.size()), 0,
                reinterpret_cast<sockaddr*>(&sa), sizeof(sa));
            it->second._nextSendTime = now + std::chrono::milliseconds(100);
        }
        ++it;
    }
}

// ============================================================
// Load word list
// ============================================================
void Server::load_wordlist() {
    //std::filesystem::path currentPath = std::filesystem::current_path();

    //// Print the path to standard output
    //std::cout << "Current path is: " << currentPath << std::endl;

    std::ifstream file{};
    file.open("resources/words.txt");
    if (!file.is_open())
        throw std::runtime_error("Failed to open words.txt");

    std::string word;
    while (std::getline(file, word)) {
        if (!word.empty() && word.back() == '\r')
            word.pop_back();
        if (!word.empty())
            word_list.push_back(word);
    }

    auto it = std::max_element(
        std::execution::par,
        word_list.begin(), word_list.end(),
        [](const std::string& a, const std::string& b) {
            return a.size() < b.size();
        }
    );

    if (it != word_list.end())
        max_len = static_cast<uint32_t>(it->size());
}

// ============================================================
// Pick word
// ============================================================

void Server::pick_word() {
    std::srand(static_cast<unsigned int>(std::time(nullptr)));
    if (word_list.empty()) return;
    word.second = word_list[std::rand() % word_list.size()];
    word.first = false;
    
    std::vector<char> vWord{ word.second.begin(), word.second.end() };
    ByteWriter SND{ .buffer = vWord};
    
    std::copy(SND.buffer.begin(), SND.buffer.end(), std::ostream_iterator<char>(std::cout));
    std::cout << std::endl;

}

// ============================================================
// Word heuristic
// ============================================================

int Server::word_heuristic() {
    return 0;
}

// ============================================================
// Advance drawer
// ============================================================

void Server::advanceDrawer()
{
    std::lock_guard lock(_gameMut);
    if (_listOfPlayersAllowedToDraw.empty())
    {
        log(std::cerr, std::format("[Server] Unable to advance drawer, no available drawers to pick from"));
        return;
    }
    // WHEN ADVANCE DRAWER, NEED TO SEND EVERYONE SVR_END_STROKE IF
    // there is a current stroke active
    SessionId currentDrawer = _listOfPlayersAllowedToDraw[_currentAllowedToDrawIndex];
    auto it = _sessionIdToClient.find(currentDrawer);
    if (it != _sessionIdToClient.end() && it->second.currentStrokeId.has_value())
    {
        it->second.currentStrokeId = std::nullopt; // remove the stroke id
        std::array<char, PacketSize::SVR_END_STROKE> svrmsg;
        ByteWriterN svrwrt{ .buffer = svrmsg };
        for (const auto& [ssiho, client] : _sessionIdToClient)
        {
            svrwrt.write(static_cast<char>(MessageType::SVR_END_STROKE));
            svrwrt.write(htonl(ssiho));
            //if (ssiho == sessionIdHostOrder) continue; // dont send back to itself

            bool success = sendWithRetry(svrmsg, client.sa);

            // if not able to send SVR_END_STROKE, its ok. wtv
            if (!success)
            {
                log(std::cerr,
                    std::format("[Server] Unable to send SVR_END_STROKE for client session id {}", ssiho));
            }

            svrwrt.offset = 0; // offset at zero to write from the beginning again
        }
    }

    _currentAllowedToDrawIndex = (_currentAllowedToDrawIndex + 1) % _listOfPlayersAllowedToDraw.size();
    log(std::cout, std::format("[Server] Advanced drawer, new drawer: {}", _listOfPlayersAllowedToDraw[_currentAllowedToDrawIndex]));

    return;
}

// ============================================================
// Broadcast Scoreboard
// ============================================================

void Server::broadcastScoreboard()
{
    auto now = std::chrono::steady_clock::now();
    std::lock_guard ntfLock(_pendingNtfUpdateScoreboardMutex);

    for (const auto& [ssiho, client] : _sessionIdToClient)
    {
        auto name = client.username;
        auto nameLength = static_cast<std::uint8_t>(std::min(name.size(), std::size_t(12)));

        std::uint16_t score = client.score;

        std::vector<char> pkt(PacketSize::NTF_UPDATE_SCOREBOARD + nameLength);
        ByteWriter wrt{ .buffer = pkt };
        wrt.write(static_cast<char>(MessageType::NTF_UPDATE_SCOREBOARD));
        wrt.write(htonl(ssiho));                                              // target session id
        wrt.write(htonl(_messageIdServer));                                   // score id
        wrt.write(htonl(static_cast<std::uint32_t>(_sessionIdToClient.size()))); // num players
        wrt.write(nameLength);
        wrt.writeSpan(std::span<const char>(name.data(), nameLength));
        wrt.write(htons(score));

        sockaddr_in clientSa = client.sa;
        sendto(_socket, pkt.data(), static_cast<int>(pkt.size()), 0,
            reinterpret_cast<sockaddr*>(&clientSa), sizeof(clientSa));

        _pendingNtfUpdateScoreboards[NtfKey{ ssiho, _messageIdServer }] = PendingNTF{
            ._data = std::move(pkt),
            ._clientAddr = client.sa,
            ._targetSessionId = ssiho,
            ._ntfId = _messageIdServer,
            ._nextSendTime = now + std::chrono::milliseconds(100),
            ._giveUpTime = now + std::chrono::seconds(2),
        };
    }
    _messageIdServer++;
}

// ============================================================
// Start game
// ============================================================

void Server::startGame()
{
    std::lock_guard lock(_gameMut);
    if (_listOfPlayersAllowedToDraw.empty())
    {
        log(std::cerr, "[Server] Cannot start game, no players connected");
        return;
    }
    _currentAllowedToDrawIndex = 0;
    _gameRunning = true;
    log(std::cout, std::format("[Server] Game started, first drawer: {}", _listOfPlayersAllowedToDraw[0]));
}

// ============================================================
// Stop game
// ============================================================

void Server::stopGame()
{
    std::lock_guard lock(_gameMut);
    _gameRunning = false;
    log(std::cout, "[Server] Game stopped");
}

// ============================================================
// Game Started
// ============================================================

bool Server::gameStarted()
{
    std::lock_guard lock(_gameMut);
    return _gameRunning;
}

// ============================================================
// Get number of players
// ============================================================

std::size_t Server::getNumberOfPlayers()
{
    std::lock_guard lock(_gameMut);
    return _listOfPlayersAllowedToDraw.size();
}

// ============================================================
// Helper - Send with retry
// ============================================================

bool Server::sendWithRetry(std::span<const char> data, const sockaddr_in& sa)
{
    bool success = false;
    for (int i = 0; i < _maxRetry; i++)
    {
        int sentBytes = sendto(_socket, data.data(), static_cast<int>(data.size()), 0,
            reinterpret_cast<const sockaddr*>(&sa), sizeof(sa));
        if (sentBytes == SOCKET_ERROR)
        {
            if (isRecoverableWSAError(WSAGetLastError())) continue;
            else
            {
                success = false;
                break;
            }
        }
        else
        {
            success = true;
            break;
        }
    }
    return success;
}