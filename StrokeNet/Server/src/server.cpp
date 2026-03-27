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
    if (LOCK_clientStorage([&ipStrAndPort](const auto& map) {
        for (const auto& [_ignore, client] : map)
        {
            if (client.ipPort == ipStrAndPort) return true;
        }
        return false;
        }))
    {
        log(std::cout,
            std::format("[Server] Client {} is already logged in, ignoring REQ_LOGIN", ipStrAndPort));
        return;
    }

    

    ByteReader rdr{ .buffer = udpPacketWithoutMID };
    auto userBuf = rdr.read<std::array<char, MAX_USERNAME_LEN>>();
    auto passBuf = rdr.read<std::array<char, MAX_PASSWORD_LEN>>();

    std::string username(userBuf.data(), strnlen(userBuf.data(), MAX_USERNAME_LEN));
    std::string password(passBuf.data(), strnlen(passBuf.data(), MAX_PASSWORD_LEN));

    LoginStatus status;
    SessionId sessionIdHostOrder = InvalidSessionId;

    // CHECK USER LOGGED IN AHHH
    if (LOCK_clientStorage([&username](const auto& map) {
        for (const auto& [_ignore, client] : map)
        {
            if (client.username == username) return true;
        }
        return false;
        })) {
        log(std::cout,
            std::format("[Server] Client {}: Username '{}' is already logged in, rejecting", ipStrAndPort, username));

        // send rejection response
        auto sessionIdNetworkOrder = htonl(InvalidSessionId);
        std::vector<char> sendPacket;
        sendPacket.resize(PacketSize::RSP_LOGIN_AND_CREATE_ACCOUNT);
        ByteWriter wrt{ .buffer = sendPacket };
        wrt.write(static_cast<char>(MessageType::RSP_LOGIN_AND_CREATE_ACCOUNT));
        wrt.write(sessionIdNetworkOrder);
        wrt.write(static_cast<char>(LoginStatus::ALREADY_LOGGED_IN));
        sendWithRetry(sendPacket, *sa);
        return;
    }

    if (_userStore.authenticate(username, password))
    {
        sessionIdHostOrder = getNextSessionIdHostOrder();
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
        Client newClient{ .ipPort = ipStrAndPort,.username = username, .sa = *sa };
        
        bool succeedAddingNewClient = LOCK_clientStorage(
            [sessionIdHostOrder, client = std::move(newClient)](auto& map) mutable 
            {
            auto [_ignore, succeed] = map.emplace(sessionIdHostOrder, std::move(client));
            return succeed;
            });

        assert(succeedAddingNewClient && "Session id registration has logic error");

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
// REQ_PLAY_GAME
// ============================================================

void Server::handle_reqPlayGame(std::span<const char> udpPacketWithoutMID, sockaddr_in* sa)
{
    assert((udpPacketWithoutMID.size() == PacketSize::REQ_PLAY_GAME - 1) &&
        "Size of REQ_PLAY_GAME packet received is wrong");

    ByteReader rdr{udpPacketWithoutMID};
    
    SessionId sessionIdHost = ntohl(rdr.read<SessionId>());

    // If it finds the id inside the map,
    // it will set inGame to true, and return true
    // else it will return false, and we will early exit
    if (!LOCK_clientStorage([sessionIdHost](auto& map) {
        auto it = map.find(sessionIdHost);
        if (it == map.end()) return false;
        it->second.inGame = true;
        return true;
        })) return;

    LOCK_gameVariables([sessionIdHost](const auto& gameRunning, auto& drawerSessionIdOpt) {
        if (!drawerSessionIdOpt.has_value() && !gameRunning) drawerSessionIdOpt = sessionIdHost;
        });
    
    // send back ack
    std::array<char, PacketSize::RSP_PLAY_GAME> msg;
    ByteWriterN wrt{.buffer = msg};
    wrt.write(static_cast<char>(MessageType::RSP_PLAY_GAME));
    wrt.write(htonl(sessionIdHost));
    bool success = sendWithRetry(msg, *sa);
    if(!success)
    {
        log(std::cerr,
            std::format("[Server] Unable to send back RSP_PLAY_GAME to client {}",sessionIdHost));
    }

    LOCK_broadcastScoreboard();
}

// ============================================================
// REQ_QUIT_GAME
// ============================================================

void Server::handle_reqQuitGame(std::span<const char> udpPacketWithoutMID, sockaddr_in* sa)
{
    assert((udpPacketWithoutMID.size() == PacketSize::REQ_QUIT_GAME - 1) &&
        "Size of REQ_QUIT_GAME packet received is wrong");

    ByteReader rdr{udpPacketWithoutMID};
    SessionId sessionIdHost = ntohl(rdr.read<SessionId>());

    // If it finds the id inside the map,
    // it will set inGame to false, and return true
    // 
    // it will also call NO_LOCK_advanceDrawer
    // 
    // else it will return false, and we will early exit
    if (!LOCK_gameVariablesANDclientStorage(
        [sessionIdHost,this](auto& map, const auto& gameRunning, const auto& drawerSessionOpt) {
        auto it = map.find(sessionIdHost);
        if (it == map.end()) return false;
        it->second.inGame = false;
        it->second.score = 0;
        it->second.currentStrokeId = std::nullopt;

        if (gameRunning)
        {
            NO_LOCK_advanceDrawer();
            NO_LOCK_broadcastScoreboard();
        }
        return true;
        })) return;


    // send back ack
    std::array<char, PacketSize::RSP_PLAY_GAME> msg;
    ByteWriterN wrt{.buffer = msg};
    wrt.write(static_cast<char>(MessageType::RSP_QUIT_GAME));
    wrt.write(htonl(sessionIdHost));
    bool success = sendWithRetry(msg, *sa);
    if(!success)
    {
        log(std::cerr,
            std::format("[Server] Unable to send back RSP_QUIT_GAME to client {}", sessionIdHost));
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

    log(std::cout, std::format("[Server] Client: {} disconnected", ipStrAndPort));
    return;
}

// ============================================================
// NTF_RCV_CLEAR_CANVAS
// ============================================================

void Server::handle_ntfRcvClearCanvas(std::span<const char> udpPacketWithoutMID, sockaddr_in* sa)
{
    assert((udpPacketWithoutMID.size() == PacketSize::NTF_RCV_CLEAR_CANVAS - 1) &&
        "Size of NTF_RCV_CLEAR_CANVAS packet received is wrong");
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
// NTF_RCV_RET
// ============================================================

void Server::handle_ntfRcvRET(std::span<const char> udpPacketWithoutMID, sockaddr_in* sa)
{
    assert((udpPacketWithoutMID.size() == PacketSize::NTF_RCV_ROUND_END_TIME - 1) &&
        "Size of NTF_RCV_ROUND_END_TIME packet received is wrong");
    ByteReader rdr{.buffer = udpPacketWithoutMID};
    auto sessionIdHostOrder = ntohl(rdr.read<SessionId>());
    auto retIdHostOrder = ntohl(rdr.read<std::uint32_t>());

    std::lock_guard lock(_pendingNtfRETMutex);
    _pendingNtfRET.erase(NtfKey{sessionIdHostOrder, retIdHostOrder});
}

void Server::handle_ntfRcvSendWordLen(std::span<const char> udpPacketWithoutMID, sockaddr_in* sa)
{
    assert((udpPacketWithoutMID.size() == PacketSize::NTF_RCV_SEND_WORD_LEN - 1) &&
        "Size of NTF_RCV_SEND_WORD_LEN packet received is wrong");
    ByteReader rdr{ .buffer = udpPacketWithoutMID };
    auto sessionIdHostOrder = ntohl(rdr.read<SessionId>());
    auto retIdHostOrder = ntohl(rdr.read<std::uint32_t>());

    std::lock_guard lock(_pendingNtfNewWordLenMutex);
    _pendingNtfNewWordsLen.erase(NtfKey{ sessionIdHostOrder, retIdHostOrder });
}

void Server::handle_ntfRcvSendWord(std::span<const char> udpPacketWithoutMID, sockaddr_in* sa)
{
    assert((udpPacketWithoutMID.size() == PacketSize::NTF_RCV_SEND_WORD - 1) &&
        "Size of NTF_RCV_SEND_WORD packet received is wrong");
    ByteReader rdr{ .buffer = udpPacketWithoutMID };
    auto sessionIdHostOrder = ntohl(rdr.read<SessionId>());
    auto retIdHostOrder = ntohl(rdr.read<std::uint32_t>());

    std::lock_guard lock(_pendingNtfNewWordMutex);
    _pendingNtfNewWords.erase(NtfKey{ sessionIdHostOrder, retIdHostOrder });
}

// ============================================================
// REQ_START_STROKE
// ============================================================

void Server::handle_reqStartStroke(std::span<const char> udpPacketWithoutMID, sockaddr_in* sa)
{
    // SIDE NOTE: This function may be safe from deadlocks but it may still introduce
    // visual glitches
    // Because the lock is not 

    assert((udpPacketWithoutMID.size() == PacketSize::REQ_START_STROKE - 1) &&
        "Size of REQ_START_STROKE packet received is wrong");
    ByteReader rdr{ .buffer = udpPacketWithoutMID };
    auto sessionIdHostOrder = ntohl(rdr.read<SessionId>());
    auto strokeIdHostOrder = ntohl(rdr.read<std::uint32_t>());

    // =======================================================================
    // Lambda does the following
    // Return error if
    //      - Cant find session id in map
    //      - Client not in game
    //      - Client in game but has active stroke
    //      - Game not running
    //      - No Drawer
    //      - Has drawer but session id not the drawer
    //
    // If ret is error, early exit will happen
    //
    // If no error, will set the currentStrokeId to strokeIdHostOrder
    // =======================================================================
    enum class ErrorRetVal { OK, CANT_FIND, NOT_IN_GAME, ALREADY_HAS_STROKE, GAME_NOT_RUNNING, NO_DRAWER, NOT_DRAWER };
    auto erv = LOCK_gameVariablesANDclientStorage([sessionIdHostOrder, strokeIdHostOrder](auto& map, const auto& gameRunning, auto& drawerSessionIdOpt) {
        if (!gameRunning) return ErrorRetVal::GAME_NOT_RUNNING;
        auto it = map.find(sessionIdHostOrder);
        if (it == map.end()) return ErrorRetVal::CANT_FIND;
        else if (!it->second.inGame) return ErrorRetVal::NOT_IN_GAME;
        else if (it->second.currentStrokeId.has_value()) return ErrorRetVal::ALREADY_HAS_STROKE;
        else if (!drawerSessionIdOpt.has_value()) return ErrorRetVal::NO_DRAWER;
        else if (*drawerSessionIdOpt != sessionIdHostOrder) return ErrorRetVal::NOT_DRAWER;

        it->second.currentStrokeId = strokeIdHostOrder;
        return ErrorRetVal::OK;
        });

    if (erv != ErrorRetVal::OK)
    {
        std::string errmsg;
        switch (erv)
        {
            using enum ErrorRetVal;
        case CANT_FIND: errmsg = std::format("cant find session id in server: session {}", sessionIdHostOrder); break;
        case NOT_IN_GAME: errmsg = std::format("client not in game: session {}", sessionIdHostOrder); break;
        case ALREADY_HAS_STROKE: errmsg = std::format("client already has active stroke: session {}", sessionIdHostOrder); break;
        case GAME_NOT_RUNNING: errmsg = std::format("game is not running: session {}", sessionIdHostOrder); break;
        case NO_DRAWER: errmsg = std::format("no drawer active: session {}", sessionIdHostOrder); break;
        case NOT_DRAWER: errmsg = std::format("client is not the drawer active: session {}", sessionIdHostOrder); break;
        default: assert(false && "Missing switch case");
        }
        log(std::cerr,
            std::format("[Server] Received REQ_START_STROKE but {}", errmsg));
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
    ByteReader rdr{ .buffer = udpPacketWithoutMID };
    auto sessionIdHostOrder = ntohl(rdr.read<SessionId>());
    auto strokeIdHostOrder = ntohl(rdr.read<std::uint32_t>());

    // =======================================================================
    // Lambda does the following
    // Return error if
    //      - Cant find session id in map
    //      - Client not in game
    //      - Client in game but has no active stroke
    //      - Game not running
    //      - No Drawer
    //      - Has drawer but session id not the drawer
    //
    // If ret is error, early exit will happen
    // 
    // If no error, then will set client's stroke id to std::nullopt
    // =======================================================================
    enum class ErrorRetVal { OK, CANT_FIND, NOT_IN_GAME, NO_STROKE, GAME_NOT_RUNNING, NO_DRAWER, NOT_DRAWER };
    auto erv = LOCK_gameVariablesANDclientStorage([sessionIdHostOrder, strokeIdHostOrder](auto& map, const auto& gameRunning, auto& drawerSessionIdOpt) {
        if (!gameRunning) return ErrorRetVal::GAME_NOT_RUNNING;
        auto it = map.find(sessionIdHostOrder);
        if (it == map.end()) return ErrorRetVal::CANT_FIND;
        else if (!it->second.inGame) return ErrorRetVal::NOT_IN_GAME;
        else if (!it->second.currentStrokeId.has_value()) return ErrorRetVal::NO_STROKE;
        else if (!drawerSessionIdOpt.has_value()) return ErrorRetVal::NO_DRAWER;
        else if (*drawerSessionIdOpt != sessionIdHostOrder) return ErrorRetVal::NOT_DRAWER;

        it->second.currentStrokeId = std::nullopt;
        return ErrorRetVal::OK;
        });

    if (erv != ErrorRetVal::OK)
    {
        std::string errmsg;
        switch (erv)
        {
            using enum ErrorRetVal;
        case CANT_FIND: errmsg = std::format("cant find session id in server: session {}", sessionIdHostOrder); break;
        case NOT_IN_GAME: errmsg = std::format("client not in game: session {}", sessionIdHostOrder); break;
        case NO_STROKE: errmsg = std::format("client has no active stroke: session {}", sessionIdHostOrder); break;
        case GAME_NOT_RUNNING: errmsg = std::format("game is not running: session {}", sessionIdHostOrder); break;
        case NO_DRAWER: errmsg = std::format("no drawer active: session {}", sessionIdHostOrder); break;
        case NOT_DRAWER: errmsg = std::format("client is not the drawer active: session {}", sessionIdHostOrder); break;
        default: assert(false && "Missing switch case");
        }
        log(std::cerr,
            std::format("[Server] Received REQ_END_STROKE but {}", errmsg));
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
    auto msgIdHostOrder = ntohl(rdr.read<std::uint32_t>());
    auto msgLength = rdr.read<std::uint8_t>();
    auto actualmsg = rdr.readBytes(msgLength);

    // =======================================================================
    // Lambda does the following
    // Return error if
    //      - Cant find session id in map
    //      - Client not in game
    //      - Game not running
    //
    // If ret is error, early exit will happen
    // 
    // If no error, then will set username to client's username
    // and call NON_LOCK_BROADCAST
    // =======================================================================
    std::string username;
    enum class ErrorRetVal { OK_BUT_NOT_GUESSED_WORD,OK_AND_GUESSED_WORD, CANT_FIND, NOT_IN_GAME, GAME_NOT_RUNNING };
    auto erv = LOCK_gameVariablesANDclientStorage([sessionIdHostOrder, &username,&actualmsg,this](auto& map, const auto& gameRunning, auto& drawerSessionIdOpt) {
        if (!gameRunning) return ErrorRetVal::GAME_NOT_RUNNING;
        auto it = map.find(sessionIdHostOrder);
        if (it == map.end()) return ErrorRetVal::CANT_FIND;
        else if (!it->second.inGame) return ErrorRetVal::NOT_IN_GAME;
        
        username = it->second.username;

        std::string str_msg = std::string(actualmsg.begin(), actualmsg.end()).c_str();
        std::transform(str_msg.begin(), str_msg.end(), str_msg.begin(), [](char c) {return std::toupper(c); });
        std::transform(word.second.begin(), word.second.end(), word.second.begin(), [](char c) {return std::toupper(c); });

        if (str_msg == word.second) {
            it->second.score += 75;
            NO_LOCK_broadcastScoreboard();
            return ErrorRetVal::OK_AND_GUESSED_WORD;
        }
        return ErrorRetVal::OK_BUT_NOT_GUESSED_WORD;
        });

    if (erv != ErrorRetVal::OK_BUT_NOT_GUESSED_WORD && erv != ErrorRetVal::OK_AND_GUESSED_WORD)
    {
        std::string errmsg;
        switch (erv)
        {
            using enum ErrorRetVal;
        case CANT_FIND: errmsg = std::format("cant find session id in server: session {}", sessionIdHostOrder); break;
        case NOT_IN_GAME: errmsg = std::format("client not in game: session {}", sessionIdHostOrder); break;
        case GAME_NOT_RUNNING: errmsg = std::format("game is not running: session {}", sessionIdHostOrder); break;
        default: assert(false && "Missing switch case");
        }
        log(std::cerr,
            std::format("[Server] Received REQ_MSG but {}", errmsg));
        return;
    }

    // CHECK IF ITS NOT CURRENT DRAWER + IF ACTUAL MSG IS THE GUESS, THEN SEND BACK
    // "USER GUESSED THE WORD" @TODOOOOOOOOOOOOOOOOOOOOOOOOOOOOOOOOOOOOOOOOOOOOOOOOOOOOOOOOOOOOOOOOOOOOOOOO



    // validated its good REQ_MSG packet, send back RSP_MSG
    std::array<char, PacketSize::RSP_MSG> rspmsg;
    ByteWriterN rspwrt{ .buffer = rspmsg };
    rspwrt.write(static_cast<char>(MessageType::RSP_MSG));
    rspwrt.write(htonl(sessionIdHostOrder));
    rspwrt.write(htonl(msgIdHostOrder));
    bool successRsp = sendWithRetry(rspmsg, *sa);

    auto nameLength = static_cast<std::uint8_t>(username.length());
    if (nameLength > 15) // 15 is arbitrary here
    {
        username = username.substr(0, 12); // trunc it
        username += "...";
        nameLength = 15;
    }

    // Now NTF all clients for msg
    std::lock_guard ntfLock(_pendingNtfMsgMutex);
    LOCK_clientStorage([this, msgLength, nameLength,&actualmsg,&username](const auto& map)
        {
            auto now = std::chrono::steady_clock::now();
            for (const auto& [ssiho, client] : map)
            {
                if (!client.inGame) continue;
                std::vector<char> svrmsg;
                svrmsg.resize(PacketSize::NTF_MSG_WITHOUT_BUFFER + msgLength + nameLength);
                ByteWriter svrwrt{ .buffer = svrmsg };
                svrwrt.write(static_cast<char>(MessageType::NTF_MSG));
                svrwrt.write(htonl(ssiho));
                svrwrt.write(htonl(_messageIdServer));
                svrwrt.write(msgLength);
                svrwrt.writeSpan(actualmsg);
                svrwrt.write(nameLength);
                svrwrt.writeSpan(username);

                // Send immediately once
                sockaddr_in clientSa = client.sa;
                sendto(_socket, svrmsg.data(), static_cast<int>(svrmsg.size()), 0,
                    reinterpret_cast<sockaddr*>(&clientSa), sizeof(clientSa));

                // Add to pending for retry
                _pendingNtfMsg[NtfKey{ ssiho, _messageIdServer }] = PendingNTF{
                    ._data = std::move(svrmsg),
                    ._clientAddr = client.sa,
                    ._targetSessionId = ssiho,
                    ._ntfId = _messageIdServer,
                    ._nextSendTime = now + std::chrono::milliseconds(100),
                    ._giveUpTime = now + std::chrono::seconds(2),
                };
            }
            _messageIdServer++;
        });
}

// ============================================================
// REQ_CLEAR_CANVAS
// ============================================================

void Server::handle_reqClearCanvas(std::span<const char> udpPacketWithoutMID, sockaddr_in* sa)
{
    assert(udpPacketWithoutMID.size() == PacketSize::REQ_CLEAR_CANVAS - 1);

    ByteReader rdr{ .buffer = udpPacketWithoutMID };
    auto sessionIdHostOrder = ntohl(rdr.read<SessionId>());
    auto clearIdHostOrder = ntohl(rdr.read<std::uint32_t>());

    // =======================================================================
    // Lambda does the following
    // Return error if
    //      - Cant find session id in map
    //      - Client not in game
    //      - Game not running
    //      - No drawer active
    //      - Has drawer but session id not the drawer < Important because we should not allow any client to just clear canvas unless they are the drawer
    //
    // If ret is error, early exit will happen
    // 
    // If no error, then will set client's stroke id to std::nullopt
    // =======================================================================
    enum class ErrorRetVal { OK, CANT_FIND, NOT_IN_GAME, GAME_NOT_RUNNING, NO_DRAWER, NOT_DRAWER };
    auto erv = LOCK_gameVariablesANDclientStorage([sessionIdHostOrder](auto& map, const auto& gameRunning, auto& drawerSessionIdOpt) {
        if (!gameRunning) return ErrorRetVal::GAME_NOT_RUNNING;
        auto it = map.find(sessionIdHostOrder);
        if (it == map.end()) return ErrorRetVal::CANT_FIND;
        else if (!it->second.inGame) return ErrorRetVal::NOT_IN_GAME;
        else if (!drawerSessionIdOpt.has_value()) return ErrorRetVal::NO_DRAWER;
        else if (*drawerSessionIdOpt != sessionIdHostOrder) return ErrorRetVal::NOT_DRAWER;

        it->second.currentStrokeId = std::nullopt;
        return ErrorRetVal::OK;
        });

    if (erv != ErrorRetVal::OK)
    {
        std::string errmsg;
        switch (erv)
        {
            using enum ErrorRetVal;
        case CANT_FIND: errmsg = std::format("cant find session id in server: session {}", sessionIdHostOrder); break;
        case NOT_IN_GAME: errmsg = std::format("client not in game: session {}", sessionIdHostOrder); break;
        case GAME_NOT_RUNNING: errmsg = std::format("game is not running: session {}", sessionIdHostOrder); break;
        case NO_DRAWER: errmsg = std::format("no drawer active: session {}", sessionIdHostOrder); break;
        case NOT_DRAWER: errmsg = std::format("client is not the drawer active: session {}", sessionIdHostOrder); break;
        default: assert(false && "Missing switch case");
        }
        log(std::cerr,
            std::format("[Server] Received REQ_CLEAR_CANVAS but {}", errmsg));
        return;
    }

    // Send RSP_CLEAR_CANVAS back to the requester
    std::array<char, PacketSize::RSP_CLEAR_CANVAS> rspmsg;
    ByteWriterN rspwrt{ .buffer = rspmsg };
    rspwrt.write(static_cast<char>(MessageType::RSP_CLEAR_CANVAS));
    rspwrt.write(htonl(sessionIdHostOrder));
    rspwrt.write(htonl(clearIdHostOrder));

    bool success = sendWithRetry(rspmsg, *sa);

    // if not able to send REQ_CLEAR_CANVAS, just return, its ok. wtv
    if (!success)
    {
        log(std::cerr,
            std::format("[Server] Unable to send REQ_CLEAR_CANVAS for client session id {}", sessionIdHostOrder));
        return;
    }

    // Now NTF all clients to clear their canvas
    sendClearCanvasCommand();
}

// ============================================================
// FAF_EXTEND_STROKE
// ============================================================

void Server::handle_fafExtendStroke(std::span<const char> udpPacketWithoutMID, sockaddr_in* sa)
{
    assert((udpPacketWithoutMID.size() == PacketSize::FAF_EXTEND_STROKE - 1) &&
        "Size of FAF_EXTEND_STROKE packet received is wrong");
    ByteReader rdr{ .buffer = udpPacketWithoutMID };
    auto sessionIdHostOrder = ntohl(rdr.read<SessionId>());
    auto strokeIdHostOrder = ntohl(rdr.read<std::uint32_t>());
    auto mousePosNetworkOrder = rdr.read<MousePosition>();

    // =======================================================================
    // Lambda does the following
    // Return error if
    //      - Cant find session id in map
    //      - Client not in game
    //      - Game not running
    //      - No drawer active
    //      - Has drawer but session id not the drawer
    //
    // If ret is error, early exit will happen
    // =======================================================================
    enum class ErrorRetVal { OK, CANT_FIND, NOT_IN_GAME, GAME_NOT_RUNNING, NO_DRAWER, NOT_DRAWER };
    auto erv = LOCK_gameVariablesANDclientStorage([sessionIdHostOrder](auto& map, const auto& gameRunning, auto& drawerSessionIdOpt) {
        if (!gameRunning) return ErrorRetVal::GAME_NOT_RUNNING;
        auto it = map.find(sessionIdHostOrder);
        if (it == map.end()) return ErrorRetVal::CANT_FIND;
        else if (!it->second.inGame) return ErrorRetVal::NOT_IN_GAME;
        else if (!drawerSessionIdOpt.has_value()) return ErrorRetVal::NO_DRAWER;
        else if (*drawerSessionIdOpt != sessionIdHostOrder) return ErrorRetVal::NOT_DRAWER;

        return ErrorRetVal::OK;
        });

    if (erv != ErrorRetVal::OK)
    {
        std::string errmsg;
        switch (erv)
        {
            using enum ErrorRetVal;
        case CANT_FIND: errmsg = std::format("cant find session id in server: session {}", sessionIdHostOrder); break;
        case NOT_IN_GAME: errmsg = std::format("client not in game: session {}", sessionIdHostOrder); break;
        case GAME_NOT_RUNNING: errmsg = std::format("game is not running: session {}", sessionIdHostOrder); break;
        case NO_DRAWER: errmsg = std::format("no drawer active: session {}", sessionIdHostOrder); break;
        case NOT_DRAWER: errmsg = std::format("client is not the drawer active: session {}", sessionIdHostOrder); break;
        default: assert(false && "Missing switch case");
        }
        log(std::cerr,
            std::format("[Server] Received REQ_CLEAR_CANVAS but {}", errmsg));
        return;
    }

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
        tickPendingNtf(_pendingNtfRETMutex, _pendingNtfRET, "NTF_ROUND_END_TIME");

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
    // =======================================================================
    // Lambda does the following
    // Return error if
    //      - Cant find session id in map
    //      - Client ip port not equal to ipPort
    //
    // If ret is error, function will return false
    // 
    // If no error, will always remove client from server's map
    // 
    // If no error AND game is running AND fella is in game AND there is a drawer AND session that got destroyed is the current drawer
    //      - advance drawer and broadcastScoreboard will happen
    // =======================================================================
    enum class ErrorRetVal { OK, CANT_FIND, IP_PORT_MISMATCH };
    auto erv = LOCK_gameVariablesANDclientStorage([id,&ipPort,this](auto& map, const auto& gameRunning, auto& drawerSessionIdOpt) {
        auto it = map.find(id);
        if (it == map.end()) return ErrorRetVal::CANT_FIND;
        else if (ipPort != it->second.ipPort) return ErrorRetVal::IP_PORT_MISMATCH;

        if ((gameRunning) &&
            (it->second.inGame) &&
            (drawerSessionIdOpt.has_value()) &&
            (*drawerSessionIdOpt == id))
        {
            NO_LOCK_advanceDrawer();
            NO_LOCK_broadcastScoreboard();
        }
        map.erase(it);
        return ErrorRetVal::OK;
        });

    if (erv != ErrorRetVal::OK)
    {
        std::string errmsg;
        switch (erv)
        {
            using enum ErrorRetVal;
        case CANT_FIND: errmsg = std::format("cant find session id in server: session {}", id); break;
        case IP_PORT_MISMATCH: errmsg = std::format("client ip and stored ip in server mismatched: session {}", id); break;
        default: assert(false && "Missing switch case");
        }
        log(std::cerr,
            std::format("[Server] Tried to disconnect client but {}", errmsg));
        return false;
    }
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
// Advance drawer - LOCK
// ============================================================

void Server::LOCK_advanceDrawer()
{
    LOCK_gameVariablesANDclientStorage([this](auto& map, const auto& gameRunning, auto& drawerSessionIdOpt) {
        NO_LOCK_advanceDrawer();
        });
}

// ============================================================
// Advance drawer - NO LOCK
// ============================================================

void Server::NO_LOCK_advanceDrawer()
{
    if (!_gameRunning)
    {
        log(std::cerr,
            std::format("[Server] Tried to advance drawer, but game is not started"));
        return;
    }
    if (_clientStorageMap.empty())
    {
        log(std::cerr,
            std::format("[Server] Tried to advance drawer, but server has no clients to choose from"));
        //stopGame();
        return;
    }
    if (_clientStorageMap.size() == 1 && _drawerSessionId.has_value())
    {
        log(std::cerr,
            std::format("[Server] Tried to advance drawer, but no drawers to choose from, sticking with current player"));
        return;
    }

    // helper: find the next in-game player starting from 'start', wrapping around
    auto findNextInGame = [&](std::map<SessionId, Client>::iterator start) -> std::optional<SessionId>
        {
            auto it = start;
            auto startPoint = it;
            bool wrapped = false;

            while (true)
            {
                if (it == _clientStorageMap.end())
                {
                    it = _clientStorageMap.begin();
                    wrapped = true;
                }
                if (wrapped && it == startPoint)
                    break; // looped all the way around, no one found

                if (it->second.inGame)
                    return it->first;

                ++it;
            }
            return std::nullopt;
        };

    if (!_drawerSessionId.has_value())
    {
        _drawerSessionId = findNextInGame(_clientStorageMap.begin());
        return;
    }

    auto it = _clientStorageMap.find(*_drawerSessionId);
    if (it == _clientStorageMap.end())
    {
        // current drawer no longer exists, pick first available
        _drawerSessionId = findNextInGame(_clientStorageMap.begin());
        return;
    }

    // advance past current drawer
    ++it;
    _drawerSessionId = findNextInGame(it);
}

// ============================================================
// Broadcast Scoreboard - LOCK
// ============================================================

void Server::LOCK_broadcastScoreboard()
{
    LOCK_gameVariablesANDclientStorage([this](const auto& map, const auto& gameRunning, const auto& drawerSessionIdOpt)
        {
            NO_LOCK_broadcastScoreboard();
        });
}

// ============================================================
// Broadcast Scoreboard - NO LOCK
// ============================================================

void Server::NO_LOCK_broadcastScoreboard()
{
    // < this by the way. should be redesigned. @WILLIAM. Broadcasting scoreboard shouldn't matter if got drawer or not
    if (!_drawerSessionId.has_value())
    {
        log(std::cerr,
            "[Server] Broadcast scoreboard requested but no current drawer");
        return;
    }
    else if (!_gameRunning)
    {
        log(std::cerr,
            "[Server] Broadcast scoreboard requested but game is not running");
        return;
    }
    else if (_clientStorageMap.empty())
    {
        log(std::cerr,
            "[Server] Broadcast scoreboard requested but server has no clients to broadcast to");
        return;
    }

    // < this by the way. should be redesigned. @WILLIAM. Broadcasting scoreboard shouldn't matter if got drawer or not
    auto it = _clientStorageMap.find(*_drawerSessionId);
    if (it == _clientStorageMap.end())
    {
        log(std::cerr,
            "[Server] Broadcast scoreboard requested but can't find drawer id");
        return;
    }

    // Pre-build the per-player payload once (shared across all clients)
    // Each entry: 1 byte name_len + name bytes + 2 bytes score
    struct PlayerEntry { std::string name; std::uint16_t score; };
    std::vector<PlayerEntry> entries;

    for (const auto& [sid, client] : _clientStorageMap)
    {
        if (!client.inGame) continue;
        std::string name = client.username;
        if (name.size() > 15)
            name = name.substr(0, 12) + "...";

        entries.push_back({ std::move(name), client.score });
    }

    // Calculate variable payload size
    std::size_t varSize = 0;
    for (const auto& e : entries)
        varSize += 1 + e.name.size() + 2; // name_len + name + score

    auto numPlayers = static_cast<std::uint32_t>(entries.size());

    auto now = std::chrono::steady_clock::now();
    std::lock_guard ntfLock(_pendingNtfUpdateScoreboardMutex);

    const std::string& drawer = it->second.username;

    for (const auto& [ssiho, client] : _clientStorageMap)
    {
        if (!client.inGame) continue;
        std::vector<char> pkt(PacketSize::NTF_UPDATE_SCOREBOARD_BASE + varSize + drawer.size());
        ByteWriter wrt{ .buffer = pkt };
        wrt.write(static_cast<char>(MessageType::NTF_UPDATE_SCOREBOARD));
        wrt.write(htonl(ssiho));
        wrt.write(htonl(_scoreBoardIdServer));
        wrt.write(htonl(numPlayers));

        // Write each player's name + score
        for (const auto& e : entries)
        {
            auto nameLen = static_cast<std::uint8_t>(e.name.size());
            wrt.write(nameLen);
            wrt.writeSpan(std::span<const char>(e.name.data(), nameLen));
            wrt.write(htons(e.score));
        }

        auto drawerLen = static_cast<std::uint8_t>(drawer.size());
        wrt.write(drawerLen);
        wrt.writeSpan(std::span<const char>(drawer.data(), drawerLen));

        sockaddr_in clientSa = client.sa;
        sendto(_socket, pkt.data(), static_cast<int>(pkt.size()), 0,
            reinterpret_cast<sockaddr*>(&clientSa), sizeof(clientSa));

        _pendingNtfUpdateScoreboards[NtfKey{ ssiho, _scoreBoardIdServer }] = PendingNTF{
            ._data = std::move(pkt),
            ._clientAddr = client.sa,
            ._targetSessionId = ssiho,
            ._ntfId = _scoreBoardIdServer,
            ._nextSendTime = now + std::chrono::milliseconds(100),
            ._giveUpTime = now + std::chrono::seconds(2),
        };
    }
    _scoreBoardIdServer++;
}


// ============================================================
// Start game
// ============================================================

void Server::startGame()
{
    LOCK_gameVariablesANDclientStorage([this](const auto& map, auto& gameRunning, const auto& _ignore) {
        if (gameRunning)
        {
            log(std::cerr, "[Server] Game has already started");
            return;
        }
        if (map.empty())
        {
            log(std::cerr, "[Server] Game can't start with no players");
            return;
        }
        gameRunning = true;
        NO_LOCK_advanceDrawer();
        NO_LOCK_broadcastScoreboard();
        });
}

// ============================================================
// Stop game
// ============================================================

void Server::stopGame()
{
    LOCK_gameVariables([](auto& gameRunning, auto& drawerSessionIdOpt)
        {
            gameRunning = false;
            drawerSessionIdOpt = std::nullopt;
            log(std::cout, "[Server] Game stopped");
        });
}

// ============================================================
// Game Started
// ============================================================

bool Server::gameStarted()
{
    std::lock_guard lock(_gameMutex);
    return _gameRunning;
}

// ============================================================
// Reset Round
// ============================================================

void Server::resetRound(std::int64_t newEpoch)
{
    // local server stuff

    LOCK_gameVariablesANDclientStorage([this](auto&, auto&, auto&) {
        NO_LOCK_advanceDrawer();
        pick_word();
        NO_LOCK_broadcastScoreboard();
        });

    // send client stuff
    sendNewWord();
    sendNewWordLen();
    sendNewRoundEndTime(newEpoch);
    sendClearCanvasCommand();

}

// ============================================================
// Get number of players
// ============================================================

std::size_t Server::getNumberOfPlayers()
{
    std::lock_guard lock(_clientStorageMutex);
    return std::ranges::count_if(_clientStorageMap, [](std::pair<const SessionId&, const Client&> p){
        return p.second.inGame;
        });
}

// ============================================================
// Send new round end time
// ============================================================

void Server::sendNewRoundEndTime(std::int64_t time)
{
    // broadcast to all clients ntf
    std::lock_guard ntfLock(_pendingNtfRETMutex);
    LOCK_clientStorage([&time,this](const auto& map) {
        time = time < std::int64_t{0} ? 0 : time;
        auto now = std::chrono::steady_clock::now();
        for(const auto& [ssiho, client] : map)
        {
            if(!client.inGame) continue;
            std::vector<char> msg;
            msg.resize(PacketSize::NTF_ROUND_END_TIME);
            ByteWriter wrt{.buffer = msg};
            wrt.write(static_cast<char>(MessageType::NTF_ROUND_END_TIME));
            wrt.write(htonl(ssiho));
            wrt.write(htonl(_roundEndTimeIdServer));
            wrt.write(htonll(static_cast<std::uint64_t>(time)));

            // Send immediately once
            sockaddr_in clientSa = client.sa;
            sendto(_socket, msg.data(), static_cast<int>(msg.size()), 0,
                reinterpret_cast<sockaddr*>(&clientSa), sizeof(clientSa));

            // Add to pending for retry
            _pendingNtfRET[NtfKey{ssiho, _roundEndTimeIdServer}] = PendingNTF{
                ._data = std::move(msg),
                ._clientAddr = client.sa,
                ._targetSessionId = ssiho,
                ._ntfId = _roundEndTimeIdServer,
                ._nextSendTime = now + std::chrono::milliseconds(100),
                ._giveUpTime = now + std::chrono::seconds(2),
            };
        }
        _roundEndTimeIdServer++;
    });
}

// ============================================================
// Send clear canvas command
// ============================================================

void Server::sendClearCanvasCommand()
{
    std::lock_guard ntfLock(_pendingNtfClearCanvasMutex);

    LOCK_clientStorage([this](const auto& map) {
        auto now = std::chrono::steady_clock::now();
        for (const auto& [ssiho, client] : map)
        {
            if (!client.inGame)continue;
            std::vector<char> ntfPkt(PacketSize::NTF_CLEAR_CANVAS);
            ByteWriter ntfWrt{ .buffer = ntfPkt };
            ntfWrt.write(static_cast<char>(MessageType::NTF_CLEAR_CANVAS));
            ntfWrt.write(htonl(ssiho));       // target's session ID
            ntfWrt.write(htonl(_clearCanvasIdServer));

            // Send immediately once
            sockaddr_in clientSa = client.sa;
            sendto(_socket, ntfPkt.data(), static_cast<int>(ntfPkt.size()), 0,
                reinterpret_cast<sockaddr*>(&clientSa), sizeof(clientSa));

            // Add to pending for retry
            _pendingNtfClearCanvases[NtfKey{ ssiho, _clearCanvasIdServer }] = PendingNTF{
                ._data = std::move(ntfPkt),
                ._clientAddr = client.sa,
                ._targetSessionId = ssiho,
                ._ntfId = _clearCanvasIdServer,
                ._nextSendTime = now + std::chrono::milliseconds(100),
                ._giveUpTime = now + std::chrono::seconds(2),
            };
        }
        _clearCanvasIdServer++;
        });
}

void Server::sendNewWordLen()
{
    std::lock_guard ntfLock(_pendingNtfNewWordLenMutex);

    LOCK_clientStorage([this](const auto& map) {
        auto now = std::chrono::steady_clock::now();
        for (const auto& [ssiho, client] : map)
        {
            if (!client.inGame) continue;
            std::vector<char> ntfPkt(PacketSize::NTF_SEND_WORD_LEN);
            ByteWriter ntfWrt{ .buffer = ntfPkt };
            ntfWrt.write(static_cast<char>(MessageType::NTF_SEND_WORD_LEN));
            ntfWrt.write(htonl(ssiho));       // target's session ID
            ntfWrt.write(htonl(_sendNewWordLenIdServer));
            ntfWrt.write(static_cast<std::uint8_t>(word.second.size()));

            // Send immediately once
            sockaddr_in clientSa = client.sa;
            sendto(_socket, ntfPkt.data(), static_cast<int>(ntfPkt.size()), 0,
                reinterpret_cast<sockaddr*>(&clientSa), sizeof(clientSa));

            // Add to pending for retry
            _pendingNtfNewWordsLen[NtfKey{ ssiho, _sendNewWordLenIdServer }] = PendingNTF{
                ._data = std::move(ntfPkt),
                ._clientAddr = client.sa,
                ._targetSessionId = ssiho,
                ._ntfId = _sendNewWordLenIdServer,
                ._nextSendTime = now + std::chrono::milliseconds(100),
                ._giveUpTime = now + std::chrono::seconds(2),
            };
        }
        _sendNewWordLenIdServer++;
        });
}


void Server::sendNewWord()
{
    LOCK_gameVariablesANDclientStorage([this](const auto& map, const auto&, const auto& drawerSesssionIdOpt)
        {
            if (!drawerSesssionIdOpt.has_value())
            {
                log(std::cerr,
                    "[Server] Tried to send new word but no current drawer available");
                return;
            }
            
            auto it = map.find(*drawerSesssionIdOpt);
            if (it == map.end())
            {
                log(std::cerr,
                    "[Server] Tried to send new word but can't find drawer in server");
                return;
            }

            auto now = std::chrono::steady_clock::now();
            std::lock_guard ntfLock(_pendingNtfNewWordMutex);
            SessionId const& ssiho = it->first;
            Server::Client const& client = it->second;

            std::vector<char> ntfPkt(PacketSize::NTF_SEND_WORD + word.second.size());
            ByteWriter ntfWrt{ .buffer = ntfPkt };
            ntfWrt.write(static_cast<char>(MessageType::NTF_SEND_WORD));
            ntfWrt.write(htonl(ssiho));       // target's session ID
            ntfWrt.write(htonl(_sendNewWordIdServer));
            ntfWrt.write(static_cast<std::uint8_t>(word.second.size()));
            ntfWrt.writeSpan(word.second);

            // Send immediately once
            sockaddr_in clientSa = client.sa;
            sendto(_socket, ntfPkt.data(), static_cast<int>(ntfPkt.size()), 0,
                reinterpret_cast<sockaddr*>(&clientSa), sizeof(clientSa));

            // Add to pending for retry
            _pendingNtfNewWords[NtfKey{ ssiho, _sendNewWordIdServer }] = PendingNTF{
                ._data = std::move(ntfPkt),
                ._clientAddr = client.sa,
                ._targetSessionId = ssiho,
                ._ntfId = _sendNewWordIdServer,
                ._nextSendTime = now + std::chrono::milliseconds(100),
                ._giveUpTime = now + std::chrono::seconds(2),
            };
            _sendNewWordIdServer++;
    });
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