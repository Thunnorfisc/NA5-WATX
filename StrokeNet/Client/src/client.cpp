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
// initalize / terminate
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

    log(std::cout, std::format("[Client] Ready at {}:{}", _ip, _portHostOrder));
}

void Client::terminate()
{
    if(_socket != INVALID_SOCKET) closesocket(_socket);
    WSACleanup();
}

// ============================================================
// startListening
// ============================================================

void Client::startListening(std::stop_token st)
{
    std::vector<char> buf(MaxUdpPacketBytes);

    while(!st.stop_requested())
    {
        fd_set rs;
        FD_ZERO(&rs);
        FD_SET(_socket, &rs);
        timeval tv{0, static_cast<long>(_recvTimeOut * 1'000'000.0)};

        int ready = select(0, &rs, nullptr, nullptr, &tv);
        if(ready == SOCKET_ERROR) { log(std::cerr, "[Client] listen select() failed"); return; }
        if(ready == 0) continue;

        sockaddr_in from{};
        int fromLen = sizeof(from);
        int n = recvfrom(_socket, buf.data(), static_cast<int>(buf.size()),
            0, reinterpret_cast<sockaddr*>(&from), &fromLen);
        if(n <= 0) continue;

        auto mid = static_cast<MessageType>(buf[0]);
        auto it = _listenMsgFns.find(mid);
        if(it != _listenMsgFns.end())
            (it->second)(std::span<const char>(buf).subspan(1, n - 1));
    }
}

// ============================================================
// startSending
// ============================================================

void Client::startSending(std::stop_token st)
{
    while(!st.stop_requested())
    {
        // Drain outbound queue
        {
            std::lock_guard lock(_sendQueueMutex);
            while(!_sendQueue.empty())
            {
                auto& [pkt, dest] = _sendQueue.front();
                doSend(pkt, dest);
                _sendQueue.pop();
            }
        }

        // Retransmit timed-out reliable packets
        {
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
                            "[Client] Reliable packet seq={} dropped after {} attempts",
                            it->first, pr.attempts));
                        it = _pendingAcks.erase(it);
                        continue;
                    }

                    log(std::cout, std::format(
                        "[Client] Retransmitting seq={} (attempt {})", it->first, pr.attempts + 1));

                    doSend(pr.packet, pr.dest);
                    pr.lastSentAt = now;
                    pr.attempts++;
                }
                ++it;
            }
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
}

// ============================================================
// sendCanvasCommand  — called from the game/UI thread
//
// Packet wire layouts (network byte order):
//   START_STROKE : [type1][session4][seq4][strokeId4][mousePos4][RGBAT5]  = 22
//   ADD_POINT    : [type1][session4][seq4][strokeId4][mousePos4]          = 17
//   END_STROKE   : [type1][session4][seq4][strokeId4]                     = 13
// ============================================================

void Client::sendCanvasCommand(const CanvasCommandSend& cmd)
{
    if(cmd._type == CanvasCommandSend::Type::START_STROKE)
    {
        // _data: [strokeId u32][mousePos u16x2][RGBAT 5] = 13 bytes
        assert(cmd._data.size() == PacketSize::PF_START_STROKE - PacketSize::HEADER_SIZE);

        SequenceNumber seq = allocSeq();
        std::vector<char> pkt(PacketSize::PF_START_STROKE);
        ByteWriter wrt{.buffer = pkt};

        wrt.write(static_cast<char>(MessageType::PF_START_STROKE));
        wrt.write(htonl(_sessionId));
        wrt.write(htonl(seq));

        ByteReader rdr{.buffer = cmd._data};
        wrt.write(htonl(rdr.read<std::uint32_t>())); // strokeId
        auto mp = rdr.read<MousePosition>();
        mp[0] = htons(mp[0]);
        mp[1] = htons(mp[1]);
        wrt.write(mp);
        wrt.write(rdr.read<std::array<char, 5>>()); // RGBAT

        sendReliable(seq, std::move(pkt));
    }
    else if(cmd._type == CanvasCommandSend::Type::ADD_POINT)
    {
        // _data: [strokeId u32][mousePos u16x2] = 8 bytes
        assert(cmd._data.size() == PacketSize::PF_ADD_POINT - PacketSize::HEADER_SIZE);

        std::vector<char> pkt(PacketSize::PF_ADD_POINT);
        ByteWriter wrt{.buffer = pkt};

        wrt.write(static_cast<char>(MessageType::PF_ADD_POINT));
        wrt.write(htonl(_sessionId));
        wrt.write(htonl(allocSeq()));

        ByteReader rdr{.buffer = cmd._data};
        wrt.write(htonl(rdr.read<std::uint32_t>())); // strokeId
        auto mp = rdr.read<MousePosition>();
        mp[0] = htons(mp[0]);
        mp[1] = htons(mp[1]);
        wrt.write(mp);

        sendUnreliable(std::move(pkt));
    }
    else if(cmd._type == CanvasCommandSend::Type::END_STROKE)
    {
        // _data: [strokeId u32] = 4 bytes
        assert(cmd._data.size() == PacketSize::PF_END_STROKE - PacketSize::HEADER_SIZE);

        SequenceNumber seq = allocSeq();
        std::vector<char> pkt(PacketSize::PF_END_STROKE);
        ByteWriter wrt{.buffer = pkt};

        wrt.write(static_cast<char>(MessageType::PF_END_STROKE));
        wrt.write(htonl(_sessionId));
        wrt.write(htonl(seq));

        ByteReader rdr{.buffer = cmd._data};
        wrt.write(htonl(rdr.read<std::uint32_t>())); // strokeId

        sendReliable(seq, std::move(pkt));
    }
}

// ============================================================
// sendReliable / sendUnreliable / doSend
// ============================================================

void Client::sendReliable(SequenceNumber seq, std::vector<char> packet)
{
    {
        std::lock_guard lock(_pendingAcksMutex);
        PendingReliable pr;
        pr.packet = packet;
        pr.dest = _serverAddr;
        pr.lastSentAt = std::chrono::steady_clock::now() -
            std::chrono::duration_cast<std::chrono::steady_clock::duration>(
                std::chrono::duration<double>(RetransmitIntervalSec));
        pr.attempts = 0;
        _pendingAcks[seq] = std::move(pr);
    }
    sendUnreliable(std::move(packet));
}

void Client::sendUnreliable(std::vector<char> packet)
{
    std::lock_guard lock(_sendQueueMutex);
    _sendQueue.push({std::move(packet), _serverAddr});
}

void Client::doSend(const std::vector<char>& packet, const sockaddr_in& dest)
{
    sendto(_socket, packet.data(), static_cast<int>(packet.size()),
        0, reinterpret_cast<const sockaddr*>(&dest), sizeof(dest));
}

// ============================================================
// handle_Ack
// ============================================================

void Client::handle_Ack(std::span<const char> body)
{
    if(body.size() < sizeof(SequenceNumber)) return;

    ByteReader rdr{.buffer = body};
    SequenceNumber seq = ntohl(rdr.read<SequenceNumber>());

    std::lock_guard lock(_pendingAcksMutex);
    auto it = _pendingAcks.find(seq);
    if(it != _pendingAcks.end())
    {
        log(std::cout, std::format("[Client] ACK received for seq={}", seq));
        _pendingAcks.erase(it);
    }
}

// ============================================================
// Stroke ordering helpers
// ============================================================

// Must be called with BOTH _strokeBufferMutex AND _canvasCommandMutex held.
void Client::pushCommand(CanvasCommandRecv cmd)
{
    _canvasCommands.push(std::move(cmd));
}

// ============================================================
// handle_StartStroke
//
// Packet body: [sessionId 4][serverSeq 4][strokeId 4][mousePos 4][RGBAT 5] = 21 bytes
//
// Ordering:
//   1. Dispatch START_STROKE to _canvasCommands.
//   2. Flush any buffered ADD_POINT / END_STROKE for this strokeId
//      (sorted by server sequence number).
// ============================================================

void Client::handle_StartStroke(std::span<const char> body)
{
    assert(body.size() == PacketSize::PF_START_STROKE - 1);

    ByteReader rdr{.buffer = body};
    if(ntohl(rdr.read<SessionId>()) != _sessionId) return; // not for us

    auto serverSeq = ntohl(rdr.read<SequenceNumber>());
    auto strokeId = ntohl(rdr.read<std::uint32_t>());
    auto mousePos = rdr.read<MousePosition>();
    auto RGBAT = rdr.read<std::array<char, 5>>();
    mousePos[0] = ntohs(mousePos[0]);
    mousePos[1] = ntohs(mousePos[1]);

    // Build the received command — _data: [strokeId u32][mousePos u16x2][RGBAT 5] = 13 bytes
    CanvasCommandRecv ccr;
    ccr._type = CanvasCommandRecv::Type::START_STROKE;
    ccr._serverSeqNumber = serverSeq;
    ccr._strokeId = strokeId;
    ccr._data.resize(PacketSize::PF_START_STROKE - PacketSize::HEADER_SIZE);
    {
        ByteWriter wrt{.buffer = ccr._data};
        wrt.write(strokeId);
        wrt.write(mousePos);
        wrt.write(RGBAT);
    }

    // Lock ordering: _strokeBufferMutex before _canvasCommandMutex (always).
    std::lock_guard bufLock(_strokeBufferMutex);
    std::lock_guard cmdLock(_canvasCommandMutex);

    // Dispatch START_STROKE first.
    pushCommand(std::move(ccr));

    // Mark stroke as started and flush any early-arriving commands.
    StrokeBuffer& sb = _strokeBuffers[strokeId];
    sb.started = true;

    std::sort(sb.pending.begin(), sb.pending.end(),
        [](const CanvasCommandRecv& a, const CanvasCommandRecv& b)
        {
            return a._serverSeqNumber < b._serverSeqNumber;
        });

    bool endSeen = false;
    for(auto& buffered : sb.pending)
    {
        if(buffered._type == CanvasCommandRecv::Type::END_STROKE)
            endSeen = true;
        pushCommand(std::move(buffered));
    }

    if(endSeen)
        _strokeBuffers.erase(strokeId); // stroke fully complete
    else
        sb.pending.clear();
}

// ============================================================
// handle_AddPoint  — unreliable
//
// Packet body: [sessionId 4][serverSeq 4][strokeId 4][mousePos 4] = 16 bytes
//
// If START_STROKE for this strokeId has already been dispatched,
// push immediately.  Otherwise buffer until it arrives.
// ============================================================

void Client::handle_AddPoint(std::span<const char> body)
{
    assert(body.size() == PacketSize::PF_ADD_POINT - 1);

    ByteReader rdr{.buffer = body};
    if(ntohl(rdr.read<SessionId>()) != _sessionId) return;

    auto serverSeq = ntohl(rdr.read<SequenceNumber>());
    auto strokeId = ntohl(rdr.read<std::uint32_t>());
    auto mousePos = rdr.read<MousePosition>();
    mousePos[0] = ntohs(mousePos[0]);
    mousePos[1] = ntohs(mousePos[1]);

    // Build the received command — _data: [strokeId u32][mousePos u16x2] = 8 bytes
    CanvasCommandRecv ccr;
    ccr._type = CanvasCommandRecv::Type::ADD_POINT;
    ccr._serverSeqNumber = serverSeq;
    ccr._strokeId = strokeId;
    ccr._data.resize(PacketSize::PF_ADD_POINT - PacketSize::HEADER_SIZE);
    {
        ByteWriter wrt{.buffer = ccr._data};
        wrt.write(strokeId);
        wrt.write(mousePos);
    }

    std::lock_guard bufLock(_strokeBufferMutex);

    auto it = _strokeBuffers.find(strokeId);
    if(it != _strokeBuffers.end() && it->second.started)
    {
        std::lock_guard cmdLock(_canvasCommandMutex);
        pushCommand(std::move(ccr));
    }
    else
    {
        // START_STROKE not yet received — hold this point.
        _strokeBuffers[strokeId].pending.push_back(std::move(ccr));
    }
}

// ============================================================
// handle_EndStroke
//
// Packet body: [sessionId 4][serverSeq 4][strokeId 4] = 12 bytes
//
// Same buffering logic as ADD_POINT.
// ============================================================

void Client::handle_EndStroke(std::span<const char> body)
{
    assert(body.size() == PacketSize::PF_END_STROKE - 1);

    ByteReader rdr{.buffer = body};
    if(ntohl(rdr.read<SessionId>()) != _sessionId) return;

    auto serverSeq = ntohl(rdr.read<SequenceNumber>());
    auto strokeId = ntohl(rdr.read<std::uint32_t>());

    // Build the received command — _data: [strokeId u32] = 4 bytes
    CanvasCommandRecv ccr;
    ccr._type = CanvasCommandRecv::Type::END_STROKE;
    ccr._serverSeqNumber = serverSeq;
    ccr._strokeId = strokeId;
    ccr._data.resize(PacketSize::PF_END_STROKE - PacketSize::HEADER_SIZE);
    {
        ByteWriter wrt{.buffer = ccr._data};
        wrt.write(strokeId);
    }

    std::lock_guard bufLock(_strokeBufferMutex);

    auto it = _strokeBuffers.find(strokeId);
    if(it != _strokeBuffers.end() && it->second.started)
    {
        {
            std::lock_guard cmdLock(_canvasCommandMutex);
            pushCommand(std::move(ccr));
        }
        _strokeBuffers.erase(it); // stroke complete
    }
    else
    {
        // START_STROKE not yet received — buffer END_STROKE.
        _strokeBuffers[strokeId].pending.push_back(std::move(ccr));
    }
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

    std::vector<char> sendPkt(PacketSize::REQ_LOGIN, '\0');
    {
        ByteWriter wrt{.buffer = sendPkt};
        wrt.write(static_cast<char>(MessageType::REQ_LOGIN));
        std::array<char, MAX_USERNAME_LEN> u{};
        std::memcpy(u.data(), username.c_str(), username.size());
        wrt.write(u);
        std::array<char, MAX_PASSWORD_LEN> p{};
        std::memcpy(p.data(), password.c_str(), password.size());
        wrt.write(p);
    }

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
            int n = recvfrom(_socket, recvBuf.data(), static_cast<int>(recvBuf.size()),
                0, reinterpret_cast<sockaddr*>(&from), &fromLen);
            if(n != static_cast<int>(PacketSize::RSP_LOGIN)) continue;
            if(static_cast<MessageType>(recvBuf[0]) != MessageType::RSP_LOGIN) continue;

            ByteReader rdr{.buffer = std::span<const char>(recvBuf).subspan(1, n - 1)};
            SessionId   sid = ntohl(rdr.read<SessionId>());
            LoginStatus status = static_cast<LoginStatus>(rdr.read<std::uint8_t>());

            if(status == LoginStatus::SUCCESS)
            {
                _sessionId = sid;
                _serverAddr = from;
                char ip[INET_ADDRSTRLEN]{};
                inet_ntop(AF_INET, &from.sin_addr, ip, sizeof(ip));
                _serverIpAndPort = std::format("{}:{}", ip, ntohs(from.sin_port));
                _nextSeq.store(0);

                _stopSource = std::stop_source{};
                _listeningThread = std::jthread([](std::stop_token st) { startListening(st); }, _stopSource.get_token());
                _sendingThread = std::jthread([](std::stop_token st) { startSending(st);   }, _stopSource.get_token());

                log(std::cout, std::format("[Client] Login OK — session {}, server {}",
                    _sessionId, _serverIpAndPort));
            }
            return status;
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

    std::vector<char> sendPkt(PacketSize::REQ_CREATE_ACCOUNT, '\0');
    {
        ByteWriter wrt{.buffer = sendPkt};
        wrt.write(static_cast<char>(MessageType::REQ_CREATE_ACCOUNT));
        std::array<char, MAX_USERNAME_LEN> u{};
        std::memcpy(u.data(), username.c_str(), username.size());
        wrt.write(u);
        std::array<char, MAX_PASSWORD_LEN> p{};
        std::memcpy(p.data(), password.c_str(), password.size());
        wrt.write(p);
    }

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
            int n = recvfrom(_socket, recvBuf.data(), static_cast<int>(recvBuf.size()),
                0, reinterpret_cast<sockaddr*>(&from), &fromLen);
            if(n != static_cast<int>(PacketSize::RSP_LOGIN)) continue;
            if(static_cast<MessageType>(recvBuf[0]) != MessageType::RSP_LOGIN) continue;

            ByteReader rdr{.buffer = std::span<const char>(recvBuf).subspan(1, n - 1)};
            [[maybe_unused]] SessionId sid = ntohl(rdr.read<SessionId>());
            LoginStatus status = static_cast<LoginStatus>(rdr.read<std::uint8_t>());
            log(std::cout, std::format("[Client] createAccount for '{}': status {}",
                username, static_cast<int>(status)));
            return status;
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
            if(_sendingThread.joinable())   _sendingThread.join();
        };

    if(_sessionId == InvalidSessionId) { stopThreads(); return; }

    std::vector<char> pkt(PacketSize::REQ_UNREGISTER);
    ByteWriter wrt{.buffer = pkt};
    wrt.write(static_cast<char>(MessageType::REQ_UNREGISTER));
    wrt.write(htonl(_sessionId));

    sendto(_socket, pkt.data(), static_cast<int>(pkt.size()),
        0, reinterpret_cast<sockaddr*>(&_serverAddr), sizeof(_serverAddr));

    log(std::cout, std::format("[Client] Disconnected from {}", _serverIpAndPort));
    _serverIpAndPort.clear();
    std::memset(&_serverAddr, 0, sizeof(_serverAddr));
    _sessionId = InvalidSessionId;

    stopThreads();
}
