/* Start Header **************************************************************/
/*! \file   server.cpp
    \author Loh Boon Cheong, Timothy
    \par    email: loh.b@digipen.edu
    \date   20th March, 2026
    \brief  Copyright (C) 2026 DigiPen Institute of Technology
            Reproduction or disclosure of this file or its contents without
            the prior written consent of DigiPen Institute of Technology is
            prohibited. */
            /* End Header ****************************************************************/
#define _WINSOCK_DEPRECATED_NO_WARNINGS

#include "server.hpp"
#include "shared_protocol.hpp"

#include <cmath>
#include <chrono>
#include <format>
#include <fstream>
#include <cassert>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <algorithm>
#include <filesystem>
#include <string_view>
#include <execution>
#include <filesystem>

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
    std::mutex s_logMutex;
    void log(std::ostream& os, std::string_view msg)
    {
        std::lock_guard lock(s_logMutex);
        os << msg << '\n';
    }
}

// ============================================================
// Constructor / Destructor
// ============================================================

Server::Server()
{
    WSADATA wsaData;
    if(int r = WSAStartup(MAKEWORD(2, 2), &wsaData); r != 0)
        throw std::runtime_error(std::format("[Server] WSAStartup failed: {}", r));

    _socket = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if(_socket == INVALID_SOCKET)
        throw std::runtime_error(std::format("[Server] socket() failed: {}", wsaErrorStr()));

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(ServerUdpPort);
    if(bind(_socket, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) == SOCKET_ERROR)
        throw std::runtime_error(std::format("[Server] bind() failed: {}", wsaErrorStr()));

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

    _userStore.load();
    log(std::cout, std::format("[Server] Ready at {}:{}", _ip, _portHostOrder));
}

Server::~Server()
{
    _stopSource.request_stop();
    if(_thread.joinable())           _thread.join();
    if(_retransmitThread.joinable()) _retransmitThread.join();
    if(_socket != INVALID_SOCKET)    closesocket(_socket);
    WSACleanup();
}

// ============================================================
// startListening
// ============================================================

void Server::startListening()
{
    if(_thread.joinable())
        throw std::runtime_error("[Server] startListening() called while already running");

    _thread = std::thread([this]() { actualStartListening(_stopSource.get_token()); });
    _retransmitThread = std::thread([this]() { retransmitLoop(_stopSource.get_token()); });
}

bool Server::isListeningThreadFinished() noexcept { return _threadFinished; }

// ============================================================
// retransmitLoop
// ============================================================

void Server::retransmitLoop(std::stop_token st) noexcept
{
    while(!st.stop_requested())
    {
        std::this_thread::sleep_for(
            std::chrono::duration<double>(RetransmitIntervalSec / 2.0));

        auto now = std::chrono::steady_clock::now();
        std::lock_guard lock(_pendingAcksMutex);

        for(auto it = _pendingAcks.begin(); it != _pendingAcks.end(); )
        {
            PendingReliable& pr = it->second;
            double elapsed = std::chrono::duration<double>(now - pr.lastSentAt).count();

            if(elapsed >= RetransmitIntervalSec)
            {
                if(pr.attempts >= MaxReliableAttempts)
                {
                    log(std::cerr, std::format(
                        "[Server] Reliable packet serverSeq={} to session {} dropped after {} attempts",
                        it->first, pr.destSessionId, pr.attempts));
                    it = _pendingAcks.erase(it);
                    continue;
                }

                log(std::cout, std::format(
                    "[Server] Retransmitting serverSeq={} to session {} (attempt {})",
                    it->first, pr.destSessionId, pr.attempts + 1));

                doSend(pr.packet, pr.dest);
                pr.lastSentAt = now;
                pr.attempts++;
            }
            ++it;
        }
    }
}

// ============================================================
// sendCanvasDrawCommand
//
// Relays a canvas command to ALL connected clients.
//   START_STROKE : reliable per-client
//   ADD_POINT    : fire-and-forget
//   END_STROKE   : reliable per-client
//
// Packet layouts (network byte order on the wire):
//   PF_START_STROKE: [type1][sessionId4][serverSeq4][strokeId4][mousePos4][RGBAT5]  = 22
//   PF_ADD_POINT   : [type1][sessionId4][serverSeq4][strokeId4][mousePos4]          = 17
//   PF_END_STROKE  : [type1][sessionId4][serverSeq4][strokeId4]                     = 13
// ============================================================

void Server::sendCanvasDrawCommand(const CanvasDrawCommand& cds)
{
    decltype(_sessionIdToClient) clients;
    {
        std::lock_guard lock(_clientStorageMutex);
        clients = _sessionIdToClient;
    }

    for(const auto& [sessionIdHostOrder, client] : clients)
    {
        std::vector<char> pkt;

        switch(cds._type)
        {
            using enum MessageType;

        case PF_START_STROKE:
        {
            // _msg layout: [strokeId u32][mousePos u16x2][RGBAT 5 bytes] = 13 bytes
            assert(cds._msg.size() == PacketSize::PF_START_STROKE - PacketSize::HEADER_SIZE);
            pkt.resize(PacketSize::PF_START_STROKE);

            ByteReader rdr{.buffer = cds._msg};
            auto strokeId = htonl(rdr.read<std::uint32_t>());
            auto mousePos = rdr.read<MousePosition>();
            auto RGBAT = rdr.read<std::array<char, 5>>();
            mousePos[0] = htons(mousePos[0]);
            mousePos[1] = htons(mousePos[1]);

            SequenceNumber serverSeq = allocServerSeq();
            ByteWriter wrt{.buffer = pkt};
            wrt.write(static_cast<char>(PF_START_STROKE));
            wrt.write(htonl(sessionIdHostOrder));
            wrt.write(htonl(serverSeq));
            wrt.write(strokeId);
            wrt.write(mousePos);
            wrt.write(RGBAT);

            sendReliableTo(serverSeq, std::move(pkt), client.sa, sessionIdHostOrder);
            continue;
        }

        case PF_ADD_POINT:
        {
            // _msg layout: [strokeId u32][mousePos u16x2] = 8 bytes
            assert(cds._msg.size() == PacketSize::PF_ADD_POINT - PacketSize::HEADER_SIZE);
            pkt.resize(PacketSize::PF_ADD_POINT);

            ByteReader rdr{.buffer = cds._msg};
            auto strokeId = htonl(rdr.read<std::uint32_t>());
            auto mousePos = rdr.read<MousePosition>();
            mousePos[0] = htons(mousePos[0]);
            mousePos[1] = htons(mousePos[1]);

            ByteWriter wrt{.buffer = pkt};
            wrt.write(static_cast<char>(PF_ADD_POINT));
            wrt.write(htonl(sessionIdHostOrder));
            wrt.write(htonl(cds._sqNumberHostOrder));
            wrt.write(strokeId);
            wrt.write(mousePos);
            break; // falls through to unreliable doSend below
        }

        case PF_END_STROKE:
        {
            // _msg layout: [strokeId u32] = 4 bytes
            assert(cds._msg.size() == PacketSize::PF_END_STROKE - PacketSize::HEADER_SIZE);
            pkt.resize(PacketSize::PF_END_STROKE);

            ByteReader rdr{.buffer = cds._msg};
            auto strokeId = htonl(rdr.read<std::uint32_t>());

            SequenceNumber serverSeq = allocServerSeq();
            ByteWriter wrt{.buffer = pkt};
            wrt.write(static_cast<char>(PF_END_STROKE));
            wrt.write(htonl(sessionIdHostOrder));
            wrt.write(htonl(serverSeq));
            wrt.write(strokeId);

            sendReliableTo(serverSeq, std::move(pkt), client.sa, sessionIdHostOrder);
            continue;
        }

        default:
            assert(false && "sendCanvasDrawCommand: unexpected message type");
            continue;
        }

        // Unreliable path (ADD_POINT only)
        doSend(pkt, client.sa);
    }
}

// ============================================================
// registerCdsFn / deregisterCdsFn
// ============================================================

Server::RegCdsFnId Server::registerCdsFn(
    std::function<void(SessionId, const CanvasDrawCommand&)> fn)
{
    std::lock_guard lock(_cdsFnsMutex);
    _cdsFns.emplace(_nextRegCdsFnId, std::move(fn));
    return _nextRegCdsFnId++;
}

void Server::deregisterCdsFn(RegCdsFnId id)
{
    std::lock_guard lock(_cdsFnsMutex);
    auto it = _cdsFns.find(id);
    if(it == _cdsFns.end())
    {
        log(std::cerr, std::format("[Server] deregisterCdsFn: id {} not found", id));
        return;
    }
    _cdsFns.erase(it);
}

// ============================================================
// sendReliableTo / doSend / sendAck
// ============================================================

void Server::sendReliableTo(SequenceNumber serverSeq, std::vector<char> packet,
    const sockaddr_in& dest, SessionId destSessionId)
{
    PendingReliable pr;
    pr.packet = packet;
    pr.dest = dest;
    pr.destSessionId = destSessionId;
    pr.lastSentAt = std::chrono::steady_clock::now() -
        std::chrono::duration_cast<std::chrono::steady_clock::duration>(
            std::chrono::duration<double>(RetransmitIntervalSec));
    pr.attempts = 0;

    {
        std::lock_guard lock(_pendingAcksMutex);
        _pendingAcks[serverSeq] = std::move(pr);
    }

    doSend(packet, dest);
}

void Server::doSend(const std::vector<char>& packet, const sockaddr_in& dest)
{
    sendto(_socket, packet.data(), static_cast<int>(packet.size()),
        0, reinterpret_cast<const sockaddr*>(&dest), sizeof(dest));
}

void Server::sendAck(const sockaddr_in& dest, SequenceNumber seqNetworkOrder)
{
    std::vector<char> ack(PacketSize::ACK);
    ByteWriter wrt{.buffer = ack};
    wrt.write(static_cast<char>(MessageType::ACK));
    wrt.write(seqNetworkOrder);
    doSend(ack, dest);
}

// ============================================================
// handle_ack
// ============================================================

void Server::handle_ack(std::span<const char> body, sockaddr_in* /*sa*/)
{
    if(body.size() < sizeof(SequenceNumber)) return;

    ByteReader rdr{.buffer = body};
    SequenceNumber serverSeq = ntohl(rdr.read<SequenceNumber>());

    std::lock_guard lock(_pendingAcksMutex);
    auto it = _pendingAcks.find(serverSeq);
    if(it != _pendingAcks.end())
    {
        log(std::cout, std::format("[Server] ACK received for serverSeq={}", serverSeq));
        _pendingAcks.erase(it);
    }
}

// ============================================================
// handle_pfStartStroke
//
// Packet body (after type byte):
//   [sessionId 4][seqNet 4][strokeId 4][mousePos 4][RGBAT 5]  = 21 bytes
//
// 1. ACK the sender.
// 2. Deserialise into a CanvasDrawCommand.
// 3. Pass to handle_CanvasDrawingCommand for ordering + dispatch.
// ============================================================

void Server::handle_pfStartStroke(std::span<const char> body, sockaddr_in* sa)
{
    assert(body.size() == PacketSize::PF_START_STROKE - 1);

    ByteReader rdr{.buffer = body};
    auto sessionIdHostOrder = ntohl(rdr.read<SessionId>());
    auto seqNetworkOrder = rdr.read<SequenceNumber>(); // kept for ACK
    auto sequenceNumberHostOrder = ntohl(seqNetworkOrder);
    auto strokeIdHostOrder = ntohl(rdr.read<std::uint32_t>());
    auto mousePos = rdr.read<MousePosition>();
    auto RGBAT = rdr.read<std::array<char, 5>>();
    mousePos[0] = ntohs(mousePos[0]);
    mousePos[1] = ntohs(mousePos[1]);

    sendAck(*sa, seqNetworkOrder);

    // Build CanvasDrawCommand — _msg: [strokeId u32][mousePos u16x2][RGBAT 5] = 13 bytes
    CanvasDrawCommand cds;
    cds._type = MessageType::PF_START_STROKE;
    cds._sqNumberHostOrder = sequenceNumberHostOrder;
    cds._strokeId = strokeIdHostOrder;
    cds._msg.resize(PacketSize::PF_START_STROKE - PacketSize::HEADER_SIZE);

    ByteWriter wrt{.buffer = cds._msg};
    wrt.write(strokeIdHostOrder);
    wrt.write(mousePos);
    wrt.write(RGBAT);

    handle_CanvasDrawingCommand(std::move(cds), sessionIdHostOrder);
}

// ============================================================
// handle_pfAddPoint  — unreliable, no ACK
//
// Packet body: [sessionId 4][seq 4][strokeId 4][mousePos 4]  = 16 bytes
// ============================================================

void Server::handle_pfAddPoint(std::span<const char> body, sockaddr_in* /*sa*/)
{
    assert(body.size() == PacketSize::PF_ADD_POINT - 1);

    ByteReader rdr{.buffer = body};
    auto sessionIdHostOrder = ntohl(rdr.read<SessionId>());
    auto sequenceNumberHostOrder = ntohl(rdr.read<SequenceNumber>());
    auto strokeIdHostOrder = ntohl(rdr.read<std::uint32_t>());
    auto mousePos = rdr.read<MousePosition>();
    mousePos[0] = ntohs(mousePos[0]);
    mousePos[1] = ntohs(mousePos[1]);

    // Build CanvasDrawCommand — _msg: [strokeId u32][mousePos u16x2] = 8 bytes
    CanvasDrawCommand cds;
    cds._type = MessageType::PF_ADD_POINT;
    cds._sqNumberHostOrder = sequenceNumberHostOrder;
    cds._strokeId = strokeIdHostOrder;
    cds._msg.resize(PacketSize::PF_ADD_POINT - PacketSize::HEADER_SIZE);

    ByteWriter wrt{.buffer = cds._msg};
    wrt.write(strokeIdHostOrder);
    wrt.write(mousePos);

    handle_CanvasDrawingCommand(std::move(cds), sessionIdHostOrder);
}

// ============================================================
// handle_pfEndStroke
//
// Packet body: [sessionId 4][seqNet 4][strokeId 4]  = 12 bytes
// ============================================================

void Server::handle_pfEndStroke(std::span<const char> body, sockaddr_in* sa)
{
    assert(body.size() == PacketSize::PF_END_STROKE - 1);

    ByteReader rdr{.buffer = body};
    auto sessionIdHostOrder = ntohl(rdr.read<SessionId>());
    auto seqNetworkOrder = rdr.read<SequenceNumber>();
    auto sequenceNumberHostOrder = ntohl(seqNetworkOrder);
    auto strokeIdHostOrder = ntohl(rdr.read<std::uint32_t>());

    sendAck(*sa, seqNetworkOrder);

    // Build CanvasDrawCommand — _msg: [strokeId u32] = 4 bytes
    CanvasDrawCommand cds;
    cds._type = MessageType::PF_END_STROKE;
    cds._sqNumberHostOrder = sequenceNumberHostOrder;
    cds._strokeId = strokeIdHostOrder;
    cds._msg.resize(PacketSize::PF_END_STROKE - PacketSize::HEADER_SIZE);

    ByteWriter wrt{.buffer = cds._msg};
    wrt.write(strokeIdHostOrder);

    handle_CanvasDrawingCommand(std::move(cds), sessionIdHostOrder);
}

// ============================================================
// handle_CanvasDrawingCommand
//
// Enforces per-stroke ordering: START_STROKE must be dispatched
// before any ADD_POINT or END_STROKE with the same strokeId.
//
// If START_STROKE has not yet arrived for a given stroke, ADD_POINT
// and END_STROKE are buffered.  When START_STROKE arrives the buffer
// is flushed in sequence-number order, ensuring the game layer always
// sees: START_STROKE → ADD_POINTs (ordered) → END_STROKE.
// ============================================================

void Server::handle_CanvasDrawingCommand(CanvasDrawCommand cds, SessionId sessionIdHostOrder)
{
    uint64_t key = strokeKey(sessionIdHostOrder, cds._strokeId);

    std::lock_guard bufLock(_strokeBufferMutex);

    if(cds._type == MessageType::PF_START_STROKE)
    {
        StrokeBuffer& sb = _strokeBuffers[key];

        // Dispatch START_STROKE first.
        dispatchCanvasCommand(sessionIdHostOrder, cds);
        sb.started = true;

        // Flush any ADD_POINTs / END_STROKE that arrived early.
        // Sort by sequence number so ordering within the stroke is preserved.
        std::sort(sb.pending.begin(), sb.pending.end(),
            [](const CanvasDrawCommand& a, const CanvasDrawCommand& b)
            {
                return a._sqNumberHostOrder < b._sqNumberHostOrder;
            });

        bool endSeen = false;
        for(auto& buffered : sb.pending)
        {
            dispatchCanvasCommand(sessionIdHostOrder, buffered);
            if(buffered._type == MessageType::PF_END_STROKE)
                endSeen = true;
        }

        if(endSeen)
            _strokeBuffers.erase(key); // stroke is fully complete
        else
            sb.pending.clear();
    }
    else // ADD_POINT or END_STROKE
    {
        auto it = _strokeBuffers.find(key);
        if(it != _strokeBuffers.end() && it->second.started)
        {
            // START_STROKE already dispatched — send immediately.
            dispatchCanvasCommand(sessionIdHostOrder, cds);

            if(cds._type == MessageType::PF_END_STROKE)
                _strokeBuffers.erase(it);
        }
        else
        {
            // START_STROKE not yet seen — buffer this command.
            _strokeBuffers[key].pending.push_back(std::move(cds));
        }
    }
}

// ============================================================
// dispatchCanvasCommand  — fires all registered callbacks
// ============================================================

void Server::dispatchCanvasCommand(SessionId sessionIdHostOrder, const CanvasDrawCommand& cds)
{
    std::lock_guard lock(_cdsFnsMutex);
    for(const auto& [_, fn] : _cdsFns)
        fn(sessionIdHostOrder, cds);
}

// ============================================================
// handle_reqLogin
// ============================================================

void Server::handle_reqLogin(std::span<const char> body, sockaddr_in* sa)
{
    assert(body.size() == PacketSize::REQ_LOGIN - 1);

    char ipStr[INET_ADDRSTRLEN]{};
    inet_ntop(AF_INET, &sa->sin_addr, ipStr, INET_ADDRSTRLEN);
    std::string ipPort = std::format("{}:{}", ipStr, ntohs(sa->sin_port));

    {
        std::lock_guard lock(_clientStorageMutex);
        for(const auto& [_, client] : _sessionIdToClient)
        {
            if(client.ipPort == ipPort)
            {
                log(std::cout, std::format("[Server] {} already logged in, ignoring REQ_LOGIN", ipPort));
                return;
            }
        }
    }

    ByteReader rdr{.buffer = body};
    auto userBuf = rdr.read<std::array<char, MAX_USERNAME_LEN>>();
    auto passBuf = rdr.read<std::array<char, MAX_PASSWORD_LEN>>();
    std::string username(userBuf.data(), strnlen(userBuf.data(), MAX_USERNAME_LEN));
    std::string password(passBuf.data(), strnlen(passBuf.data(), MAX_PASSWORD_LEN));

    LoginStatus status;
    SessionId   sessionIdHostOrder = InvalidSessionId;

    if(_userStore.authenticate(username, password))
    {
        sessionIdHostOrder = getNextSessionIdHostOrder();
        status = LoginStatus::SUCCESS;
    }
    else
    {
        status = LoginStatus::INVALID_CREDENTIALS;
        log(std::cout, std::format("[Server] {} login failed for '{}'", ipPort, username));
    }

    std::vector<char> rspPkt(PacketSize::RSP_LOGIN);
    {
        ByteWriter wrt{.buffer = rspPkt};
        wrt.write(static_cast<char>(MessageType::RSP_LOGIN));
        wrt.write(htonl(sessionIdHostOrder));
        wrt.write(static_cast<char>(status));
    }

    bool sent = false;
    for(int i = 0; i < _maxRetry && !sent; ++i)
    {
        int n = sendto(_socket, rspPkt.data(), static_cast<int>(rspPkt.size()),
            0, reinterpret_cast<sockaddr*>(sa), sizeof(*sa));
        if(n != SOCKET_ERROR)                             sent = true;
        else if(!isRecoverableWSAError(WSAGetLastError())) break;
    }

    if(sent && status == LoginStatus::SUCCESS)
    {
        std::lock_guard lock(_clientStorageMutex);
        auto [_, ok] = _sessionIdToClient.emplace(sessionIdHostOrder,
            Client{.ipPort = ipPort, .sa = *sa});
        assert(ok && "Duplicate session id — logic error");
        log(std::cout, std::format("[Server] {} '{}' logged in (session {})",
            ipPort, username, sessionIdHostOrder));
    }
    else if(!sent)
    {
        log(std::cerr, std::format("[Server] {} failed to send RSP_LOGIN", ipPort));
    }
}

// ============================================================
// handle_reqCreateAccount
// ============================================================

void Server::handle_reqCreateAccount(std::span<const char> body, sockaddr_in* sa)
{
    assert(body.size() == PacketSize::REQ_CREATE_ACCOUNT - 1);

    char ipStr[INET_ADDRSTRLEN]{};
    inet_ntop(AF_INET, &sa->sin_addr, ipStr, INET_ADDRSTRLEN);
    std::string ipPort = std::format("{}:{}", ipStr, ntohs(sa->sin_port));

    ByteReader rdr{.buffer = body};
    auto userBuf = rdr.read<std::array<char, MAX_USERNAME_LEN>>();
    auto passBuf = rdr.read<std::array<char, MAX_PASSWORD_LEN>>();
    std::string username(userBuf.data(), strnlen(userBuf.data(), MAX_USERNAME_LEN));
    std::string password(passBuf.data(), strnlen(passBuf.data(), MAX_PASSWORD_LEN));

    LoginStatus status;
    if(username.empty())
        status = LoginStatus::USERNAME_TOO_LONG;
    else if(_userStore.createAccount(username, password))
        status = LoginStatus::SUCCESS;
    else
        status = LoginStatus::USERNAME_TAKEN;

    log(std::cout, std::format("[Server] {} createAccount '{}': status {}",
        ipPort, username, static_cast<int>(status)));

    std::vector<char> rspPkt(PacketSize::RSP_LOGIN);
    {
        ByteWriter wrt{.buffer = rspPkt};
        wrt.write(static_cast<char>(MessageType::RSP_LOGIN));
        wrt.write(htonl(InvalidSessionId));
        wrt.write(static_cast<char>(status));
    }

    for(int i = 0; i < _maxRetry; ++i)
    {
        int n = sendto(_socket, rspPkt.data(), static_cast<int>(rspPkt.size()),
            0, reinterpret_cast<sockaddr*>(sa), sizeof(*sa));
        if(n != SOCKET_ERROR)                             break;
        if(!isRecoverableWSAError(WSAGetLastError()))     break;
    }
}

// ============================================================
// handle_reqUnregister
// ============================================================

void Server::handle_reqUnregister(std::span<const char> body, sockaddr_in* sa)
{
    assert(body.size() == PacketSize::REQ_UNREGISTER - 1);

    char ipStr[INET_ADDRSTRLEN]{};
    inet_ntop(AF_INET, &sa->sin_addr, ipStr, INET_ADDRSTRLEN);
    std::string ipPort = std::format("{}:{}", ipStr, ntohs(sa->sin_port));

    ByteReader rdr{.buffer = body};
    auto sessionIdHostOrder = ntohl(rdr.read<SessionId>());

    if(!destroySessionIdHostOrder(sessionIdHostOrder, ipPort))
    {
        log(std::cerr, std::format("[Server] {} REQ_UNREGISTER: session {} not found",
            ipPort, sessionIdHostOrder));
        return;
    }
    log(std::cout, std::format("[Server] {} disconnected", ipPort));
}

// ============================================================
// actualStartListening
// ============================================================

void Server::actualStartListening(std::stop_token st) noexcept
{
    std::vector<char> buf(MaxUdpPacketBytes);

    while(!st.stop_requested())
    {
        fd_set rs;
        FD_ZERO(&rs);
        FD_SET(_socket, &rs);
        timeval tv{0, static_cast<int>(_recvTimeOut * 1'000'000.0)};

        int ready = select(0, &rs, nullptr, nullptr, &tv);
        if(ready == SOCKET_ERROR)
        {
            log(std::cerr, std::format("[Server] select() failed: {}", wsaErrorStr()));
            break;
        }
        if(ready == 0) continue;

        sockaddr_in from{};
        int fromLen = sizeof(from);
        int n = recvfrom(_socket, buf.data(), static_cast<int>(buf.size()),
            0, reinterpret_cast<sockaddr*>(&from), &fromLen);
        if(n == SOCKET_ERROR)
        {
            log(std::cerr, std::format("[Server] recvfrom() failed: {}", wsaErrorStr()));
            break;
        }
        if(n == 0) continue;

        auto mid = static_cast<MessageType>(buf[0]);
        auto it = _messageTypeFns.find(mid);
        if(it != _messageTypeFns.end())
            (this->*it->second)(std::span<const char>(buf).subspan(1, n - 1), &from);
    }

    _threadFinished = true;
}

// ============================================================
// Session helpers
// ============================================================

SessionId Server::getNextSessionIdHostOrder()
{
    assert(_nextSessionIdHostOrder != InvalidSessionId);
    std::lock_guard lock(_clientStorageMutex);
    return _nextSessionIdHostOrder++;
}

bool Server::destroySessionIdHostOrder(SessionId id, const std::string& ipPort)
{
    std::lock_guard lock(_clientStorageMutex);
    auto it = _sessionIdToClient.find(id);
    if(it == _sessionIdToClient.end()) return false;
    if(it->second.ipPort != ipPort)
    {
        log(std::cerr, std::format("[Server] {} tried to destroy session {} which belongs to {}",
            ipPort, id, it->second.ipPort));
        return false;
    }
    _sessionIdToClient.erase(it);
    return true;
}

// ============================================================
// Game logic
// ============================================================

void Server::load_wordlist()
{
    std::ifstream file("resources/words.txt");
    if(!file.is_open())
        throw std::runtime_error("Failed to open resources/words.txt");

    std::string w;
    while(std::getline(file, w))
    {
        if(!w.empty() && w.back() == '\r') w.pop_back();
        if(!w.empty()) word_list.push_back(w);
    }

    auto it = std::max_element(std::execution::par,
        word_list.begin(), word_list.end(),
        [](const std::string& a, const std::string& b) { return a.size() < b.size(); });
    if(it != word_list.end())
        max_len = static_cast<uint32_t>(it->size());
}

void Server::pick_word()
{
    if(word_list.empty()) return;
    std::srand(static_cast<unsigned int>(std::time(nullptr)));
    word.second = word_list[std::rand() % word_list.size()];
    word.first = false;
    std::cout << word.second << '\n';
}

int Server::word_heuristic() { return 0; }
