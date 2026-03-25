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
#include "login.hpp"
class Server
{
public:
    using RegCdsFnId = int;
public:
    Server();
    ~Server();

    void startListening();
    bool isListeningThreadFinished() noexcept;
public: //GMAE
    std::vector<std::string> word_list{};
    std::pair<bool, std::string> word{ 1,{} };
    uint32_t max_len{};

    void load_wordlist();
    void pick_word();
    int word_heuristic();
private:
    struct Client
    {
        std::string ipPort;
        sockaddr_in sa;

        std::optional<std::uint32_t> currentStrokeId;
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

    // Game stuff
    std::atomic<SessionId> _currentAllowedToDraw = InvalidSessionId;
    UserStore _userStore;

    // Handling messages
    void handle_reqLogin(std::span<const char> udpPacketWithoutMID, sockaddr_in* sa);
    void handle_reqCreateAccount(std::span<const char> udpPacketWithoutMID, sockaddr_in* sa);
    void handle_reqUnregister(std::span<const char> udpPacketWithoutMID, sockaddr_in* sa);

    void handle_reqStartStroke(std::span<const char> udpPacketWithoutMID, sockaddr_in* sa);
    void handle_reqEndStroke(std::span<const char> udpPacketWithoutMID, sockaddr_in* sa);

    void handle_fafExtendStroke(std::span<const char> udpPacketWithoutMID, sockaddr_in* sa);

    using MessageFn = void(Server::*)(std::span<const char>, sockaddr_in*);
    const std::unordered_map<MessageType, MessageFn> _messageTypeFns
    {
        std::make_pair(MessageType::REQ_LOGIN, &Server::handle_reqLogin),
        std::make_pair(MessageType::REQ_CREATE_ACCOUNT, &Server::handle_reqCreateAccount),
        std::make_pair(MessageType::REQ_UNREGISTER,&Server::handle_reqUnregister),

        std::make_pair(MessageType::REQ_START_STROKE, &Server::handle_reqStartStroke),
        std::make_pair(MessageType::REQ_END_STROKE, &Server::handle_reqEndStroke),

        std::make_pair(MessageType::FAF_EXTEND_STROKE, &Server::handle_fafExtendStroke),
    };

    // right now, if client misbehaves and keeps sending
    // registeration after registration without deregistering
    // then we have a leak
    std::unordered_map<SessionId, Client> _sessionIdToClient;
    std::mutex _clientStorageMutex;
    // ========================= runs on a different thread
    void actualStartListening(std::stop_token st) noexcept;
    // ========================= all called by the receiving thread start
    SessionId getNextSessionIdHostOrder();
    bool destroySessionIdHostOrder(SessionId id, std::string ipPort);
    // ========================= all called by the receiving thread end
};
