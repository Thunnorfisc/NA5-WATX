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
// startListening
// ============================================================

void Client::startListening(std::stop_token st)
{
    std::vector<char> buf(MaxUdpPacketBytes);

    while (!st.stop_requested())
    {
        // send any commands
        auto now = std::chrono::steady_clock::now();
        if (auto lk = std::unique_lock(_bsestsMutex, std::try_to_lock); lk.owns_lock() && !_bsestsQueue.empty())
        {
            std::queue<BufferedStartEndStrokeToSend> cpyBsestsQueue;
            cpyBsestsQueue.swap(_bsestsQueue);
            lk.unlock();
            while (!cpyBsestsQueue.empty())
            {
                auto bsests = cpyBsestsQueue.front();
                cpyBsestsQueue.pop();
                sendto(_socket, bsests._data.data(), static_cast<int>(bsests._data.size()),
                    0, reinterpret_cast<sockaddr*>(&_serverAddr), sizeof(_serverAddr));

                // Add to pending with deadlines
                PendingBSESTS pending{
                    ._data = bsests._data,
                    ._nextSendTime = now + std::chrono::milliseconds(100),  // retry interval
                    ._giveUpTime = now + std::chrono::seconds(1),           // total wait
                };
                if (bsests._type == BufferedStartEndStrokeToSend::Type::START_STROKE)
                    _pendingStartStrokes[bsests._hostStrokeId] = std::move(pending);
                else if (bsests._type == BufferedStartEndStrokeToSend::Type::END_STROKE)
                    _pendingEndStrokes[bsests._hostStrokeId] = std::move(pending);
                else assert(false && "Unhandled BufferedStartEndStrokeToSend::Type in Client::startListening()");
            }
        }

        for (auto it = _pendingStartStrokes.begin(); it != _pendingStartStrokes.end();)
        {
            if (now >= it->second._giveUpTime) // no more retries, just give up
            {
                log(std::cerr, std::format("[Client] Start Stroke {} timed out", it->first));
                it = _pendingStartStrokes.erase(it);
                continue;
            }

            // didnt get back an ack, but exceed attempt time
            // so, send data again, and reset the attempt timer
            if (now >= it->second._nextSendTime)
            {
                sendto(_socket, it->second._data.data(), static_cast<int>(it->second._data.size()),
                    0, reinterpret_cast<sockaddr*>(&_serverAddr), sizeof(_serverAddr));
                it->second._nextSendTime = now + std::chrono::milliseconds(100);
            }
            // move on to the next pending
            ++it;
        }

        for (auto it = _pendingEndStrokes.begin(); it != _pendingEndStrokes.end();)
        {
            if (now >= it->second._giveUpTime) // no more retries, just give up
            {
                log(std::cerr, std::format("[Client] End Stroke {} timed out", it->first));
                it = _pendingEndStrokes.erase(it);
                continue;
            }

            // didnt get back an ack, but exceed attempt time
            // so, send data again, and reset the attempt timer
            if (now >= it->second._nextSendTime)
            {
                sendto(_socket, it->second._data.data(), static_cast<int>(it->second._data.size()),
                    0, reinterpret_cast<sockaddr*>(&_serverAddr), sizeof(_serverAddr));
                it->second._nextSendTime = now + std::chrono::milliseconds(100);
            }
            // move on to the next pending
            ++it;
        }

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
            while (true)
            {
                int n = recvfrom(_socket, recvBuf.data(), static_cast<int>(recvBuf.size()),
                    0, reinterpret_cast<sockaddr*>(&from), &fromLen);

                if (n == SOCKET_ERROR)
                {
                    if (WSAGetLastError() == WSAEWOULDBLOCK) break;
                    break;
                }
                if (n != static_cast<int>(PacketSize::RSP_LOGIN)) continue;
                if (static_cast<MessageType>(recvBuf[0]) != MessageType::RSP_LOGIN) continue;

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
            while (true)
            {
                int n = recvfrom(_socket, recvBuf.data(), static_cast<int>(recvBuf.size()),
                    0, reinterpret_cast<sockaddr*>(&from), &fromLen);
                if (n == SOCKET_ERROR)
                {
                    if (WSAGetLastError() == WSAEWOULDBLOCK) break;
                    break;
                }
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
    BufferedStartEndStrokeToSend bsests;
    bsests._type = BufferedStartEndStrokeToSend::Type::START_STROKE;
    bsests._data = std::move(msg);
    bsests._hostStrokeId = strokeid;
    std::lock_guard lock(_bsestsMutex);
    _bsestsQueue.push(std::move(bsests));
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
    BufferedStartEndStrokeToSend bsests;
    bsests._type = BufferedStartEndStrokeToSend::Type::END_STROKE;
    bsests._data = std::move(msg);
    bsests._hostStrokeId = strokeid;
    std::lock_guard lock(_bsestsMutex);
    _bsestsQueue.push(std::move(bsests));
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
