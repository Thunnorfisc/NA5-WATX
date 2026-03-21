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
#pragma once
#include "shared_protocol.hpp"

#include <winsock2.h>

#include <span>
#include <deque>
#include <string>
#include <thread>
#include <atomic>
#include <vector>
#include <utility>
#include <cstdint>
#include <stop_token>
#include <unordered_map>

class Server
{
public:
    Server();
    ~Server();

    void startListening();
    bool isListeningThreadFinished() noexcept;
private:
    SOCKET _socket = INVALID_SOCKET;
    unsigned _portHostOrder = 0;
    std::string _ip;
    std::thread _thread;
    std::atomic<bool> _threadFinished = false;
    std::stop_source _stopSource;

    const int _maxRetry = 5;

    const double _recvTimeOut = 0.1;

    SessionId _nextSessionIdHostOrder = InvalidSessionId + 1;

    void handle_reqRegister(std::span<const char> udpPacketWithoutMID, sockaddr_in* sa);
    void handle_reqUnregister(std::span<const char> udpPacketWithoutMID, sockaddr_in* sa);
    void handle_pfInputState(std::span<const char> udpPacketWithoutMID, sockaddr_in* sa);

    using MessageFn = void(Server::*)(std::span<const char>, sockaddr_in*);
    const std::unordered_map<MessageType, MessageFn> _messageTypeFns
    {
        std::make_pair(MessageType::REQ_REGISTER,&Server::handle_reqRegister),
        std::make_pair(MessageType::REQ_UNREGISTER,&Server::handle_reqUnregister),
        std::make_pair(MessageType::PF_INPUTSTATE,&Server::handle_pfInputState)
    };

    // right now, if client misbehaves and keeps sending
    // registeration after registration without deregistering
    // then we have a leak
    std::unordered_map<SessionId, std::string> _sessionIdToIpPort;
    // ========================= runs on a different thread
    void actualStartListening(std::stop_token st) noexcept;
    // ========================= all called by the receiving thread start
    SessionId getNextSessionIdHostOrder();
    bool destroySessionIdHostOrder(SessionId id, std::string ipPort);
    // ========================= all called by the receiving thread end
};
