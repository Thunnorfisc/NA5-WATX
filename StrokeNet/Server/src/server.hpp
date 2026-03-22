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
#include <mutex>
#include <string>
#include <thread>
#include <atomic>
#include <vector>
#include <utility>
#include <cstdint>
#include <optional>
#include <stop_token>
#include <functional>
#include <unordered_map>
class Server
{
public:
    using RegCdsFnId = int;
public:
    Server();
    ~Server();

    void startListening();
    bool isListeningThreadFinished() noexcept;
    void sendCanvasDrawState(const CanvasDrawState& cds);
    RegCdsFnId registerCdsFn(
        std::function<void(SessionId sessionIdHostOrder,const CanvasDrawState&)> fn);
    void deregisterCdsFn(RegCdsFnId id);
private:
    struct Client
    {
        std::string ipPort;
        sockaddr_in sa;
    };

    SOCKET _socket = INVALID_SOCKET;
    unsigned _portHostOrder = 0;
    std::string _ip;
    std::thread _thread;
    std::atomic<bool> _threadFinished = false;
    std::stop_source _stopSource;

    const int _maxRetry = 5;
    const double _recvTimeOut = 0.1;

    SessionId _nextSessionIdHostOrder = InvalidSessionId + 1;

    std::mutex _cdsFnsMutex;
    RegCdsFnId _nextRegCdsFnId = 1;
    std::unordered_map<RegCdsFnId,
        std::function<void(SessionId sessionIdHostOrder,const CanvasDrawState&)>> _cdsFns;

    void handle_reqRegister(std::span<const char> udpPacketWithoutMID, sockaddr_in* sa);
    void handle_reqUnregister(std::span<const char> udpPacketWithoutMID, sockaddr_in* sa);
    void handle_pfInputState(std::span<const char> udpPacketWithoutMID, sockaddr_in* sa);
    void handle_pfStartStroke(std::span<const char> udpPacketWithoutMID, sockaddr_in* sa);
    void handle_pfAddPoint(std::span<const char> udpPacketWithoutMID, sockaddr_in* sa);
    void handle_pfEndStroke(std::span<const char> udpPacketWithoutMID, sockaddr_in* sa);
    void handle_CanvasDrawingCommand(CanvasDrawState cds,
        SessionId sessionIdHostOrder);

    using MessageFn = void(Server::*)(std::span<const char>, sockaddr_in*);
    const std::unordered_map<MessageType, MessageFn> _messageTypeFns
    {
        std::make_pair(MessageType::REQ_REGISTER,&Server::handle_reqRegister),
        std::make_pair(MessageType::REQ_UNREGISTER,&Server::handle_reqUnregister),
        std::make_pair(MessageType::PF_INPUTSTATE,&Server::handle_pfInputState),

        std::make_pair(MessageType::PF_START_STROKE,&Server::handle_pfStartStroke),
        std::make_pair(MessageType::PF_ADD_POINT,&Server::handle_pfAddPoint),
        std::make_pair(MessageType::PF_END_STROKE,&Server::handle_pfEndStroke),
    };

    // right now, if client misbehaves and keeps sending
    // registeration after registration without deregistering
    // then we have a leak
    std::unordered_map<SessionId, Client> _sessionIdToClient;
    // ========================= runs on a different thread
    void actualStartListening(std::stop_token st) noexcept;
    // ========================= all called by the receiving thread start
    SessionId getNextSessionIdHostOrder();
    bool destroySessionIdHostOrder(SessionId id, std::string ipPort);
    // ========================= all called by the receiving thread end
};
