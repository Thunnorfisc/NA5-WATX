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
#include "main.hpp"
#include "shared_protocol.hpp"

#include <mutex>
#include <cmath>
#include <deque>
#include <chrono>
#include <string>
#include <format>
#include <ranges>
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
    for (const auto& [_ignore, client] : _clientStorageMap) _userStore.saveHighscore(client.username, client.highscore);
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
        std::uint16_t highScore = 0;
        auto highscoreOpt = _userStore.getHighscore(username);
        if (highscoreOpt) highScore = static_cast<std::uint16_t>(*highscoreOpt);

        Client newClient{ .ipPort = ipStrAndPort,
            .username = username, .sa = *sa,
        .highscore = highScore };
        
        bool succeedAddingNewClient = LOCK_clientStorage(
            [sessionIdHostOrder, client = std::move(newClient),this](auto& map) mutable 
            {
            auto [_ignore, succeed] = map.emplace(sessionIdHostOrder, std::move(client));
            NO_LOCK_sendLeaderboard();
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

    std::string username;
    std::optional<PlayGameStatus> pgs = LOCK_gameVariablesANDclientStorage([sessionIdHost,&username,this]
    (auto& map, const auto& gameRunning, auto& drawerSessionIdOpt) -> std::optional<PlayGameStatus>{
        auto it = map.find(sessionIdHost);
        if (it == map.end()) return std::nullopt;
        if (NO_LOCK_getNumberOfPlayers() >= MAX_PLAYERS_IN_GAME) return PlayGameStatus::TOO_MANY_PLAYERS;
        it->second.inGame = true;
        username = it->second.username;
        return PlayGameStatus::SUCCESS;
        });

    if (!pgs.has_value()) return; // no session id registered
    
    // send back ack
    std::array<char, PacketSize::RSP_PLAY_GAME> msg;
    ByteWriterN wrt{.buffer = msg};
    wrt.write(static_cast<char>(MessageType::RSP_PLAY_GAME));
    wrt.write(htonl(sessionIdHost));
    wrt.write(static_cast<char>(*pgs));
    {
        std::lock_guard lock(_gameMutex);
        wrt.write(rounds.first);
        wrt.write(rounds.second);
    }
    bool success = sendWithRetry(msg, *sa);
    if(!success)
    {
        log(std::cerr,
            std::format("[Server] Unable to send back RSP_PLAY_GAME to client {}",sessionIdHost));
    }
    if (pgs == PlayGameStatus::SUCCESS)
    {
    auto nameLength = static_cast<std::uint8_t>(username.length());
    if (nameLength > MAX_SHOWN_USERNAME_LEN)
    {
        username = username.substr(0, MAX_SHOWN_USERNAME_LEN - 3); // trunc it
        username += "...";
        nameLength = MAX_SHOWN_USERNAME_LEN;
    }
    LOCK_sendMessage("Server",std::format("{} has joined the lobby",username));

    LOCK_broadcastScoreboard();
    LOCK_sendNewWordLen();
    LOCK_sendNewRoundEndTime();
    LOCK_sendStrokeHistory();
    LOCK_sendMessageHistory();
    }

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
    std::string username;
    std::atomic_bool to_resetRound{};

    if (!LOCK_gameVariablesANDclientStorage(
        [sessionIdHost, &username, &to_resetRound, this](auto& map, const auto& gameRunning, const auto& drawerSessionOpt) {
            auto it = map.find(sessionIdHost);
            if (it == map.end()) return false;
            it->second.inGame = false;
            if (it->second.score > it->second.highscore)
            {
                it->second.highscore = it->second.score;
                _userStore.saveHighscore(it->second.username, it->second.highscore);
            }
            it->second.score = 0;
            it->second.currentStrokeId = std::nullopt;

            username = it->second.username;
            if (gameRunning && drawerSessionOpt && drawerSessionOpt == sessionIdHost)
            {
                _roundEndTime =
                    std::chrono::duration_cast<std::chrono::milliseconds>
                    (std::chrono::steady_clock::now().time_since_epoch()).count();
                _roundEndTime += (budgetAdvanceTurnInt * 1000);
                advanceTurnNow = std::chrono::steady_clock::now();

                NO_LOCK_advanceDrawer();
                NO_LOCK_forceEndStroke();
                pick_word();

                to_resetRound = true;
            }
            // update scoreboard no matter wat
            NO_LOCK_broadcastScoreboard();
            return true;
        }))return;

    if (to_resetRound) {
        // send client stuff
        LOCK_sendNewWord();
        LOCK_sendNewWordLen();
        LOCK_sendNewRoundEndTime();
        LOCK_sendClearCanvasCommand();
        to_resetRound = false;
    }
    
    LOCK_sendLeaderboard();

    // send back ack
    std::array<char, PacketSize::RSP_QUIT_GAME> msg;
    ByteWriterN wrt{.buffer = msg};
    wrt.write(static_cast<char>(MessageType::RSP_QUIT_GAME));
    wrt.write(htonl(sessionIdHost));
    bool success = sendWithRetry(msg, *sa);
    if(!success)
    {
        log(std::cerr,
            std::format("[Server] Unable to send back RSP_QUIT_GAME to client {}", sessionIdHost));
    }

    auto nameLength = static_cast<std::uint8_t>(username.length());
    if (nameLength > MAX_SHOWN_USERNAME_LEN)
    {
        username = username.substr(0, MAX_SHOWN_USERNAME_LEN - 3); // trunc it
        username += "...";
        nameLength = MAX_SHOWN_USERNAME_LEN;
    }
    LOCK_sendMessage("Server", std::format("{} has left the lobby",username));
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

void Server::handle_ntfRcvUpdateLeaderboard(std::span<const char> udpPacketWithoutMID, sockaddr_in* sa)
{
    assert((udpPacketWithoutMID.size() == PacketSize::NTF_RCV_LEADERBOARD - 1) &&
        "Size of NTF_RCV_LEADERBOARD packet received is wrong");
    ByteReader rdr{ .buffer = udpPacketWithoutMID };
    auto sessionIdHostOrder = ntohl(rdr.read<SessionId>());
    auto leaderboardIdHostOrder = ntohl(rdr.read<std::uint32_t>());

    std::lock_guard lock(_pendingNtfLeaderboardMutex);
    _pendingNtfLeaderboard.erase(NtfKey{ sessionIdHostOrder, leaderboardIdHostOrder });
}

// ============================================================
// NTF_RCV_ROUND_END_TIME
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

// ============================================================
// NTF_RCV_SEND_WORD_LEN
// ============================================================

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

// ============================================================
// NTF_RCV_SEND_WORD
// ============================================================

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
// NTF_RCV_STROKE_HISTORY
// ============================================================

void Server::handle_ntfRcvStrokeHistory(std::span<const char> udpPacketWithoutMID, sockaddr_in* sa)
{
    assert((udpPacketWithoutMID.size() == PacketSize::NTF_RCV_STROKE_HISTORY - 1) &&
        "Size of NTF_RCV_STROKE_HISTORY packet received is wrong");

    ByteReader rdr{ .buffer = udpPacketWithoutMID };
    auto sessionIdHostOrder = ntohl(rdr.read<SessionId>());
    auto historyIdHostOrder = ntohl(rdr.read<std::uint32_t>());
    auto chunkIdxHostOrder = ntohs(rdr.read<std::uint16_t>());

    std::lock_guard lock(_pendingNtfStrokeHistoryMutex);
    auto it = _pendingNtfStrokeHistory.find(NtfKey{ sessionIdHostOrder, historyIdHostOrder });
    if (it == _pendingNtfStrokeHistory.end()) return; // stale ack, ignore

    auto& p = it->second;

    // Ignore if not the chunk we're currently waiting on
    if (chunkIdxHostOrder != p._nextChunkToSend) return;

    // Advance
    p._nextChunkToSend++;

    // All chunks delivered
    if (p._nextChunkToSend >= static_cast<std::uint16_t>(p._chunks.size()))
    {
        _pendingNtfStrokeHistory.erase(it);
        return;
    }

    // Point _data at next chunk and reset timers
    auto now = std::chrono::steady_clock::now();
    p._data = p._chunks[p._nextChunkToSend];
    p._nextSendTime = now; // send on next tick immediately
    p._giveUpTime = now + std::chrono::seconds(2);
}

// ============================================================
// NTF_RCV_START_STROKE
// ============================================================

void Server::handle_ntfRcvStartStroke(std::span<const char> udpPacketWithoutMID, sockaddr_in* sa)
{
    assert((udpPacketWithoutMID.size() == PacketSize::NTF_RCV_START_STROKE - 1) &&
        "Size of NTF_RCV_START_STROKE packet received is wrong");
    ByteReader rdr{ .buffer = udpPacketWithoutMID };
    auto sessionIdHostOrder = ntohl(rdr.read<SessionId>());
    auto retIdHostOrder = ntohl(rdr.read<std::uint32_t>());

    std::lock_guard lock(_pendingNtfStartStrokeMutex);
    _pendingNtfStartStroke.erase(NtfKey{ sessionIdHostOrder, retIdHostOrder });
}

// ============================================================
// NTF_RCV_END_STROKE
// ============================================================

void Server::handle_ntfRcvEndStroke(std::span<const char> udpPacketWithoutMID, sockaddr_in* sa)
{
    assert((udpPacketWithoutMID.size() == PacketSize::NTF_RCV_END_STROKE - 1) &&
        "Size of NTF_RCV_END_STROKE packet received is wrong");
    ByteReader rdr{ .buffer = udpPacketWithoutMID };
    auto sessionIdHostOrder = ntohl(rdr.read<SessionId>());
    auto retIdHostOrder = ntohl(rdr.read<std::uint32_t>());

    std::lock_guard lock(_pendingNtfEndStrokeMutex);
    _pendingNtfEndStroke.erase(NtfKey{ sessionIdHostOrder, retIdHostOrder });
}

// ============================================================
// NTF_RCV_MSG_HISTORY
// ============================================================

void Server::handle_ntfRcvMsgHistory(std::span<const char> udpPacketWithoutMID, sockaddr_in* sa)
{
    assert((udpPacketWithoutMID.size() == PacketSize::NTF_RCV_MSG_HISTORY - 1) &&
        "Size of NTF_RCV_MSG_HISTORY packet received is wrong");

    ByteReader rdr{ .buffer = udpPacketWithoutMID };
    auto sessionIdHostOrder = ntohl(rdr.read<SessionId>());
    auto historyIdHostOrder = ntohl(rdr.read<std::uint32_t>());
    auto chunkIdxHostOrder = ntohs(rdr.read<std::uint16_t>());

    std::lock_guard lock(_pendingNtfMessageHistoryMutex);
    auto it = _pendingNtfMessageHistory.find(NtfKey{ sessionIdHostOrder, historyIdHostOrder });
    if (it == _pendingNtfMessageHistory.end()) return; // stale ack, ignore

    auto& p = it->second;

    // Ignore if not the chunk we're currently waiting on
    if (chunkIdxHostOrder != p._nextChunkToSend) return;

    // Advance
    p._nextChunkToSend++;

    // All chunks delivered
    if (p._nextChunkToSend >= static_cast<std::uint16_t>(p._chunks.size()))
    {
        _pendingNtfMessageHistory.erase(it);
        return;
    }

    // Point _data at next chunk and reset timers
    auto now = std::chrono::steady_clock::now();
    p._data = p._chunks[p._nextChunkToSend];
    p._nextSendTime = now; // send on next tick immediately
    p._giveUpTime = now + std::chrono::seconds(2);
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
        log(std::cerr, std::format("[Server] REQ_START_STROKE rejected: {}", errmsg));
        return;
    }

    auto mousePosNetworkOrder = rdr.read<MousePosition>();
    auto rgbat = rdr.read<std::array<std::uint8_t, 5>>();

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

    log(std::cout, std::format("[Server] REQ_START_STROKE accepted for session {}, stroke {}, colour {},{},{}",
        sessionIdHostOrder, strokeIdHostOrder,
        static_cast<uint8_t>(rgbat[0]),
        static_cast<uint8_t>(rgbat[1]),
        static_cast<uint8_t>(rgbat[2])));

    std::lock_guard lock(_pendingNtfStartStrokeMutex);
    LOCK_gameVariablesANDclientStorage([this,sessionIdHostOrder,&mousePosNetworkOrder,&rgbat](const auto& map, const auto&, const auto& drawerSessionIdOpt)
        {
            auto now = std::chrono::steady_clock::now();
            for (const auto& [ssiho, client] : map)
            {
                if (!client.inGame) continue;

                std::vector<char> svrmsg;
                svrmsg.resize(PacketSize::NTF_START_STROKE);
                ByteWriter svrwrt{ .buffer = svrmsg };
                svrwrt.write(static_cast<char>(MessageType::NTF_START_STROKE));
                svrwrt.write(htonl(ssiho));
                svrwrt.write(htonl(_sendStartStrokedIdServer));
                svrwrt.write(mousePosNetworkOrder[0]);
                svrwrt.write(mousePosNetworkOrder[1]);
                svrwrt.write(rgbat[0]);
                svrwrt.write(rgbat[1]);
                svrwrt.write(rgbat[2]);
                svrwrt.write(rgbat[3]);
                svrwrt.write(rgbat[4]);

                // Send immediately once
                sockaddr_in clientSa = client.sa;
                sendto(_socket, svrmsg.data(), static_cast<int>(svrmsg.size()), 0,
                    reinterpret_cast<sockaddr*>(&clientSa), sizeof(clientSa));

                // Add to pending for retry
                _pendingNtfStartStroke[NtfKey{ ssiho, _sendStartStrokedIdServer }] = PendingNTF{
                    ._data = std::move(svrmsg),
                    ._clientAddr = client.sa,
                    ._targetSessionId = ssiho,
                    ._ntfId = _sendStartStrokedIdServer,
                    ._nextSendTime = now + std::chrono::milliseconds(100),
                    ._giveUpTime = now + std::chrono::seconds(2),
                };
            }
            _sendStartStrokedIdServer++;
        });

    PastStroke ps{};
    ps._type = PastStroke::Type::START_STROKE;
    ps._data.resize(PacketSize::PAST_HISTORY_START_STROKE);
    ByteWriter paststartstrokeWrt{.buffer = ps._data };
    paststartstrokeWrt.write(mousePosNetworkOrder);
    paststartstrokeWrt.write(rgbat);
    std::lock_guard plock(_pastStrokesMutex);
    _pastStrokes_NEED_MUTEX.push_back(std::move(ps));
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

    std::lock_guard lock(_pendingNtfEndStrokeMutex);
    LOCK_gameVariablesANDclientStorage([this, sessionIdHostOrder](const auto& map, const auto&, const auto& drawerSessionIdOpt)
        {
            auto now = std::chrono::steady_clock::now();
            for (const auto& [ssiho, client] : map)
            {
                if (!client.inGame) continue;

                std::vector<char> svrmsg;
                svrmsg.resize(PacketSize::NTF_END_STROKE);
                ByteWriter svrwrt{ .buffer = svrmsg };
                svrwrt.write(static_cast<char>(MessageType::NTF_END_STROKE));
                svrwrt.write(htonl(ssiho));
                svrwrt.write(htonl(_sendEndStrokeIdServer));

                // Send immediately once
                sockaddr_in clientSa = client.sa;
                sendto(_socket, svrmsg.data(), static_cast<int>(svrmsg.size()), 0,
                    reinterpret_cast<sockaddr*>(&clientSa), sizeof(clientSa));

                // Add to pending for retry
                _pendingNtfEndStroke[NtfKey{ ssiho, _sendEndStrokeIdServer }] = PendingNTF{
                    ._data = std::move(svrmsg),
                    ._clientAddr = client.sa,
                    ._targetSessionId = ssiho,
                    ._ntfId = _sendEndStrokeIdServer,
                    ._nextSendTime = now + std::chrono::milliseconds(100),
                    ._giveUpTime = now + std::chrono::seconds(2),
                };
            }
            _sendEndStrokeIdServer++;
        });

    PastStroke ps{};
    ps._type = PastStroke::Type::END_STROKE;
    std::lock_guard plock(_pastStrokesMutex);
    _pastStrokes_NEED_MUTEX.push_back(std::move(ps));
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
    std::string usernameWhoGuessedTheWord;
    SessionId idWhoGuessedTheWord;
    bool alreadyGuessed = false;
    enum class ErrorRetVal { 
        OKAY_GUESSED_WORD,OKAY_DIDNT_GUESS_WORD,  // OKAYS 
        CANT_FIND, NOT_IN_GAME, GAME_NOT_RUNNING       // ERRORS
    };
    auto erv = LOCK_gameVariablesANDclientStorage([sessionIdHostOrder, &username,
        &actualmsg,&usernameWhoGuessedTheWord,
        &idWhoGuessedTheWord,&alreadyGuessed, this](auto& map, const auto& gameRunning, auto& drawerSessionIdOpt) {
        if (!gameRunning) return ErrorRetVal::GAME_NOT_RUNNING;
        auto it = map.find(sessionIdHostOrder);
        if (it == map.end()) return ErrorRetVal::CANT_FIND;
        else if (!it->second.inGame) return ErrorRetVal::NOT_IN_GAME;
        
        username = it->second.username;

        std::string str_msg = std::string(actualmsg.begin(), actualmsg.end()).c_str();
        std::transform(str_msg.begin(), str_msg.end(), str_msg.begin(), [](char c) {return std::toupper(c); });
        std::transform(word.second.begin(), word.second.end(), word.second.begin(), [](char c) {return std::toupper(c); });

        if (str_msg == word.second) {
            idWhoGuessedTheWord = it->first;
            usernameWhoGuessedTheWord = it->second.username;
            alreadyGuessed = it->second.wordAlreadyGuessed;
            if (!it->second.wordAlreadyGuessed && drawerSessionIdOpt != sessionIdHostOrder) {
                it->second.score += 75;
                it->second.wordAlreadyGuessed = true;
                NO_LOCK_broadcastScoreboard();
            }
            return ErrorRetVal::OKAY_GUESSED_WORD;
        }
        return ErrorRetVal::OKAY_DIDNT_GUESS_WORD;
        });

    //if (erv != ErrorRetVal::OK_BUT_NOT_GUESSED_WORD && erv != ErrorRetVal::OK_AND_GUESSED_WORD && erv != ErrorRetVal::HIDE_MSG) // essenncialy dis
    if (erv > ErrorRetVal::OKAY_DIDNT_GUESS_WORD)
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

    // validated its good REQ_MSG packet, send back RSP_MSG
    std::array<char, PacketSize::RSP_MSG> rspmsg;
    ByteWriterN rspwrt{ .buffer = rspmsg };
    rspwrt.write(static_cast<char>(MessageType::RSP_MSG));
    rspwrt.write(htonl(sessionIdHostOrder));
    rspwrt.write(htonl(msgIdHostOrder));
    bool successRsp = sendWithRetry(rspmsg, *sa);

    auto nameLength = static_cast<std::uint8_t>(username.length());
    if (nameLength > MAX_SHOWN_USERNAME_LEN)
    {
        username = username.substr(0, MAX_SHOWN_USERNAME_LEN - 3); // trunc it
        username += "...";
        nameLength = MAX_SHOWN_USERNAME_LEN;
    }

    std::string actualmsgstr = std::string(actualmsg.begin(), actualmsg.end());
    // Now NTF all clients for msg
    
    if (erv == ErrorRetVal::OKAY_DIDNT_GUESS_WORD) LOCK_sendMessage(username, actualmsgstr);
    else
    {
        auto nL = static_cast<std::uint8_t>(usernameWhoGuessedTheWord.length());
        if (nL > MAX_SHOWN_USERNAME_LEN)
        {
            usernameWhoGuessedTheWord = usernameWhoGuessedTheWord.substr(0, MAX_SHOWN_USERNAME_LEN - 3); // trunc it
            usernameWhoGuessedTheWord += "...";
            nL = MAX_SHOWN_USERNAME_LEN;
        }
        LOCK_gameVariablesANDclientStorage([this, &actualmsgstr, &username,
            &usernameWhoGuessedTheWord,
            &idWhoGuessedTheWord,
            erv, sessionIdHostOrder,&alreadyGuessed](const auto& map, const auto&, const auto& drawerSessionIdOpt)
            {
                std::uint8_t msgLength = static_cast<std::uint8_t>(actualmsgstr.length());
                std::uint8_t nameLength = static_cast<std::uint8_t>(username.length());
                auto now = std::chrono::steady_clock::now();
                for (const auto& [ssiho, client] : map)
                {
                    if (!client.inGame) continue;

                    std::string actual_actualmsgstr = actualmsgstr;
                    std::uint8_t actual_actualmsglen = msgLength;

                    enum class SendType { NameHasGuessed, Secret, Nothing };
                    SendType st = SendType::Nothing;
                    if (erv == ErrorRetVal::OKAY_GUESSED_WORD) // word was guessed
                    {
                        // now we can choose directions to go from here
                        enum class ClientType { Drawer, Guesser,EveryoneElse };
                        ClientType ct = (ssiho == drawerSessionIdOpt) ? ClientType::Drawer :
                            (ssiho == sessionIdHostOrder) ? ClientType::Guesser :
                            ClientType::EveryoneElse;

                        // we are sending to drawer
                        if (ct == ClientType::Drawer)
                        {
                            // if person who guessed is not the drawer, and is first time
                            // send NameHasGuessed
                            if (!alreadyGuessed && idWhoGuessedTheWord != drawerSessionIdOpt) st = SendType::NameHasGuessed;
                            else st = SendType::Nothing;
                        }
                        else if (ct == ClientType::Guesser)
                        {
                            if (!alreadyGuessed && idWhoGuessedTheWord != drawerSessionIdOpt) st = SendType::NameHasGuessed;
                            else st = SendType::Nothing;
                        }
                        else if (ct == ClientType::EveryoneElse)
                        {
                            if (!alreadyGuessed && idWhoGuessedTheWord != drawerSessionIdOpt) st = SendType::NameHasGuessed;
                            else st = SendType::Secret;
                        }
                    }

                    if (st == SendType::NameHasGuessed)
                    {
                        actual_actualmsgstr = std::format("{} has guessed the word!", usernameWhoGuessedTheWord);
                        actual_actualmsglen = static_cast<std::uint8_t>(actual_actualmsgstr.length());
                        username = "Server";
                        nameLength = static_cast<std::uint8_t>(username.length());
                    }
                    else if (st == SendType::Secret)
                    {
                        actual_actualmsgstr = "********";
                        actual_actualmsglen = static_cast<std::uint8_t>(actual_actualmsgstr.length());
                    }

                    std::vector<char> svrmsg;
                    svrmsg.resize(PacketSize::NTF_MSG_WITHOUT_BUFFER + actual_actualmsglen + nameLength);
                    ByteWriter svrwrt{ .buffer = svrmsg };
                    svrwrt.write(static_cast<char>(MessageType::NTF_MSG));
                    svrwrt.write(htonl(ssiho));
                    svrwrt.write(htonl(_messageIdServer));
                    svrwrt.write(actual_actualmsglen);
                    svrwrt.writeSpan(actual_actualmsgstr);
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
    LOCK_sendClearCanvasCommand();
    // When receive clear command, clear all past strokes
    std::lock_guard lock(_pastStrokesMutex);
    _pastStrokes_NEED_MUTEX.clear();
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
    LOCK_broadcastPacket(svrmsg,
        [&](auto& pkt, SessionId sid)
        {
            SessionId networkSID = htonl(sid);
            std::memcpy(pkt.data() + sizeof(MessageType::SVR_EXTEND_STROKE), &networkSID, sizeof(networkSID));
        });

    PastStroke ps{};
    ps._type = PastStroke::Type::EXTEND_STROKE;
    ps._data.resize(PacketSize::PAST_HISTORY_EXTEND_STROKE);
    ByteWriter paststartstrokeWrt{ .buffer = ps._data };
    paststartstrokeWrt.write(mousePosNetworkOrder);
    std::lock_guard lock(_pastStrokesMutex);
    _pastStrokes_NEED_MUTEX.push_back(std::move(ps));
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
        tickPendingNtf(_pendingNtfMsgMutex, _pendingNtfMsg, "NTF_MSG");
        tickPendingNtf(_pendingNtfRETMutex, _pendingNtfRET, "NTF_ROUND_END_TIME");
        tickPendingNtf(_pendingNtfNewWordMutex, _pendingNtfNewWords, "NTF_NEW_WORD");
        tickPendingNtf(_pendingNtfEndStrokeMutex, _pendingNtfEndStroke, "NTF_END_STROKE");
        tickPendingNtf(_pendingNtfNewWordLenMutex, _pendingNtfNewWordsLen, "NTF_NEW_WORD_LEN");
        tickPendingNtf(_pendingNtfStartStrokeMutex, _pendingNtfStartStroke, "NTF_START_STROKE");
        tickPendingNtf(_pendingNtfClearCanvasMutex, _pendingNtfClearCanvases, "NTF_CLEAR_CANVAS");
        tickPendingNtf(_pendingNtfStrokeHistoryMutex, _pendingNtfStrokeHistory, "NTF_STROKE_HISTORY");
        tickPendingNtf(_pendingNtfLeaderboardMutex, _pendingNtfLeaderboard, "NTF_UPDATE_LEADERBOARD");
        tickPendingNtf(_pendingNtfMessageHistoryMutex, _pendingNtfMessageHistory, "NTF_MESSAGE_HISTORY");
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
                //log(std::cerr, std::format("[Server] recvfrom() failed: {}", wsaErrorStr()));
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

void Server::LOCK_advanceDrawer()
{
    LOCK_gameVariablesANDclientStorage([this](auto& map, const auto& gameRunning, auto& drawerSessionIdOpt) {
        NO_LOCK_advanceDrawer();
        });
}

// ============================================================
// Advance drawer
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
// Broadcast Scoreboard
// ============================================================

void Server::LOCK_broadcastScoreboard()
{
    LOCK_gameVariablesANDclientStorage([this](const auto& map, const auto& gameRunning, const auto& drawerSessionIdOpt)
        {
            NO_LOCK_broadcastScoreboard();
        });
}

// ============================================================
// Broadcast Scoreboard
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
    struct PlayerEntry { std::string name; std::uint16_t score; SessionId sessionId; };
    std::vector<PlayerEntry> entries;

    for (const auto& [sid, client] : _clientStorageMap)
    {
        if (!client.inGame) continue;
        std::string name = client.username;
        if (name.size() > MAX_SHOWN_USERNAME_LEN)
            name = name.substr(0, MAX_SHOWN_USERNAME_LEN - 3) + "...";

        entries.push_back({ std::move(name), client.score, sid });
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
        std::uint8_t yourIndex = 0;
        for (std::uint8_t i = 0; i < entries.size(); ++i)
        {
            const auto& e = entries[i];
            auto nameLen = static_cast<std::uint8_t>(e.name.size());
            wrt.write(nameLen);
            wrt.writeSpan(std::span<const char>(e.name.data(), nameLen));
            wrt.write(htons(e.score));

            if (e.sessionId == ssiho)   // <--- need sessionId in entries
                yourIndex = i;
        }

        auto drawerLen = static_cast<std::uint8_t>(drawer.size());
        wrt.write(drawerLen);
        wrt.writeSpan(std::span<const char>(drawer.data(), drawerLen));

        wrt.write(yourIndex);

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
        log(std::cout, "[Server] Game started");
        gameRunning = true;

        rounds.second = MAX_ROUNDS;
        rounds.first = rounds.second;
        NO_LOCK_advanceDrawer();
        //NO_LOCK_broadcastScoreboard();
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

void Server::resetRound(bool isGameStart)
{
    bool wrapped = false;
    LOCK_gameVariablesANDclientStorage([this, &wrapped, isGameStart](auto& map, auto&, auto& drawerOpt) {
        for (auto& [_sid, client] : map)
            if (client.inGame) client.wordAlreadyGuessed = false;
        NO_LOCK_forceEndStroke();

        if (!isGameStart) {
            auto oldDrawer = drawerOpt;

            NO_LOCK_advanceDrawer();

            if (oldDrawer.has_value() && drawerOpt.has_value()
                && *drawerOpt != *oldDrawer
                && *drawerOpt <= *oldDrawer)
                wrapped = true;
        }

        pick_word();
        NO_LOCK_broadcastScoreboard();
        });

    LOCK_sendNewWord();
    LOCK_sendNewWordLen();
    LOCK_sendNewRoundEndTime();
    LOCK_sendClearCanvasCommand();

    if (wrapped) {
        // HERE DADDY, PUT IT IN AHHHH~ MESSAGE HERE AHHHH~
        std::lock_guard lock(_gameMutex);
        rounds.first = rounds.first > 1 ? rounds.first - 1 : 0;
    }
}

// ============================================================
// Get number of players - LOCK
// ============================================================

std::size_t Server::LOCK_getNumberOfPlayers()
{
    std::lock_guard lock(_clientStorageMutex);
    return NO_LOCK_getNumberOfPlayers();
}

// ============================================================
// Get number of players - NO LOCK
// ============================================================

std::size_t Server::NO_LOCK_getNumberOfPlayers()
{
    return std::ranges::count_if(_clientStorageMap, [](std::pair<const SessionId&, const Client&> p) {
        return p.second.inGame;
        });
}

// ============================================================
// Send new round end time
// ============================================================

void Server::LOCK_sendNewRoundEndTime()
{
    // broadcast to all clients ntf
    std::lock_guard ntfLock(_pendingNtfRETMutex);
    LOCK_clientStorage([this](const auto& map) {
        _roundEndTime = _roundEndTime < std::int64_t{0} ? 0 : _roundEndTime;
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
            wrt.write(htonll(static_cast<std::uint64_t>(_roundEndTime)));

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

void Server::LOCK_sendClearCanvasCommand()
{
    {
        std::lock_guard lock(_pastStrokesMutex);
        _pastStrokes_NEED_MUTEX.clear();
    }
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

// ============================================================
// Send new word length
// ============================================================

void Server::LOCK_sendNewWordLen()
{
    std::lock_guard ntfLock(_pendingNtfNewWordLenMutex);

    LOCK_clientStorage([this](const auto& map) {
        auto now = std::chrono::steady_clock::now();
        for (const auto& [ssiho, client] : map)
        {
            if (!client.inGame || ssiho == _drawerSessionId) continue;
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

// ============================================================
// Send new word
// ============================================================

void Server::LOCK_sendNewWord()
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
// Send stroke history
// ============================================================

void Server::LOCK_sendStrokeHistory()
{
    std::vector<PastStroke> cpy;
    {
        std::lock_guard lock(_pastStrokesMutex);
        cpy = _pastStrokes_NEED_MUTEX;
    }

    if (cpy.empty())
    {
        log(std::cerr, "[Server] Skipping send stroke history, no stroke history");
        return;
    }

    // 1. Build raw stroke data only (no headers)
    std::vector<char> strokeData;
    strokeData.reserve(30'000);
    for (const auto& stroke : cpy)
    {
        strokeData.emplace_back(static_cast<char>(stroke._type));
        strokeData.insert(strokeData.end(), stroke._data.begin(), stroke._data.end());
    }

    // 2. Calculate number of chunks correctly
    constexpr std::size_t MAX_STROKE_PAYLOAD =
        MaxUdpPacketBytes - PacketSize::NTF_STROKE_HISTORY_WITHOUT_DATA;
    std::size_t numChunks = (strokeData.size() + MAX_STROKE_PAYLOAD - 1) / MAX_STROKE_PAYLOAD;
    if (numChunks == 0) numChunks = 1;
    const std::uint16_t totalChunks = static_cast<std::uint16_t>(numChunks);
    const std::uint32_t totalStrokeDataBytes = static_cast<std::uint32_t>(strokeData.size());

    // 3. Pre-build chunks with dummy session id (0), will patch per-client later
    //    Layout: [MID(1)][SESSION_ID(4)][HISTORY_ID(4)][TOTAL_BYTES(4)][CHUNK_NUM(2)][NUM_STROKES(4)][PAYLOAD]
    //    Offsets:    0        1              5               9               13            15            19
    constexpr std::size_t OFF_SESSION_ID = 1;
    constexpr std::size_t OFF_TOTAL_BYTES = 9;
    constexpr std::size_t OFF_CHUNK_NUM = 13;

    std::vector<std::vector<char>> chunks;
    chunks.reserve(numChunks);

    std::size_t strokeDataOffset = 0;
    for (std::uint16_t chunkIdx = 0; chunkIdx < totalChunks; chunkIdx++)
    {
        std::size_t bytesLeft = strokeData.size() - strokeDataOffset;
        std::size_t payloadSize = std::min(bytesLeft, MAX_STROKE_PAYLOAD);

        std::vector<char> chunk(PacketSize::NTF_STROKE_HISTORY_WITHOUT_DATA + payloadSize);
        ByteWriter wrt{ .buffer = chunk };
        wrt.write(static_cast<char>(MessageType::NTF_STROKE_HISTORY));
        wrt.write(std::uint32_t{ 0 });                          // session id - dummy, patched per client
        wrt.write(htonl(_strokeHistoryIdServer));               // history id
        wrt.write(htonl(totalStrokeDataBytes));                 // total bytes across all chunks
        wrt.write(htons(chunkIdx));                             // this chunk's index (0-based)
        wrt.write(htonl(static_cast<std::uint32_t>(cpy.size()))); // number of strokes

        wrt.writeSpan(std::span<const char>{
            strokeData.data() + strokeDataOffset, payloadSize });

        strokeDataOffset += payloadSize;
        chunks.push_back(std::move(chunk));
    }

    assert(strokeDataOffset == strokeData.size() && "Not all stroke data was chunked");

    // 4. Per-client: patch session id into each chunk, register into pending
    std::lock_guard ntfLock(_pendingNtfStrokeHistoryMutex);
    LOCK_clientStorage([&](const auto& map) {
        auto now = std::chrono::steady_clock::now();
        for (const auto& [ssiho, client] : map)
        {
            if (!client.inGame) continue;

            // Patch session id into every chunk for this client
            std::uint32_t sessionIdNet = htonl(ssiho);
            std::vector<std::vector<char>> clientChunks = chunks; // copy shared chunks
            for (auto& chunk : clientChunks)
                std::memcpy(chunk.data() + OFF_SESSION_ID, &sessionIdNet, sizeof(sessionIdNet));

            // Send chunk 0 immediately
            sockaddr_in clientSa = client.sa;
            sendto(_socket, clientChunks[0].data(), static_cast<int>(clientChunks[0].size()), 0,
                reinterpret_cast<sockaddr*>(&clientSa), sizeof(clientSa));

            // Register into pending — _data starts as chunk 0, _nextChunkToSend starts at 0
            PendingNTF& pending = _pendingNtfStrokeHistory[NtfKey{ ssiho, _strokeHistoryIdServer }];
            pending._chunks = std::move(clientChunks);
            pending._data = pending._chunks[0];       // safe: assigned after _chunks
            pending._clientAddr = client.sa;
            pending._targetSessionId = ssiho;
            pending._ntfId = _strokeHistoryIdServer;
            pending._nextChunkToSend = 0;
            pending._nextSendTime = now + std::chrono::milliseconds(100);
            pending._giveUpTime = now + std::chrono::seconds(10);
        }
        _strokeHistoryIdServer++;
        });
}

// ============================================================
// Send Message History
// ============================================================

void Server::LOCK_sendMessageHistory()
{
    std::deque<PastMessage> cpy;
    {
        std::lock_guard lock(_pastChatMsgMutex);
        cpy = _pastChatMsg_NEED_MUTEX;
    }

    if (cpy.empty())
    {
        log(std::cerr, "[Server] Skipping send message history, no message history");
        return;
    }

    // 1. Build raw message data only (no headers)
    std::vector<char> msgData;
    msgData.reserve(30'000);
    for (const auto& pastMsg : cpy)
    {
        msgData.emplace_back(static_cast<std::uint8_t>(pastMsg._message.length()));
        msgData.insert(msgData.end(), pastMsg._message.begin(), pastMsg._message.end());
        msgData.emplace_back(static_cast<std::uint8_t>(pastMsg._name.length()));
        msgData.insert(msgData.end(), pastMsg._name.begin(), pastMsg._name.end());
    }

    // 2. Calculate number of chunks correctly
    constexpr std::size_t MAX_MSG_PAYLOAD =
        MaxUdpPacketBytes - PacketSize::NTF_MSG_HISTORY_WITHOUT_DATA;
    std::size_t numChunks = (msgData.size() + MAX_MSG_PAYLOAD - 1) / MAX_MSG_PAYLOAD;
    if (numChunks == 0) numChunks = 1;
    const std::uint16_t totalChunks = static_cast<std::uint16_t>(numChunks);
    const std::uint32_t totalMsgDataBytes = static_cast<std::uint32_t>(msgData.size());

    // 3. Pre-build chunks with dummy session id (0), will patch per-client later
    //    Layout: [MID(1)][SESSION_ID(4)][HISTORY_ID(4)][TOTAL_BYTES(4)][CHUNK_NUM(2)][NUM_MSG(2)][PAYLOAD]
    //    Offsets:    0        1              5               9               13            15        17
    constexpr std::size_t OFF_SESSION_ID = 1;
    constexpr std::size_t OFF_TOTAL_BYTES = 9;
    constexpr std::size_t OFF_CHUNK_NUM = 13;

    std::vector<std::vector<char>> chunks;
    chunks.reserve(numChunks);

    std::size_t msgDataOffset = 0;
    for (std::uint16_t chunkIdx = 0; chunkIdx < totalChunks; chunkIdx++)
    {
        std::size_t bytesLeft = msgData.size() - msgDataOffset;
        std::size_t payloadSize = std::min(bytesLeft, MAX_MSG_PAYLOAD);

        std::vector<char> chunk(PacketSize::NTF_MSG_HISTORY_WITHOUT_DATA + payloadSize);
        ByteWriter wrt{ .buffer = chunk };
        wrt.write(static_cast<char>(MessageType::NTF_MSG_HISTORY));
        wrt.write(std::uint32_t{ 0 });                          // session id - dummy, patched per client
        wrt.write(htonl(_msgHistoryIdServer));               // history id
        wrt.write(htonl(totalMsgDataBytes));                 // total bytes across all chunks
        wrt.write(htons(chunkIdx));                             // this chunk's index (0-based)
        wrt.write(htons(static_cast<std::uint16_t>(cpy.size()))); // number of msges

        wrt.writeSpan(std::span<const char>{
            msgData.data() + msgDataOffset, payloadSize });

        msgDataOffset += payloadSize;
        chunks.push_back(std::move(chunk));
    }

    assert(msgDataOffset == msgData.size() && "Not all msg data was chunked");

    // 4. Per-client: patch session id into each chunk, register into pending
    std::lock_guard ntfLock(_pendingNtfMessageHistoryMutex);
    LOCK_clientStorage([&](const auto& map) {
        auto now = std::chrono::steady_clock::now();
        for (const auto& [ssiho, client] : map)
        {
            if (!client.inGame) continue;

            // Patch session id into every chunk for this client
            std::uint32_t sessionIdNet = htonl(ssiho);
            std::vector<std::vector<char>> clientChunks = chunks; // copy shared chunks
            for (auto& chunk : clientChunks)
                std::memcpy(chunk.data() + OFF_SESSION_ID, &sessionIdNet, sizeof(sessionIdNet));

            // Send chunk 0 immediately
            sockaddr_in clientSa = client.sa;
            sendto(_socket, clientChunks[0].data(), static_cast<int>(clientChunks[0].size()), 0,
                reinterpret_cast<sockaddr*>(&clientSa), sizeof(clientSa));

            // Register into pending — _data starts as chunk 0, _nextChunkToSend starts at 0
            PendingNTF& pending = _pendingNtfMessageHistory[NtfKey{ ssiho, _msgHistoryIdServer }];
            pending._chunks = std::move(clientChunks);
            pending._data = pending._chunks[0];       // safe: assigned after _chunks
            pending._clientAddr = client.sa;
            pending._targetSessionId = ssiho;
            pending._ntfId = _msgHistoryIdServer;
            pending._nextChunkToSend = 0;
            pending._nextSendTime = now + std::chrono::milliseconds(100);
            pending._giveUpTime = now + std::chrono::seconds(10);
        }
        _msgHistoryIdServer++;
        });
}

// ============================================================
// Force broadcast to all clients to end stroke
// ============================================================

void Server::LOCK_forceEndStroke()
{
    std::lock_guard lock(_pendingNtfEndStrokeMutex);
    LOCK_gameVariablesANDclientStorage([this](const auto& map, const auto&, const auto& drawerSessionIdOpt)
        {
            auto now = std::chrono::steady_clock::now();
            for (const auto& [ssiho, client] : map)
            {
                if (!client.inGame) continue;

                std::vector<char> svrmsg;
                svrmsg.resize(PacketSize::NTF_END_STROKE);
                ByteWriter svrwrt{ .buffer = svrmsg };
                svrwrt.write(static_cast<char>(MessageType::NTF_END_STROKE));
                svrwrt.write(htonl(ssiho));
                svrwrt.write(htonl(_sendEndStrokeIdServer));

                // Send immediately once
                sockaddr_in clientSa = client.sa;
                sendto(_socket, svrmsg.data(), static_cast<int>(svrmsg.size()), 0,
                    reinterpret_cast<sockaddr*>(&clientSa), sizeof(clientSa));

                // Add to pending for retry
                _pendingNtfEndStroke[NtfKey{ ssiho, _sendEndStrokeIdServer }] = PendingNTF{
                    ._data = std::move(svrmsg),
                    ._clientAddr = client.sa,
                    ._targetSessionId = ssiho,
                    ._ntfId = _sendEndStrokeIdServer,
                    ._nextSendTime = now + std::chrono::milliseconds(100),
                    ._giveUpTime = now + std::chrono::seconds(2),
                };
            }
            _sendEndStrokeIdServer++;
        });

    PastStroke ps{};
    ps._type = PastStroke::Type::END_STROKE;
    std::lock_guard plock(_pastStrokesMutex);
    _pastStrokes_NEED_MUTEX.push_back(std::move(ps));
}

// ============================================================
// Force broadcast to all clients to end stroke
// ============================================================

void Server::NO_LOCK_forceEndStroke()
{
    std::lock_guard lock(_pendingNtfEndStrokeMutex);
    auto now = std::chrono::steady_clock::now();
    for (const auto& [ssiho, client] : _clientStorageMap)
    {
        if (!client.inGame) continue;

        std::vector<char> svrmsg;
        svrmsg.resize(PacketSize::NTF_END_STROKE);
        ByteWriter svrwrt{ .buffer = svrmsg };
        svrwrt.write(static_cast<char>(MessageType::NTF_END_STROKE));
        svrwrt.write(htonl(ssiho));
        svrwrt.write(htonl(_sendEndStrokeIdServer));

        // Send immediately once
        sockaddr_in clientSa = client.sa;
        sendto(_socket, svrmsg.data(), static_cast<int>(svrmsg.size()), 0,
            reinterpret_cast<sockaddr*>(&clientSa), sizeof(clientSa));

        // Add to pending for retry
        _pendingNtfEndStroke[NtfKey{ ssiho, _sendEndStrokeIdServer }] = PendingNTF{
            ._data = std::move(svrmsg),
            ._clientAddr = client.sa,
            ._targetSessionId = ssiho,
            ._ntfId = _sendEndStrokeIdServer,
            ._nextSendTime = now + std::chrono::milliseconds(100),
            ._giveUpTime = now + std::chrono::seconds(2),
        };
    }
    _sendEndStrokeIdServer++;

    PastStroke ps{};
    ps._type = PastStroke::Type::END_STROKE;
    std::lock_guard plock(_pastStrokesMutex);
    _pastStrokes_NEED_MUTEX.push_back(std::move(ps));
}

void Server::LOCK_sendMessage(const std::string& username, const std::string& message)
{
    std::lock_guard ntfLock(_pendingNtfMsgMutex);
    LOCK_clientStorage([this, &message, &username](const auto& map)
        {
            std::uint8_t msgLength = static_cast<std::uint8_t>(message.length());
            std::uint8_t nameLength = static_cast<std::uint8_t>(username.length());
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
                svrwrt.writeSpan(message);
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
    // push into message history
    {
        std::lock_guard chatmsgLock(_pastChatMsgMutex);
        _pastChatMsg_NEED_MUTEX.push_back(PastMessage{ ._message = message, ._name = username });

        // make sure it is within the size constraints
        while (_pastChatMsg_NEED_MUTEX.size() > MAX_CHAT_HISTORY_SHOWN)
        {
            _pastChatMsg_NEED_MUTEX.pop_front();
        }
    }
}

// ============================================================
// Send leaderboard - LOCK
// ============================================================

void Server::LOCK_sendLeaderboard()
{
    std::lock_guard lock(_clientStorageMutex);
    NO_LOCK_sendLeaderboard();
}

// ============================================================
// Send leaderboard - LOCK
// ============================================================

void Server::NO_LOCK_sendLeaderboard()
{
    for (const auto& [_ignore, client] : _clientStorageMap) _userStore.saveHighscore(client.username, client.highscore);
    std::vector<std::pair<std::string, std::uint16_t>> leaderboardEntries = _userStore.getHighscoresAndName();

    std::ranges::sort(leaderboardEntries,
        [](const std::pair<std::string, std::uint16_t>& lhs,
            const std::pair<std::string, std::uint16_t>& rhs) {
                return lhs.second > rhs.second;
        });;

    decltype(leaderboardEntries) truncEntries = leaderboardEntries;

    // take top entries
    if (truncEntries.size() > MAX_LEADERBOARD_ENTRIES) truncEntries.resize(MAX_LEADERBOARD_ENTRIES);

    // first pass to get total var bytes
    std::size_t totalMsgBytes = PacketSize::NTF_LEADERBOARD_BASE;
    for (auto& [name, _ignore] : truncEntries)
        totalMsgBytes += (1 + 2 + name.length());

    std::vector<char> msg;
    msg.resize(totalMsgBytes);
    ByteWriter wrt{.buffer = msg};

    wrt.write(static_cast<char>(MessageType::NTF_UPDATE_LEADERBOARD));
    wrt.write(std::uint32_t{ 0 }); // will update client session id later
    wrt.write(htonl(_sendLeaderboardIdServer));
    wrt.write(static_cast<std::uint8_t>(truncEntries.size()));
    
    for (auto& [name, highscore] : truncEntries)
    {
        wrt.write(static_cast<std::uint8_t>(name.length()));
        wrt.writeSpan(name);
        wrt.write(htons(highscore));
    }

    std::size_t offsetAtEndOfVars = wrt.offset;

    wrt.write(std::uint32_t{ 0 }); // will update player index later
    wrt.write(std::uint16_t{ 0 }); // will update player score later

    auto now = std::chrono::steady_clock::now();
    std::lock_guard lock(_pendingNtfLeaderboardMutex);
    for (const auto& [ssiho, client] : _clientStorageMap)
    {
        // update session id in message
        SessionId ssidNetwork = htonl(ssiho);
        std::memcpy(msg.data() + 1, &ssidNetwork, sizeof(ssidNetwork));

        std::string truncUsername = client.username;
        if (truncUsername.length() > MAX_SHOWN_USERNAME_LEN)
        {
            truncUsername = truncUsername.substr(0, MAX_SHOWN_USERNAME_LEN - 3);
            truncUsername += "...";
        }
        std::uint32_t playerIndexHost = 0;
        std::uint16_t playerScoreHost = 0;
        // update player index 
        auto it = std::ranges::find_if(leaderboardEntries, [&truncUsername](const std::pair<std::string, std::uint16_t>& entry)
            {
                std::string entryUsername = entry.first;
                if (entryUsername.length() > MAX_SHOWN_USERNAME_LEN)
                {
                    entryUsername = entryUsername.substr(0, MAX_SHOWN_USERNAME_LEN - 3);
                    entryUsername += "...";
                }
                return entryUsername == truncUsername;
            });
        if (it != leaderboardEntries.end())
        {
            playerIndexHost = static_cast<std::uint32_t>(std::distance(leaderboardEntries.begin(), it));
            playerScoreHost = it->second;
        }

        std::uint32_t playerIndexNetwork = htonl(playerIndexHost);
        std::uint16_t playerScoreNetwork = htons(playerScoreHost);

        std::memcpy(msg.data() + offsetAtEndOfVars, &playerIndexNetwork, sizeof(playerIndexNetwork));
        std::memcpy(msg.data() + offsetAtEndOfVars + sizeof(playerIndexNetwork),
            &playerScoreNetwork, sizeof(playerScoreNetwork));

        // Send immediately once
        sockaddr_in clientSa = client.sa;
        sendto(_socket, msg.data(), static_cast<int>(msg.size()), 0,
            reinterpret_cast<sockaddr*>(&clientSa), sizeof(clientSa));

        // Add to pending for retry
        _pendingNtfLeaderboard[NtfKey{ ssiho, _sendLeaderboardIdServer }] = PendingNTF{
            ._data = msg, // NO MOVE
            ._clientAddr = client.sa,
            ._targetSessionId = ssiho,
            ._ntfId = _sendLeaderboardIdServer,
            ._nextSendTime = now + std::chrono::milliseconds(100),
            ._giveUpTime = now + std::chrono::seconds(2),
        };
    }


    _sendLeaderboardIdServer++;
}

std::uint8_t Server::LOCK_getCurrentRound()
{
    std::lock_guard lock(_gameMutex);
    return rounds.first;
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

// ============================================================
// Helper - Tick pending ntf
// ============================================================

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