#include "client.hpp"

#include <mutex>
#include <vector>
#include <format>
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
    threadSafeOStream(std::cout, std::format("[Client]: {}:{}", _ip, _portHostOrder));
}

void Client::terminate()
{
    if (_socket != INVALID_SOCKET) closesocket(_socket);
    WSACleanup();
}

bool Client::connect(std::string serverIp, std::string serverPort)
{
    std::uint16_t portHostOrder;
    try
    {
        portHostOrder = std::stoi(serverPort);
        if (portHostOrder > 65535) throw std::runtime_error("Invalid port range");
    }
    catch (...)
    {
        threadSafeOStream(std::cerr, std::format("[Client] Tried to connect with an invalid port, port {} is not a valid number",serverPort));
        return false;
    }
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(portHostOrder);
    if (inet_pton(AF_INET, serverIp.c_str(), &addr.sin_addr) != 1)
    {
        threadSafeOStream(std::cerr, std::format("[Client] Tried to connect with an invalid ip {}",serverIp));
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
        if (bytesReceived != 5)
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
        std::memcpy(&_sessionId, udpPacket.data() + 1, sizeof(_sessionId));
        _sessionId = ntohl(_sessionId);
        threadSafeOStream(std::cout, std::format("[Client] Successfully connected to {}:{} after {} attempts!", serverIp, serverPort, attempt + 1));
        return true;
    }
    return false;
}

void Client::sendInputState(const InputState& inputState)
{
    std::vector<char> msg;
    // +1 for the message type, +4 for session id
    msg.resize(InputState::SIZE_OF_INPUT_STATE + 1 + sizeof(SessionId));
    msg[0] = static_cast<char>(static_cast<std::uint8_t>(MessageType::PF_INPUTSTATE));
    SessionId sessionIdNetworkOrder = htonl(_sessionId);
    std::memcpy(msg.data() + 1, &sessionIdNetworkOrder, sizeof(sessionIdNetworkOrder));
    SequenceNumber sqNumberNetworkOrder = htonl(inputState.currentSequenceNumber);
    std::memcpy(msg.data() + 5, &sqNumberNetworkOrder, sizeof(sqNumberNetworkOrder));
    InputBits inputBitsNetworkOrder = htonl(inputState.currentInput);
    std::memcpy(msg.data() + 9,&inputBitsNetworkOrder,sizeof(inputBitsNetworkOrder));
    std::uint16_t mouseXNetworkOrder = htons(inputState.currentMousePos[0]);
    std::uint16_t mouseYNetworkOrder = htons(inputState.currentMousePos[1]);
    std::memcpy(msg.data() + 13, &mouseXNetworkOrder, sizeof(mouseXNetworkOrder));
    std::memcpy(msg.data() + 15, &mouseYNetworkOrder, sizeof(mouseYNetworkOrder));
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
            //threadSafeOStream(std::cout,
            //    std::format("[Client] Successfully sent input state for sequence {} on attempt {}",
            //        inputState.currentSequenceNumber, attempt + 1));
            return;
        }
    }
    threadSafeOStream(std::cerr,
        std::format("[Client] Failed to send input state for sequence {}",
            inputState.currentSequenceNumber));
    return;
}
