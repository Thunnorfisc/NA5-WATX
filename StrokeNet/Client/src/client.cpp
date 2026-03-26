/* Start Header
***********************************************************************/

/*! \file   client.cpp
    \author Loh Boon Cheong, Timothy
    \par    email: loh.b@digipen.edu
    \co-author William Wibisana Dumanauw
    \par    email: williamwibisana.d@digipen.edu
    \date   20th March, 2026
    \brief  Copyright (C) 2026 DigiPen Institute of Technology

    Reproduction or diclosure of this file or its contents without the prior
    written consent of DigiPen Institute of Technology is prohibited. */

/* End Header
***********************************************************************/
#include "client.hpp"

#include <array>
#include <format>
#include <cassert>
#include <iostream>
#include <algorithm>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>
#include <winsock2.h>
#include <WS2tcpip.h>
#include <iphlpapi.h>
#pragma comment(lib, "Ws2_32.lib")

static std::mutex s_logMutex;
void log(std::ostream& os, std::string_view msg)
{
    std::lock_guard lock(s_logMutex);
    os << msg << '\n';
}

// ============================================================
// initalize
// ============================================================

void Client::initalize()
{
    WSADATA wsaData;
    if(int r = WSAStartup(MAKEWORD(2, 2), &wsaData); r != 0)
        throw std::runtime_error(std::format("[Client] WSAStartup failed: {}", r));

    _socket = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if(_socket == INVALID_SOCKET)
        throw std::runtime_error(std::format("[Client] socket() failed: {}", wsaErrorStr()));

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(0);
    if(bind(_socket, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) == SOCKET_ERROR)
        throw std::runtime_error(std::format("[Client] bind() failed: {}", wsaErrorStr()));

    sockaddr_in bound{};
    int boundLen = sizeof(bound);
    getsockname(_socket, reinterpret_cast<sockaddr*>(&bound), &boundLen);
    _portHostOrder = ntohs(bound.sin_port);

    char hostName[256]{};
    gethostname(hostName, sizeof(hostName));
    addrinfo hints{};
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_DGRAM;
    hints.ai_protocol = IPPROTO_UDP;
    addrinfo* result = nullptr;
    getaddrinfo(hostName, nullptr, &hints, &result);
    char ipStr[INET_ADDRSTRLEN]{};
    inet_ntop(AF_INET, &reinterpret_cast<sockaddr_in*>(result->ai_addr)->sin_addr, ipStr, sizeof(ipStr));
    _ip = ipStr;
    freeaddrinfo(result);

    int broadcast = 1;
    setsockopt(_socket, SOL_SOCKET, SO_BROADCAST,
        reinterpret_cast<const char*>(&broadcast), sizeof(broadcast));

    // make it non-blocking
    u_long mode = 1;
    ioctlsocket(_socket, FIONBIO, &mode);

    log(std::cout, std::format("[Client] Ready at {}:{}", _ip, _portHostOrder));
}

// ============================================================
// terminate
// ============================================================

void Client::terminate()
{
    if(_socket != INVALID_SOCKET) closesocket(_socket);
    WSACleanup();
}

// ============================================================
// RSP_START_STROKE
// ============================================================

void Client::handle_RSP_StartStroke(std::span<const char> msg)
{
    assert(msg.size() == (PacketSize::RSP_START_STROKE - 1) && "Size of rsp_start_stroke is wrong");
    ByteReader rdr{ .buffer = msg };
    auto sessionIdHost = ntohl(rdr.read<SessionId>());
    if (sessionIdHost != _sessionId)
    {
        log(std::cerr,
            std::format("[Client] Received an invalid session id [{}] from the server, ignoring packet", sessionIdHost));
        return;
    }
    auto strokeIdHost = ntohl(rdr.read<std::uint32_t>());
    // simply erase, might or might not succeed its ok.
    _pendingStartStrokes.erase(strokeIdHost);
}

// ============================================================
// RSP_END_STROKE
// ============================================================

void Client::handle_RSP_EndStroke(std::span<const char> msg)
{
    assert(msg.size() == (PacketSize::RSP_END_STROKE - 1) && "Size of rsp_end_stroke is wrong");
    ByteReader rdr{ .buffer = msg };
    auto sessionIdHost = ntohl(rdr.read<SessionId>());
    if (sessionIdHost != _sessionId)
    {
        log(std::cerr,
            std::format("[Client] Received an invalid session id [{}] from the server, ignoring packet", sessionIdHost));
        return;
    }
    auto strokeIdHost = ntohl(rdr.read<std::uint32_t>());
    // simply erase, might or might not succeed its ok.
    _pendingEndStrokes.erase(strokeIdHost);
}

// ============================================================
// RSP_CLEAR_CANVAS
// ============================================================

void Client::handle_RSP_ClearCanvas(std::span<const char> msg)
{
    assert(msg.size() == (PacketSize::RSP_CLEAR_CANVAS - 1) && "Size of rsp_clear_canvas is wrong");
    ByteReader rdr{ .buffer = msg };
    auto sessionIdHost = ntohl(rdr.read<SessionId>());
    if (sessionIdHost != _sessionId)
    {
        log(std::cerr,
            std::format("[Client] Received an invalid session id [{}] from the server, ignoring packet", sessionIdHost));
        return;
    }
    auto clearIdHost = ntohl(rdr.read<std::uint32_t>());
    // simply erase, might or might not succeed its ok.
    _pendingClearCanvas.erase(clearIdHost);
}

// ============================================================
// RSP_MSG
// ============================================================

void Client::handle_RSP_Msg(std::span<const char> msg)
{
    assert(msg.size() == (PacketSize::RSP_MSG - 1) && "Size of rsp_msg is wrong");
    ByteReader rdr{ .buffer = msg };
    auto sessionIdHost = ntohl(rdr.read<SessionId>());
    if (sessionIdHost != _sessionId)
    {
        log(std::cerr,
            std::format("[Client] Received an invalid session id [{}] from the server, ignoring packet", sessionIdHost));
        return;
    }
    auto msgIdHost = ntohl(rdr.read<std::uint32_t>());
    // simply erase, might or might not succeed its ok.
    _pendingMsges.erase(msgIdHost);
}

// ============================================================
// SVR_START_STROKE
// ============================================================

void Client::handle_SVR_StartStroke(std::span<const char> msg)
{
    assert(msg.size() == (PacketSize::SVR_START_STROKE - 1) && "Size of svr_start_stroke is wrong");
    ByteReader rdr{ .buffer = msg };
    auto sessionIdHost = ntohl(rdr.read<SessionId>());
    if (sessionIdHost != _sessionId)
    {
        log(std::cerr,
            std::format("[Client] Received an invalid session id [{}] from the server, ignoring packet", sessionIdHost));
        return;
    }
    std::vector<char> towrt;
    towrt.resize(PacketSize::SVR_START_STROKE - sizeof(SessionId) - sizeof(MessageType::SVR_START_STROKE));
    ByteWriter wrt{ .buffer = towrt };
    
    auto mousePositionHostOrder = rdr.read<MousePosition>();
    mousePositionHostOrder[0] = ntohs(mousePositionHostOrder[0]);
    mousePositionHostOrder[1] = ntohs(mousePositionHostOrder[1]);
    wrt.write(mousePositionHostOrder);

    auto rgbat = rdr.read<std::array<char, 5>>();
    wrt.write(rgbat);

    ReceivedStrokeCommand rcs;
    rcs._type = ReceivedStrokeCommand::Type::START_STROKE;
    rcs._data = towrt;
    std::lock_guard lock(_strokeCommandsReceivedMut);
    _strokeCommandsReceived.push(std::move(rcs));
}

// ============================================================
// SVR_END_STROKE
// ============================================================

void Client::handle_SVR_EndStroke(std::span<const char> msg)
{
    assert(msg.size() == (PacketSize::SVR_END_STROKE - 1) && "Size of svr_end_stroke is wrong");
    ByteReader rdr{ .buffer = msg };
    auto sessionIdHost = ntohl(rdr.read<SessionId>());
    if (sessionIdHost != _sessionId)
    {
        log(std::cerr,
            std::format("[Client] Received an invalid session id [{}] from the server, ignoring packet", sessionIdHost));
        return;
    }
    ReceivedStrokeCommand rcs;
    rcs._type = ReceivedStrokeCommand::Type::END_STROKE;
    std::lock_guard lock(_strokeCommandsReceivedMut);
    _strokeCommandsReceived.push(std::move(rcs));
}

// ============================================================
// SVR_EXTEND_STROKE
// ============================================================

void Client::handle_SVR_ExtendStroke(std::span<const char> msg)
{
    assert(msg.size() == (PacketSize::SVR_EXTEND_STROKE - 1) && "Size of svr_extend_stroke is wrong");
    ByteReader rdr{ .buffer = msg };
    auto sessionIdHost = ntohl(rdr.read<SessionId>());
    if (sessionIdHost != _sessionId)
    {
        log(std::cerr,
            std::format("[Client] Received an invalid session id [{}] from the server, ignoring packet", sessionIdHost));
        return;
    }
    std::vector<char> towrt;
    towrt.resize(PacketSize::SVR_EXTEND_STROKE - sizeof(SessionId) - sizeof(MessageType::SVR_EXTEND_STROKE));
    ByteWriter wrt{ .buffer = towrt };

    auto mousePositionHostOrder = rdr.read<MousePosition>();
    mousePositionHostOrder[0] = ntohs(mousePositionHostOrder[0]);
    mousePositionHostOrder[1] = ntohs(mousePositionHostOrder[1]);
    wrt.write(mousePositionHostOrder);

    ReceivedStrokeCommand rcs;
    rcs._type = ReceivedStrokeCommand::Type::EXTEND_STROKE;
    rcs._data = towrt;
    std::lock_guard lock(_strokeCommandsReceivedMut);
    _strokeCommandsReceived.push(std::move(rcs));
}

// ============================================================
// NTF_MSG
// ============================================================

void Client::handle_NTF_Msg(std::span<const char> msg)
{
    ByteReader rdr{ .buffer = msg };
    auto sessionIdHost = ntohl(rdr.read<SessionId>());
    if (sessionIdHost != _sessionId)
    {
        log(std::cerr,
            std::format("[Client] Received an invalid session id [{}] from the server, ignoring packet", sessionIdHost));
        return;
    }
    auto msgIdHost = ntohl(rdr.read<std::uint32_t>());

    auto msgLengthHost = rdr.read<std::uint8_t>();
    auto actualMsg = rdr.readBytes(msgLengthHost);
    
    auto nameLengthHost = rdr.read<std::uint8_t>();
    auto actualName = rdr.readBytes(nameLengthHost);

    // Prepare ack to send back to server
    std::array<char, PacketSize::NTF_RCV_MSG> ntfrcvmsg;
    ByteWriterN wrt{ .buffer = ntfrcvmsg };
    wrt.write(static_cast<char>(MessageType::NTF_RCV_MSG));
    wrt.write(htonl(_sessionId));
    wrt.write(htonl(msgIdHost));

    // if not able to send back ntf_rcv_msg,
    // log, and continue pushing the message
    // into the recvQueue
    if (!sendWithRetry(ntfrcvmsg))
    {
        log(std::cerr, "[Client] Unable to send NTF_RCV_MSG back to server");
    }

    // we only check here if we have already seen this
    // because we want the server to stop sending regardless if
    // we already seen the message. its like letting a worried parent
    // know that ure ok without them worrying and constantly pinging you
    if (_seenMsgesId.contains(msgIdHost))
    {
        log(std::cerr,
            std::format("[Client] Received an already seen message, discarding message"));
        return;
    }
    _seenMsgesId.insert(msgIdHost);

    // limit it at max 100
    while (_seenMsgesId.size() >= 100) _seenMsgesId.erase(_seenMsgesId.begin());

    // Write into queue
    ReceivedChatMessage rcm;
    rcm._message.resize(msgLengthHost);
    rcm._name.resize(nameLengthHost);

    std::memcpy(rcm._message.data(), actualMsg.data(), msgLengthHost);
    std::memcpy(rcm._name.data(), actualName.data(), nameLengthHost);

    std::lock_guard lock(_msgesReceivedMut);
    _msgesReceived.push(std::move(rcm));
}

void Client::handle_NTF_ClearCanvas(std::span<const char> msg)
{
    assert(msg.size() == (PacketSize::NTF_CLEAR_CANVAS - 1) && "Size of ntf_clear_canvas is wrong");
    ByteReader rdr{ .buffer = msg };
    auto sessionIdHost = ntohl(rdr.read<SessionId>());
    if (sessionIdHost != _sessionId)
    {
        log(std::cerr,
            std::format("[Client] Received an invalid session id [{}] from the server, ignoring packet", sessionIdHost));
        return;
    }
    auto clearIdHost = ntohl(rdr.read<std::uint32_t>());

    // Prepare ack to send back to server
    std::array<char, PacketSize::NTF_RCV_CLEAR_CANVAS> ntfclrcvs;
    ByteWriterN wrt{ .buffer = ntfclrcvs };
    wrt.write(static_cast<char>(MessageType::NTF_RCV_CLEAR_CANVAS));
    wrt.write(htonl(_sessionId));
    wrt.write(htonl(clearIdHost));

    // if not able to send back ntf_clear_canvas,
    // log, and continue pushing the message
    // into the recvQueue
    if (!sendWithRetry(ntfclrcvs))
    {
        log(std::cerr, "[Client] Unable to send NTF_RCV_CLEAR_CANVAS back to server");
    }

    ReceivedStrokeCommand rcs;
    rcs._type = ReceivedStrokeCommand::Type::CLEAR_CANVAS;
    std::lock_guard lock(_strokeCommandsReceivedMut);
    _strokeCommandsReceived.push(std::move(rcs));
}

void Client::handle_NTF_UpdateScoreboard(std::span<const char> msg)
{
    ByteReader rdr{ .buffer = msg };
    auto sessionIdHost = ntohl(rdr.read<SessionId>());
    if (sessionIdHost != _sessionId)
    {
        log(std::cerr,
            std::format("[Client] Received an invalid session id [{}] from the server, ignoring packet", sessionIdHost));
        return;
    }

    auto scoreIdHost = ntohl(rdr.read<std::uint32_t>());

    auto num = ntohl(rdr.read<std::uint32_t>());

    auto nameLengthHost = rdr.read<std::uint8_t>();
    auto actualName = rdr.readBytes(nameLengthHost);

    auto score = ntohs(rdr.read<std::uint16_t>());

    // Prepare ack to send back to server
    std::array<char, PacketSize::NTF_RCV_UPDATE_SCOREBOARD> ntfrcvsb;
    ByteWriterN wrt{ .buffer = ntfrcvsb };
    wrt.write(static_cast<char>(MessageType::NTF_RCV_UPDATE_SCOREBOARD));
    wrt.write(htonl(_sessionId));
    wrt.write(htonl(scoreIdHost));

    // if not able to send back ntf_rcv_update_scoreboard,
    // log, and continue pushing the message
    // into the recvQueue
    if (!sendWithRetry(ntfrcvsb))
    {
        log(std::cerr, "[Client] Unable to send NTF_RCV_UPDATE_SCOREBOARD back to server");
    }

    ScoreBoard sb;
    sb._users.resize(num);
    for (std::size_t i{}; i < num; ++i) {
        sb._users[i].first.resize(nameLengthHost);
        std::memcpy(sb._users[i].first.data(), actualName.data(), nameLengthHost);
        sb._users[i].second = score;
    }
    std::lock_guard lock(_scoreboardReceivedMut);
    _scoreboardReceived.push(std::move(sb));
}

void Client::handle_NTF_RoundEndTime(std::span<const char> msg)
{
    assert(msg.size() == (PacketSize::NTF_ROUND_END_TIME - 1) && "Size of ntf_round_end_time is wrong");
    ByteReader rdr{.buffer = msg};
    auto sessionIdHost = ntohl(rdr.read<SessionId>());
    if(sessionIdHost != _sessionId)
    {
        log(std::cerr,
            std::format("[Client] Received an invalid session id [{}] from the server, ignoring packet", sessionIdHost));
        return;
    }
    auto roundEndTimeIdHost = ntohl(rdr.read<std::uint32_t>());
    auto roundEndTimeHost = static_cast<std::int64_t>(ntohll(rdr.read<std::uint64_t>()));

    // Prepare ack to send back to server
    std::array<char, PacketSize::NTF_RCV_ROUND_END_TIME> ntfRcvRET;
    ByteWriterN wrt{.buffer = ntfRcvRET};
    wrt.write(static_cast<char>(MessageType::NTF_RCV_ROUND_END_TIME));
    wrt.write(htonl(_sessionId));
    wrt.write(htonl(roundEndTimeIdHost));

    // if not able to send back ntf_clear_canvas,
    // log, and continue pushing the message
    // into the recvQueue
    if(!sendWithRetry(ntfRcvRET))
    {
        log(std::cerr, "[Client] Unable to send NTF_RCV_ROUND_END_TIME back to server");
    }

    _roundEndTimeMs = roundEndTimeHost;
}

// ============================================================
// startListening
// ============================================================

void Client::startListening(std::stop_token st)
{
    std::vector<char> buf(MaxUdpPacketBytes);

    while (!st.stop_requested())
    {
        // send any commands
        auto now = std::chrono::steady_clock::now();
        tickBuffered(_bufferedStartStrokeMutex, _pendingStartStrokes, _bufferedStartStrokeQueue, now, "Start Stroke");
        tickBuffered(_bufferEndStrokeMutex, _pendingEndStrokes, _bufferedEndStrokeQueue, now, "End Stroke");
        tickBuffered(_bmtsMsgesMutex, _pendingMsges, _bufferedMsgesQueue, now, "Chat Message");
        tickBuffered(_bufferClearCanvasMutex, _pendingClearCanvas, _bufferedClearCanvasQueue, now, "Clear Canvas");

        // listen for commands
        fd_set rs;
        FD_ZERO(&rs);
        FD_SET(_socket, &rs);
        timeval tv{ 0, static_cast<long>(_recvTimeOut * 1'000'000.0) };

        int ready = select(0, &rs, nullptr, nullptr, &tv);
        if (ready == SOCKET_ERROR)
        {
            if (!isRetryableSelectError(WSAGetLastError()))
            {
                log(std::cerr, "[Client] listen select() failed");
                return;
            }
            else continue;
        }

        if(ready == 0) continue;

        // drain all the recvfrom
        while (true)
        {
            sockaddr_in from{};
            int fromLen = sizeof(from);
            int n = recvfrom(_socket, buf.data(), static_cast<int>(buf.size()),
                0, reinterpret_cast<sockaddr*>(&from), &fromLen);
            if (n == SOCKET_ERROR)
            {
                if (WSAGetLastError() == WSAEWOULDBLOCK) break; // fully drained
                log(std::cerr, "[Client] recvfrom error");
                break;
            }
            else if (n == 0) continue; // move on with our lives

            auto mid = static_cast<MessageType>(buf[0]);
            auto it = _listenMsgFns.find(mid);
            if(it != _listenMsgFns.end())
                (it->second)(std::span<const char>(buf).subspan(1, n - 1));
        }
    }
}

// ============================================================
// Helper - Send with retry
// ============================================================

bool Client::sendWithRetry(std::span<const char> data)
{
    bool success = false;
    int svrAddrLen = sizeof(_serverAddr);
    for (int i = 0; i < _maxRetries; i++)
    {
        int sentBytes = sendto(_socket, data.data(), static_cast<int>(data.size()),
            0, reinterpret_cast<sockaddr*>(&_serverAddr),
            svrAddrLen);
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
// loginViaBroadcast
// ============================================================

LoginStatus Client::loginViaBroadcast(const std::string& username, const std::string& password)
{
    if(_sessionId != InvalidSessionId) return LoginStatus::INVALID_CREDENTIALS;

    sockaddr_in bcast{};
    bcast.sin_family = AF_INET;
    bcast.sin_port = htons(ServerUdpPort);
    bcast.sin_addr.S_un.S_addr = INADDR_BROADCAST;

    unsigned char hashed_pass[MAX_PASSWORD_LEN]{};
    hashbrown256(password, hashed_pass);

    std::vector<char> sendPkt(PacketSize::REQ_LOGIN, '\0');
    {
        ByteWriter wrt{ .buffer = sendPkt };
        wrt.write(static_cast<char>(MessageType::REQ_LOGIN));
        std::array<char, MAX_USERNAME_LEN> u{};
        std::memcpy(u.data(), username.c_str(), username.size());
        wrt.write(u);
        std::array<char, MAX_PASSWORD_LEN> p{};
        std::memcpy(p.data(), hashed_pass, sizeof(hashed_pass));
        wrt.write(p);
    }
    
    /* {
        ByteWriter wrt{.buffer = sendPkt};
        wrt.write(static_cast<char>(MessageType::REQ_LOGIN));
        std::array<char, MAX_USERNAME_LEN> u{};
        std::memcpy(u.data(), username.c_str(), username.size());
        wrt.write(u);
        std::array<char, MAX_PASSWORD_LEN> p{};
        std::memcpy(p.data(), password.c_str(), password.size());
        wrt.write(p);
    }*/

    std::vector<char> recvBuf(MaxUdpPacketBytes);

    for(int attempt = 0; attempt < _maxRetries; ++attempt)
    {
        sendto(_socket, sendPkt.data(), static_cast<int>(sendPkt.size()),
            0, reinterpret_cast<sockaddr*>(&bcast), sizeof(bcast));

        auto deadline = std::chrono::steady_clock::now() +
            std::chrono::milliseconds(static_cast<int>(_recvTimeOut * 1000.0));

        while(true)
        {
            auto rem = deadline - std::chrono::steady_clock::now();
            if(rem.count() <= 0) break;

            fd_set rs; FD_ZERO(&rs); FD_SET(_socket, &rs);
            auto us = std::chrono::duration_cast<std::chrono::microseconds>(rem);
            timeval tv{static_cast<long>(us.count() / 1'000'000),
                       static_cast<long>(us.count() % 1'000'000)};

            if(select(0, &rs, nullptr, nullptr, &tv) <= 0) break;

            sockaddr_in from{}; int fromLen = sizeof(from);
            while (true)
            {
                int n = recvfrom(_socket, recvBuf.data(), static_cast<int>(recvBuf.size()),
                    0, reinterpret_cast<sockaddr*>(&from), &fromLen);

                if (n == SOCKET_ERROR)
                {
                    if (WSAGetLastError() == WSAEWOULDBLOCK) break;
                    break;
                }
                if (n != static_cast<int>(PacketSize::RSP_LOGIN_AND_CREATE_ACCOUNT)) continue;
                if (static_cast<MessageType>(recvBuf[0]) != MessageType::RSP_LOGIN_AND_CREATE_ACCOUNT) continue;

                ByteReader rdr{ .buffer = std::span<const char>(recvBuf).subspan(1, n - 1) };
                SessionId   sid = ntohl(rdr.read<SessionId>());
                LoginStatus status = static_cast<LoginStatus>(rdr.read<std::uint8_t>());

                if (status == LoginStatus::SUCCESS)
                {
                    _sessionId = sid;
                    _serverAddr = from;
                    char ip[INET_ADDRSTRLEN]{};
                    inet_ntop(AF_INET, &from.sin_addr, ip, sizeof(ip));
                    _serverIpAndPort = std::format("{}:{}", ip, ntohs(from.sin_port));
                    _seenMsgesId.clear();
                    //_nextSeq.store(0);

                    _stopSource = std::stop_source{};
                    _listeningThread = std::jthread([](std::stop_token st) { startListening(st); }, _stopSource.get_token());

                    log(std::cout, std::format("[Client] Login OK — session {}, server {}",
                        _sessionId.load(), _serverIpAndPort));
                }
                return status;
            }
        }
    }

    log(std::cerr, "[Client] loginViaBroadcast: no server responded");
    return LoginStatus::INVALID_CREDENTIALS;
}

// ============================================================
// createAccountViaBroadcast
// ============================================================

LoginStatus Client::createAccountViaBroadcast(const std::string& username, const std::string& password)
{
    sockaddr_in bcast{};
    bcast.sin_family = AF_INET;
    bcast.sin_port = htons(ServerUdpPort);
    bcast.sin_addr.S_un.S_addr = INADDR_BROADCAST;

    unsigned char hashed_pass[MAX_PASSWORD_LEN]{};
    hashbrown256(password, hashed_pass);

    std::vector<char> sendPkt(PacketSize::REQ_CREATE_ACCOUNT, '\0');
    {
        ByteWriter wrt{ .buffer = sendPkt };
        wrt.write(static_cast<char>(MessageType::REQ_CREATE_ACCOUNT));
        std::array<char, MAX_USERNAME_LEN> u{};
        std::memcpy(u.data(), username.c_str(), username.size());
        wrt.write(u);
        std::array<char, MAX_PASSWORD_LEN> p{};
        std::memcpy(p.data(), hashed_pass, sizeof(hashed_pass));
        wrt.write(p);
    }

    //std::vector<char> sendPkt(PacketSize::REQ_CREATE_ACCOUNT, '\0');
    //{
    //    ByteWriter wrt{.buffer = sendPkt};
    //    wrt.write(static_cast<char>(MessageType::REQ_CREATE_ACCOUNT));
    //    std::array<char, MAX_USERNAME_LEN> u{};
    //    std::memcpy(u.data(), username.c_str(), username.size());
    //    wrt.write(u);
    //    std::array<char, MAX_PASSWORD_LEN> p{};
    //    std::memcpy(p.data(), password.c_str(), password.size());
    //    wrt.write(p);
    //}

    std::vector<char> recvBuf(MaxUdpPacketBytes);

    for(int attempt = 0; attempt < _maxRetries; ++attempt)
    {
        sendto(_socket, sendPkt.data(), static_cast<int>(sendPkt.size()),
            0, reinterpret_cast<sockaddr*>(&bcast), sizeof(bcast));

        auto deadline = std::chrono::steady_clock::now() +
            std::chrono::milliseconds(static_cast<int>(_recvTimeOut * 1000.0));

        while(true)
        {
            auto rem = deadline - std::chrono::steady_clock::now();
            if(rem.count() <= 0) break;

            fd_set rs; FD_ZERO(&rs); FD_SET(_socket, &rs);
            auto us = std::chrono::duration_cast<std::chrono::microseconds>(rem);
            timeval tv{static_cast<long>(us.count() / 1'000'000),
                       static_cast<long>(us.count() % 1'000'000)};

            if(select(0, &rs, nullptr, nullptr, &tv) <= 0) break;

            sockaddr_in from{}; int fromLen = sizeof(from);
            while (true)
            {
                int n = recvfrom(_socket, recvBuf.data(), static_cast<int>(recvBuf.size()),
                    0, reinterpret_cast<sockaddr*>(&from), &fromLen);
                if (n == SOCKET_ERROR)
                {
                    if (WSAGetLastError() == WSAEWOULDBLOCK) break;
                    break;
                }
                if(n != static_cast<int>(PacketSize::RSP_LOGIN_AND_CREATE_ACCOUNT)) continue;
                if(static_cast<MessageType>(recvBuf[0]) != MessageType::RSP_LOGIN_AND_CREATE_ACCOUNT) continue;

                ByteReader rdr{.buffer = std::span<const char>(recvBuf).subspan(1, n - 1)};
                [[maybe_unused]] SessionId sid = ntohl(rdr.read<SessionId>());
                LoginStatus status = static_cast<LoginStatus>(rdr.read<std::uint8_t>());
                log(std::cout, std::format("[Client] createAccount for '{}': status {}",
                    username, static_cast<int>(status)));
                return status;
            }
        }
    }

    log(std::cerr, "[Client] createAccountViaBroadcast: no server responded");
    return LoginStatus::INVALID_CREDENTIALS;
}

// ============================================================
// disconnect
// ============================================================

void Client::disconnect()
{
    auto stopThreads = []()
        {
            _stopSource.request_stop();
            if(_listeningThread.joinable()) _listeningThread.join();
        };

    if(_sessionId == InvalidSessionId) { stopThreads(); return; }

    std::vector<char> pkt(PacketSize::FAF_DISCONNECT);
    ByteWriter wrt{.buffer = pkt};
    wrt.write(static_cast<char>(MessageType::FAF_DISCONNECT));
    wrt.write(htonl(_sessionId));

    sendto(_socket, pkt.data(), static_cast<int>(pkt.size()),
        0, reinterpret_cast<sockaddr*>(&_serverAddr), sizeof(_serverAddr));

    log(std::cout, std::format("[Client] Disconnected from {}", _serverIpAndPort));
    _serverIpAndPort.clear();
    _seenMsgesId.clear();
    std::memset(&_serverAddr, 0, sizeof(_serverAddr));
    _sessionId = InvalidSessionId;

    stopThreads();
}

// ============================================================
// send start stroke
// ============================================================

void Client::sendStartStroke(
    std::uint32_t strokeid,
    std::array<std::uint16_t, 2> mousePos,
    std::array<std::uint8_t, 5> rgbat)
{
    std::vector<char> msg;
    msg.resize(PacketSize::REQ_START_STROKE);
    ByteWriter wrt{ .buffer = msg };
    wrt.write(static_cast<char>(MessageType::REQ_START_STROKE));
    wrt.write(htonl(_sessionId));
    wrt.write(htonl(strokeid));
    wrt.write(htons(mousePos[0]));
    wrt.write(htons(mousePos[1]));
    wrt.write(rgbat);
    BufferedToSend buffered;
    buffered._data = std::move(msg);
    buffered._hostId = strokeid;
    std::lock_guard lock(_bufferedStartStrokeMutex);
    _bufferedStartStrokeQueue.push(std::move(buffered));
}

// ============================================================
// send extend stroke
// ============================================================

void Client::sendExtendStroke(std::uint32_t strokeid, std::array<std::uint16_t, 2> mousePos)
{
    // fire and forget ts
    // dont need buffering and staging ground, just send only
    std::array<char, PacketSize::FAF_EXTEND_STROKE> msg;
    ByteWriterN wrt{ .buffer = msg };
    wrt.write(static_cast<char>(MessageType::FAF_EXTEND_STROKE));
    wrt.write(htonl(_sessionId));
    wrt.write(htonl(strokeid));
    wrt.write(htons(mousePos[0]));
    wrt.write(htons(mousePos[1]));
    sendto(_socket, msg.data(), static_cast<int>(msg.size()),
        0, reinterpret_cast<sockaddr*>(&_serverAddr), sizeof(_serverAddr));
}

// ============================================================
// send end stroke
// ============================================================

void Client::sendEndStroke(std::uint32_t strokeid)
{
    std::vector<char> msg;
    msg.resize(PacketSize::REQ_END_STROKE);
    ByteWriter wrt{ .buffer = msg };
    wrt.write(static_cast<char>(MessageType::REQ_END_STROKE));
    wrt.write(htonl(_sessionId));
    wrt.write(htonl(strokeid));
    BufferedToSend buffered;
    buffered._data = std::move(msg);
    buffered._hostId = strokeid;
    std::lock_guard lock(_bufferEndStrokeMutex);
    _bufferedEndStrokeQueue.push(std::move(buffered));
}

// ============================================================
// send clear canvas
// ============================================================

void Client::sendClearCanvas(std::uint32_t clearid)
{
    std::vector<char> msg;
    msg.resize(PacketSize::REQ_CLEAR_CANVAS);
    ByteWriter wrt{ .buffer = msg };
    wrt.write(static_cast<char>(MessageType::REQ_CLEAR_CANVAS));
    wrt.write(htonl(_sessionId));
    wrt.write(htonl(clearid));
    BufferedToSend buffered;
    buffered._data = std::move(msg);
    buffered._hostId = clearid;
    std::lock_guard lock(_bufferClearCanvasMutex);
    _bufferedClearCanvasQueue.push(std::move(buffered));
}

// ============================================================
// for game to retrieve stroke commands
// 
// only try_lock, if not move on
// ============================================================

std::queue<Client::ReceivedStrokeCommand> Client::getReceivedStrokeCommands()
{
    if (!_strokeCommandsReceivedMut.try_lock()) return {};
    std::queue<ReceivedStrokeCommand> cpy;
    cpy.swap(_strokeCommandsReceived);
    _strokeCommandsReceivedMut.unlock();
    return cpy;
}

// ============================================================
// send req msg
// ============================================================

void Client::sendChatMessage(std::uint32_t msgId, const std::string& message)
{
    if (message.empty())
    {
        log(std::cerr,
            std::format("[Client] Can't send empty message!"));
        return;
    }
    if (message.length() > 255)
    {
        log(std::cerr,
            std::format("[Client] Message must be less than 256 characters, message received had {} characters", message.length()));
        return;
    }
    std::vector<char> msg;
    msg.resize(PacketSize::REQ_MSG_WITHOUT_BUFFER + message.length());
    ByteWriter wrt{ .buffer = msg };
    wrt.write(static_cast<char>(MessageType::REQ_MSG));
    wrt.write(htonl(_sessionId));
    wrt.write(htonl(msgId));
    wrt.write(static_cast<std::uint8_t>(message.length()));
    wrt.writeSpan(message);
    BufferedToSend buffered;
    buffered._data = std::move(msg);
    buffered._hostId = msgId;
    std::lock_guard lock(_bmtsMsgesMutex);
    _bufferedMsgesQueue.push(std::move(buffered));
}

// ============================================================
// for game to retrieve chat msges
// 
// only try_lock, if not move on
// ============================================================

std::queue<Client::ReceivedChatMessage> Client::getReceivedChatMessages()
{
    if (!_msgesReceivedMut.try_lock()) return {};
    std::queue<ReceivedChatMessage> cpy;
    cpy.swap(_msgesReceived);
    _msgesReceivedMut.unlock();
    return cpy;
}

std::queue<Client::ScoreBoard> Client::getScoreboard()
{
    if (!_scoreboardReceivedMut.try_lock()) return {};
    std::queue<ScoreBoard> cpy;
    cpy.swap(_scoreboardReceived);
    _scoreboardReceivedMut.unlock();
    return cpy;
}

// ============================================================
// for game to retrieve round end time
// ============================================================

std::int64_t Client::getRoundEndTimeMs()
{
    return _roundEndTimeMs;
}