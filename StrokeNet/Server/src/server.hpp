/* Start Header
***********************************************************************/

/*! \file   server.hpp
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
#pragma once
#include "shared_protocol.hpp"

#include <winsock2.h>

#include <list>
#include <span>
#include <deque>
#include <mutex>
#include <format>
#include <string>
#include <thread>
#include <atomic>
#include <vector>
#include <utility>
#include <cstdint>
#include <ostream>
#include <iostream>
#include <optional>
#include <stop_token>
#include <functional>
#include <string_view>
#include <unordered_map>
#include "login.hpp"

void log(std::ostream& os, std::string_view msg);
class Server
{
public:
    Server();
    ~Server();

    void startListening();
    bool isListeningThreadFinished() noexcept;
public:
    // ============================================================
    // Game
    // ============================================================
    uint32_t max_len{};
    std::vector<std::string> word_list{};
    std::pair<bool, std::string> word{ 1,{} };

    void pick_word();
    void load_wordlist();
    int word_heuristic();

    void stopGame();
    void startGame();
    bool gameStarted();
    void advanceDrawer();

    std::size_t getNumberOfPlayers();
private:
    // ============================================================
    // Game State
    // ============================================================
    std::mutex _gameMut;
    UserStore _userStore;
    bool _gameRunning = false;
    std::size_t _currentAllowedToDrawIndex{};
    std::vector<SessionId> _listOfPlayersAllowedToDraw;

    // ============================================================
    // Client storage
    // ============================================================
    struct Client
    {
        std::string ipPort;
        std::string username;
        sockaddr_in sa;

        std::optional<std::uint32_t> currentStrokeId;
    };
    // right now, if client misbehaves and keeps sending
    // registeration after registration without deregistering
    // then we have a leak
    std::mutex _clientStorageMutex;
    std::unordered_map<SessionId, Client> _sessionIdToClient;

    // ============================================================
    // Socket / session
    // ============================================================
    std::string _ip;
    std::thread _thread;
    const int _maxRetry = 5;
    unsigned _portHostOrder = 0;
    std::stop_source _stopSource;
    const double _recvTimeOut = 0.1;
    SOCKET _socket = INVALID_SOCKET;
    std::atomic<bool> _threadFinished = false;
    SessionId _nextSessionIdHostOrder = InvalidSessionId + 1;

    // ============================================================
    // Message handlers
    // ============================================================
    void handle_reqLogin            (std::span<const char> udpPacketWithoutMID, sockaddr_in* sa);
    void handle_reqCreateAccount    (std::span<const char> udpPacketWithoutMID, sockaddr_in* sa);

    void handle_reqStartStroke      (std::span<const char> udpPacketWithoutMID, sockaddr_in* sa);
    void handle_reqEndStroke        (std::span<const char> udpPacketWithoutMID, sockaddr_in* sa);

    void handle_reqMsg              (std::span<const char> udpPacketWithoutMID, sockaddr_in* sa);

    void handle_fafExtendStroke     (std::span<const char> udpPacketWithoutMID, sockaddr_in* sa);
    void handle_fafDisconnect       (std::span<const char> udpPacketWithoutMID, sockaddr_in* sa);

    using MessageFn = void(Server::*)(std::span<const char>, sockaddr_in*);
    const std::unordered_map<MessageType, MessageFn> _messageTypeFns
    {
        std::make_pair(MessageType::REQ_LOGIN,          &Server::handle_reqLogin            ),
        std::make_pair(MessageType::REQ_CREATE_ACCOUNT, &Server::handle_reqCreateAccount    ),

        std::make_pair(MessageType::REQ_START_STROKE,   &Server::handle_reqStartStroke      ),
        std::make_pair(MessageType::REQ_END_STROKE,     &Server::handle_reqEndStroke        ),
        std::make_pair(MessageType::REQ_MSG,            &Server::handle_reqMsg              ),

        std::make_pair(MessageType::FAF_EXTEND_STROKE,  &Server::handle_fafExtendStroke     ),
        std::make_pair(MessageType::FAF_DISCONNECT,     &Server::handle_fafDisconnect       ),
    };

    // ============================================================
    // Listening thread
    // ============================================================
    void actualStartListening(std::stop_token st) noexcept;
    SessionId getNextSessionIdHostOrder();
    bool destroySessionIdHostOrder(SessionId id, std::string ipPort);

    // ============================================================
    // Helpers
    // ============================================================
    bool sendWithRetry(std::span<const char> data, const sockaddr_in& sa);

    template <typename Packet, typename FillFn>
    void broadcastPacket(Packet& pkt,FillFn fill)
    {
        for (auto& [sid, client] : _sessionIdToClient)
        {
            fill(pkt, sid);

            bool success = sendWithRetry(pkt, client.sa);
            if (!success)
            {
                log(std::cerr,
                    std::format("[Server] Broadcast send failed for client session id: {}", sid));
            }
        }
    }
};
