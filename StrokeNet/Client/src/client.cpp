#include "client.hpp"

#include <array>
#include <mutex>
#include <vector>
#include <format>
#include <chrono>
#include <cassert>
#include <ostream>
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
    std::mutex s_ostreamMutex;
    void threadSafeOStream(std::ostream& os, std::string_view msg)
    {
        std::lock_guard lock(s_ostreamMutex);
        os << msg << '\n';
    }
}
void Client::initalize()
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
        std::string err = std::format("[Client] WSAStartup failed: {}", iResult);
        throw std::runtime_error(err);
    }

    _socket = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (_socket == INVALID_SOCKET)
        throw std::runtime_error(std::format("[Client] socket() failed, unable to set up udp socket: {}", wsaErrorStr()));

    sockaddr_in addr{};
    addr.sin_family = AF_INET; // ipv4
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(0);

    if (bind(_socket, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) == SOCKET_ERROR)
        throw std::runtime_error(std::format("[Client] bind() failed, unable to set up udp socket: {}", wsaErrorStr()));

    // get port
    sockaddr_in boundAddr{};
    int boundAddrLen = sizeof(boundAddr);
    if (getsockname(_socket, reinterpret_cast<sockaddr*>(&boundAddr), &boundAddrLen) == SOCKET_ERROR)
        throw std::runtime_error(std::format("[Client] getsockname() failed, unable to setup udp socket: {}", wsaErrorStr()));

    _portHostOrder = ntohs(boundAddr.sin_port);

    // get machine host name
    char hostName[256]{};
    if (gethostname(hostName, sizeof(hostName)) == SOCKET_ERROR)
        throw std::runtime_error(std::format("[Client] gethostname() failed, unable to setup udp socket: {}", wsaErrorStr()));

    addrinfo hints{};
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_DGRAM;
    hints.ai_protocol = IPPROTO_UDP;

    addrinfo* result = nullptr;
    if (getaddrinfo(hostName, nullptr, &hints, &result) != 0)
        throw std::runtime_error(std::format("[Client] getaddrinfo() failed, unable to setup udp socket"));

    char ipStr[INET_ADDRSTRLEN]{};
    auto* ipv4 = reinterpret_cast<sockaddr_in*>(result->ai_addr);
    inet_ntop(AF_INET, &(ipv4->sin_addr), ipStr, sizeof(ipStr));
    _ip = ipStr;
    freeaddrinfo(result);

    // allow sending to all LAN devices
    int broadcast = 1;
    setsockopt(_socket, SOL_SOCKET, SO_BROADCAST,
        reinterpret_cast<const char*>(&broadcast), sizeof(broadcast));

    threadSafeOStream(std::cout, std::format("[Client]: {}:{}", _ip, _portHostOrder));
}

void Client::terminate()
{
    if (_socket != INVALID_SOCKET) closesocket(_socket);
    WSACleanup();
}

void Client::startListening(std::stop_token st)
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
        timeout.tv_usec = static_cast<long>(_recvTimeOut * 1'000'000.0);
        int ready = select(
            0,
            &readSet,
            nullptr,
            nullptr,
            &timeout
        );

        if (ready == SOCKET_ERROR)
        {
            threadSafeOStream(std::cerr, std::format("[Client] select() failed: {}", wsaErrorStr()));
            return;
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
                threadSafeOStream(std::cerr, std::format("[Client] recvfrom() failed: {}", wsaErrorStr()));
                return;
            }
            else if (bytesReceived == 0) continue; // empty datagram, move on

            MessageType message = static_cast<MessageType>(udpPacket[0]);
            auto it = _listenMsgFns.find(message);
            if (it != _listenMsgFns.end())
            {
                (it->second)(std::span<const char>(udpPacket).subspan(1, bytesReceived -1));
            }
        }
    }
}

bool Client::connectViaBroadcast()
{
    if (_sessionId != InvalidSessionId)
    {
        threadSafeOStream(std::cerr, "[Client] Tried to connect while there is already an active connection");
        return false;
    }

    if (_listeningThread.joinable())
    {
        threadSafeOStream(std::cerr, "[Client] Tried to connect while there is already an active connection");
        return false;
    }

    sockaddr_in broadcastAddr{};
    broadcastAddr.sin_family = AF_INET;
    broadcastAddr.sin_port = htons(ServerUdpPort);
    broadcastAddr.sin_addr.S_un.S_addr = INADDR_BROADCAST;

    std::vector<char> udpPacket;
    udpPacket.resize(MaxUdpPacketBytes);

    for (int attempt = 0; attempt < _maxRetries; attempt++)
    {
        const char mid = static_cast<char>(static_cast<std::uint8_t>(MessageType::REQ_REGISTER));
        int sentBytes = sendto
        (
            _socket,
            &mid,
            1,
            0,
            reinterpret_cast<sockaddr*>(&broadcastAddr),
            sizeof(broadcastAddr)
        );

        if (sentBytes == SOCKET_ERROR)
        {
            const int err = WSAGetLastError();
            if (isRecoverableWSAError(err)) continue; // retry
            else
            {
                threadSafeOStream(std::cerr,
                    std::format("[Client] sendto() failed, unable to connect to server: {}", err));
                return false;
            }
        }

        auto deadline = std::chrono::steady_clock::now() +
            std::chrono::milliseconds(static_cast<int>(_recvTimeOut * 1000.0));
        while (true)
        {
            auto remaining = deadline - std::chrono::steady_clock::now();
            if (remaining.count() <= 0) break;

            fd_set readset;
            FD_ZERO(&readset);
            FD_SET(_socket, &readset);

            auto us = std::chrono::duration_cast<std::chrono::microseconds>(remaining);
            timeval timeout{};
            timeout.tv_sec = static_cast<long>(us.count() / 1'000'000);
            timeout.tv_usec = static_cast<long>(us.count() % 1'000'000);

            int ready = select(0, &readset, nullptr, nullptr, &timeout);
            if (ready == SOCKET_ERROR)
            {
                const int err = WSAGetLastError();
                if (isRecoverableWSAError(err)) continue; // retry
                else
                {
                    threadSafeOStream(std::cerr,
                        std::format("[Client] select() failed: {}", err));
                    return false;
                }
            }

            if (ready == 0) break; // timeout

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
                const int err = WSAGetLastError();
                if (isRecoverableWSAError(err)) continue; // retry
                else
                {

                    threadSafeOStream(std::cerr,
                        std::format("[Client] recvfrom() failed: {}", err));
                    return false;
                }
            }

            else if (bytesReceived == 0) continue;

            if (bytesReceived != PacketSize::RSP_REGISTER) continue;
            if (static_cast<MessageType>(udpPacket[0]) != MessageType::RSP_REGISTER) continue;

            ByteReader rdr{ .buffer = std::span<const char>(udpPacket).subspan(1, bytesReceived - 1) };
            _sessionId = ntohl(rdr.read<SessionId>());

            _serverAddr = from;
            char ipStr[INET_ADDRSTRLEN]{};
            inet_ntop(AF_INET, &from.sin_addr, ipStr, sizeof(ipStr));
            _serverIpAndPort = std::format("{}:{}", ipStr, ntohs(from.sin_port));

            _listeningThread = std::jthread([st = _stopSource.get_token()]()
                {
                    startListening(st);
                });

            threadSafeOStream(std::cout, std::format("[Client] Successfully connected to server: {}", _serverIpAndPort));
            return true;
        }
    }
    return false;
}

bool Client::connectViaIpAndPort(const std::string& serverIp, const std::string& serverPort)
{
    if (_sessionId != InvalidSessionId)
    {
        threadSafeOStream(std::cerr, "[Client] Tried to connect while there is already an active connection");
        return false;
    }

    if (_listeningThread.joinable())
    {
        threadSafeOStream(std::cerr, "[Client] Tried to connect while there is already an active connection");
        return false;
    }

    std::uint16_t portHostOrder;
    try
    {
        portHostOrder = std::stoi(serverPort);
        if (portHostOrder > 65535) throw std::runtime_error("Invalid port range");
    }
    catch (...)
    {
        threadSafeOStream(std::cerr, std::format("[Client] Tried to connect with an invalid port, port {} is not a valid number", serverPort));
        return false;
    }
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(portHostOrder);
    if (inet_pton(AF_INET, serverIp.c_str(), &addr.sin_addr) != 1)
    {
        threadSafeOStream(std::cerr, std::format("[Client] Tried to connect with an invalid ip {}", serverIp));
        return false;
    }
    _serverAddr = addr;
    std::vector<char> udpPacket;
    udpPacket.resize(MaxUdpPacketBytes);
    for (int attempt = 0; attempt < _maxRetries; ++attempt)
    {
        const char mid = static_cast<char>(static_cast<std::uint8_t>(MessageType::REQ_REGISTER));

        int sentBytes = sendto(
            _socket,
            &mid,
            1,
            0,
            reinterpret_cast<sockaddr*>(&_serverAddr),
            sizeof(_serverAddr)
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
                std::format("[Client] sendto() failed: {}", wsaErrorStr())
            );
            return false;
        }

        assert(sentBytes == 1 && "sendto() should send exactly 1 byte for REQ_REGISTER");

        fd_set readSet;
        FD_ZERO(&readSet);
        FD_SET(_socket, &readSet);

        timeval timeout{};
        timeout.tv_sec = 0;
        timeout.tv_usec = static_cast<int>(_recvTimeOut * 1000);

        int ready = select(
            0,
            &readSet,
            nullptr,
            nullptr,
            &timeout
        );

        if (ready == SOCKET_ERROR)
        {
            threadSafeOStream(
                std::cerr,
                std::format("[Client] select() failed: {}", wsaErrorStr())
            );
            return false;
        }

        if (ready == 0)
        {
            continue; // timeout, retry next attempt
        }

        if (!FD_ISSET(_socket, &readSet))
        {
            continue; // defensive: treat as failed attempt
        }

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
            threadSafeOStream(
                std::cerr,
                std::format("[Client] recvfrom() failed: {}", wsaErrorStr())
            );
            return false;
        }

        if (bytesReceived == 0)
        {
            continue; // empty datagram, treat as failed attempt
        }

        // Validate sender.
        if (from.sin_family != AF_INET ||
            from.sin_port != addr.sin_port ||
            from.sin_addr.s_addr != addr.sin_addr.s_addr)
        {
            continue; // packet not from the server we are registering with
        }

        // Validate payload size
        if (bytesReceived != PacketSize::RSP_REGISTER)
        {
            continue;
        }

        const auto receivedType =
            static_cast<MessageType>(static_cast<std::uint8_t>(udpPacket[0]));

        if (receivedType != MessageType::RSP_REGISTER)
        {
            continue; // not the response we were waiting for
        }

        // okay we successfully received rsp_register, now to get the session id
        ByteReader rdr{ .buffer = std::span<const char>(udpPacket).subspan(1, bytesReceived) };
        _sessionId = ntohl(rdr.read<SessionId>());
        _serverIpAndPort = std::format("{}:{}", serverIp, serverPort);
        _listeningThread = std::jthread([st = _stopSource.get_token()]()
            {
                startListening(st);
            });
        threadSafeOStream(std::cout, std::format("[Client] Successfully connected to {} after {} attempts!", _serverIpAndPort, attempt + 1));
        return true;
    }
    return false;
}

void Client::disconnect()
{
    auto cleanUpThread = []()
        {
            if (_listeningThread.joinable())
            {
                _stopSource.request_stop();
                _listeningThread.join();
            }
        };

    if (_sessionId == InvalidSessionId)
    {
        threadSafeOStream(std::cerr,
            "[Client] Disconnect called when there is not a valid session id");
        cleanUpThread();
        return;
    }
    std::vector<char> msg;
    msg.resize(PacketSize::REQ_UNREGISTER);

    ByteWriter wrt{ .buffer = msg };
    wrt.write(static_cast<char>(MessageType::REQ_UNREGISTER));
    wrt.write(htonl(_sessionId));
    for (int attempt = 0; attempt < _maxRetries; ++attempt)
    {
        int sentBytes = sendto(
            _socket,
            msg.data(),
            static_cast<int>(msg.size()),
            0,
            reinterpret_cast<sockaddr*>(&_serverAddr),
            sizeof(_serverAddr)
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
                std::format("[Client] sendto() failed, disconnecting from the server had issues: {}", wsaErrorStr())
            );
            cleanUpThread();
            return;
        }
        else
        {
            // success
            threadSafeOStream(
                std::cout,
                std::format("[Client] Successfully disconnected from Server {}", _serverIpAndPort));
            _serverIpAndPort.clear();
            std::memset(&_serverAddr, 0, sizeof(_serverAddr));
            _sessionId = InvalidSessionId;

            cleanUpThread();

            return;
        }
    }
    threadSafeOStream(
        std::cerr,
        std::format("[Client] Disconnecting from the server had issues"));
    cleanUpThread();
}

void Client::sendInputState(const InputState& inputState)
{
    std::vector<char> msg;
    msg.resize(PacketSize::PF_INPUT_STATE);

    MousePosition mousePosNetworkOrder = inputState.currentMousePos;
    mousePosNetworkOrder[0] = htons(mousePosNetworkOrder[0]);
    mousePosNetworkOrder[1] = htons(mousePosNetworkOrder[1]);

    ByteWriter wrt{ .buffer = msg };
    wrt.write(static_cast<char>(MessageType::PF_INPUT_STATE));
    wrt.write(htonl(_sessionId));
    wrt.write(htonl(inputState.currentSequenceNumber));
    wrt.write(htonl(inputState.currentInput));
    wrt.write(mousePosNetworkOrder);
    for (int attempt = 0; attempt < _maxRetries; ++attempt)
    {
        int sentBytes = sendto(
            _socket,
            msg.data(),
            static_cast<int>(msg.size()),
            0,
            reinterpret_cast<sockaddr*>(&_serverAddr),
            sizeof(_serverAddr)
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
                std::format("[Client] sendto() failed: {}", wsaErrorStr())
            );
            return;
        }
        else
        {
            return; // success
        }
    }
    threadSafeOStream(std::cerr,
        std::format("[Client] Failed to send input state for sequence {}",
            inputState.currentSequenceNumber));
    return;
}

void Client::sendCanvasCommand(const CanvasDrawState& drawState)
{
    //drawState.assertCanvasDrawState();
    std::vector<char> msg;
    MessageType type = drawState._type;
    if (type == MessageType::PF_START_STROKE)
    {
        assert((drawState._msg.size() == PacketSize::PF_START_STROKE - PacketSize::HEADER_SIZE)
            && "Size is wrong when sending start stroke");
        msg.resize(PacketSize::PF_START_STROKE);

        auto sessionIdNetworkOrder = htonl(_sessionId);
        auto seqNumberNetworkOrder = htonl(drawState._sqNumberHostOrder);

        ByteReader rdr{ .buffer = drawState._msg };
        auto strokeIdNetworkOrder = htonl(rdr.read<std::uint32_t>());
        auto mousePositionNetworkOrder = rdr.read<MousePosition>();
        auto RGBAT = rdr.read<std::array<char, 5>>();

        mousePositionNetworkOrder[0] = htons(mousePositionNetworkOrder[0]);
        mousePositionNetworkOrder[1] = htons(mousePositionNetworkOrder[1]);

        ByteWriter wrt{ .buffer = msg };
        wrt.write(static_cast<char>(MessageType::PF_START_STROKE));
        wrt.write(sessionIdNetworkOrder);
        wrt.write(seqNumberNetworkOrder);
        wrt.write(strokeIdNetworkOrder);
        wrt.write(mousePositionNetworkOrder);
        wrt.write(RGBAT);
    }
    else if (type == MessageType::PF_ADD_POINT)
    {
        assert((drawState._msg.size() == PacketSize::PF_ADD_POINT - PacketSize::HEADER_SIZE)
            && "Size is wrong when sending add point");
        msg.resize(PacketSize::PF_ADD_POINT);

        auto sessionIdNetworkOrder = htonl(_sessionId);
        auto seqNumberNetworkOrder = htonl(drawState._sqNumberHostOrder);

        ByteReader rdr{ .buffer = drawState._msg };
        auto mousePositionNetworkOrder = rdr.read<MousePosition>();
        mousePositionNetworkOrder[0] = htons(mousePositionNetworkOrder[0]);
        mousePositionNetworkOrder[1] = htons(mousePositionNetworkOrder[1]);

        ByteWriter wrt{ .buffer = msg };
        wrt.write(static_cast<char>(MessageType::PF_ADD_POINT));
        wrt.write(sessionIdNetworkOrder);
        wrt.write(seqNumberNetworkOrder);
        wrt.write(mousePositionNetworkOrder);
    }
    else if (type == MessageType::PF_END_STROKE)
    {
        assert((drawState._msg.size() == PacketSize::PF_END_STROKE - PacketSize::HEADER_SIZE)
            && "Size is wrong when sending end stroke");
        msg.resize(PacketSize::PF_END_STROKE);

        auto sessionIdNetworkOrder = htonl(_sessionId);
        auto seqNumberNetworkOrder = htonl(drawState._sqNumberHostOrder);

        ByteWriter wrt{ .buffer = msg };
        wrt.write(static_cast<char>(MessageType::PF_END_STROKE));
        wrt.write(sessionIdNetworkOrder);
        wrt.write(seqNumberNetworkOrder);
    }
    else assert(false && "sendCanvasCommand() received an invalid message type");

    for (int attempt = 0; attempt < _maxRetries; ++attempt)
    {
        int sentBytes = sendto(
            _socket,
            msg.data(),
            static_cast<int>(msg.size()),
            0,
            reinterpret_cast<sockaddr*>(&_serverAddr),
            sizeof(_serverAddr)
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
                std::format("[Client] sendto() failed: {}", wsaErrorStr())
            );
            return;
        }
        else
        {
            return;
        }
    }
    threadSafeOStream(std::cerr,
        std::format("[Client] Failed to send canvas state for sequence {}",
            drawState._sqNumberHostOrder));
    return;
}

Client::RegCanvasStateFnId Client::registerCanvasStateCommandEvent(std::function<void(const CanvasDrawState&)> fn)
{
    std::lock_guard lock(_canvasDrawStateFunctionsMutex);
    _canvasDrawStateFunctions.emplace(std::make_pair(_nextCanvasStateFnId,fn));
    return _nextCanvasStateFnId++;
}

void Client::deregisterCanvasStateCommandEvent(RegCanvasStateFnId id)
{
    std::lock_guard lock(_canvasDrawStateFunctionsMutex);
    auto it = _canvasDrawStateFunctions.find(id);
    if (it == _canvasDrawStateFunctions.end())
    {
        threadSafeOStream(std::cerr,
            std::format("[Client] Failed to deregister canvas state command event: {}",id));
        return;
    }
    _canvasDrawStateFunctions.erase(id);
}

void Client::handle_StartStroke(std::span<const char> msgWithoutMID)
{
    assert((msgWithoutMID.size() == PacketSize::PF_START_STROKE - 1)
        && "Size is wrong when receiving start stroke");

    CanvasDrawState cds;
    cds._type = MessageType::PF_START_STROKE;
    cds._msg.resize(PacketSize::PF_START_STROKE - PacketSize::HEADER_SIZE);
    ByteWriter wrt{ .buffer = cds._msg };

    ByteReader rdr{ .buffer = msgWithoutMID };
    auto sessionIdHostOrder = ntohl(rdr.read<SessionId>());

    // datagram not meant for us, ignore it
    if (sessionIdHostOrder != _sessionId) return;

    cds._sqNumberHostOrder = ntohl(rdr.read<SequenceNumber>());
    wrt.write(ntohl(rdr.read<std::uint32_t>())); // stroke id

    auto mousePositionHostOrder = rdr.read<MousePosition>();
    mousePositionHostOrder[0] = ntohs(mousePositionHostOrder[0]);
    mousePositionHostOrder[1] = ntohs(mousePositionHostOrder[1]);
    wrt.write(mousePositionHostOrder);

    wrt.write(rdr.read<std::array<char, 5>>());
    
    invokeCanvasDrawCallbacks(cds);
}

void Client::handle_AddPoint(std::span<const char> msgWithoutMID)
{
    assert((msgWithoutMID.size() == PacketSize::PF_ADD_POINT - 1)
        && "Size is wrong when receiving add point");

    CanvasDrawState cds;
    cds._type = MessageType::PF_ADD_POINT;
    cds._msg.resize(PacketSize::PF_ADD_POINT - PacketSize::HEADER_SIZE);
    ByteWriter wrt{ .buffer = cds._msg };

    ByteReader rdr{ .buffer = msgWithoutMID };
    auto sessionIdHostOrder = ntohl(rdr.read<SessionId>());

    // datagram not meant for us, ignore it
    if (sessionIdHostOrder != _sessionId) return;

    cds._sqNumberHostOrder = ntohl(rdr.read<SequenceNumber>());

    auto mousePositionHostOrder = rdr.read<MousePosition>();
    mousePositionHostOrder[0] = ntohs(mousePositionHostOrder[0]);
    mousePositionHostOrder[1] = ntohs(mousePositionHostOrder[1]);
    wrt.write(mousePositionHostOrder);

    invokeCanvasDrawCallbacks(cds);
}

void Client::handle_EndStroke(std::span<const char> msgWithoutMID)
{
    assert((msgWithoutMID.size() == PacketSize::PF_END_STROKE - 1)
        && "Size is wrong when receiving end stroke");

    CanvasDrawState cds;
    cds._type = MessageType::PF_END_STROKE;
    ByteReader rdr{ .buffer = msgWithoutMID };
    auto sessionIdHostOrder = ntohl(rdr.read<SessionId>());

    // datagram not meant for us, ignore it
    if (sessionIdHostOrder != _sessionId) return;

    cds._sqNumberHostOrder = ntohl(rdr.read<SequenceNumber>());
    invokeCanvasDrawCallbacks(cds);
}

void Client::invokeCanvasDrawCallbacks(const CanvasDrawState& cds)
{
    std::lock_guard lock(_canvasDrawStateFunctionsMutex);
    for (const auto& [_ignore, fn] : _canvasDrawStateFunctions) fn(cds);
}
