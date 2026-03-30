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

#include <map>
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

    std::mutex roundsMutex;
    std::pair<uint8_t, uint8_t> rounds{};

    void pick_word();
    void load_wordlist();
    int word_heuristic();

    void stopGame();
    void startGame();
    void resetRound();
    bool gameStarted();

    const double budgetAdvanceTurn{ 50.0 }; // advance turn every 60 seconds, just for testing
    const int budgetAdvanceTurnInt{ static_cast<int>(budgetAdvanceTurn) };
    std::atomic<std::chrono::steady_clock::time_point> advanceTurnNow{ std::chrono::steady_clock::now() };

    // This locks both game and client storage mutexes
    void LOCK_advanceDrawer();
    void LOCK_broadcastScoreboard();

    // This doesnt lock any mutex
    void NO_LOCK_advanceDrawer();
    void NO_LOCK_broadcastScoreboard();

    std::size_t LOCK_getNumberOfPlayers();
    std::size_t NO_LOCK_getNumberOfPlayers();

    // Round reset commands
    void LOCK_sendNewRoundEndTime();

    void LOCK_sendClearCanvasCommand();
    void LOCK_sendNewWordLen();
    void LOCK_sendNewWord();

    void LOCK_sendStrokeHistory();
    void LOCK_sendMessageHistory();

    void LOCK_forceEndStroke();
    void NO_LOCK_forceEndStroke();

    void LOCK_sendMessage(const std::string& username, const std::string& message);

    void LOCK_sendLeaderboard();
    void NO_LOCK_sendLeaderboard();
private:
    // ============================================================
    // Game State
    // ============================================================
    UserStore _userStore;

    // Mutex protects _gameRunning and _drawerSessionId
    std::mutex _gameMutex;
    bool _gameRunning = false;
    std::optional<SessionId> _drawerSessionId = std::nullopt;

    // ============================================================
    // Client storage
    // ============================================================
    struct Client
    {
        std::string ipPort;
        std::string username;
        sockaddr_in sa;
        std::uint16_t score{};
        std::uint16_t highscore{};
        std::optional<std::uint32_t> currentStrokeId;
        bool wordAlreadyGuessed = false;
        bool inGame = false;
    };
    // right now, if client misbehaves and keeps sending
    // registeration after registration without deregistering
    // then we have a leak
    std::mutex _clientStorageMutex;
    std::map<SessionId, Client> _clientStorageMap;

    // ============================================================
    // Stroke History
    // ============================================================
    std::mutex _pastStrokesMutex;
    std::vector<PastStroke> _pastStrokes_NEED_MUTEX;

    // ============================================================
    // Chat Message History
    // ============================================================
    std::mutex _pastChatMsgMutex;
    std::deque<PastMessage> _pastChatMsg_NEED_MUTEX;

    // ============================================================
    // Ids for acks
    // ============================================================
    std::uint32_t _messageIdServer = 1;
    std::uint32_t _msgHistoryIdServer = 1;
    std::uint32_t _scoreBoardIdServer = 1;
    std::uint32_t _sendNewWordIdServer = 1;
    std::uint32_t _clearCanvasIdServer = 1;
    std::uint32_t _roundEndTimeIdServer = 1;
    std::uint32_t _strokeHistoryIdServer = 1;
    std::uint32_t _sendNewWordLenIdServer = 1;
    std::uint32_t _sendLeaderboardIdServer = 1;

    // ============================================================
    // NTF Handling
    // ============================================================
    struct PendingNTF
    {
        std::vector<char> _data;
        sockaddr_in _clientAddr;
        SessionId _targetSessionId;
        std::uint32_t _ntfId;

        std::chrono::steady_clock::time_point _nextSendTime;
        std::chrono::steady_clock::time_point _giveUpTime;

        std::vector<std::vector<char>> _chunks;
        std::uint16_t _nextChunkToSend = 1;
    };

    // Unordered map bs, since 1:N for ntdId:client, so need to composite
    struct NtfKey
    {
        SessionId sessionId;
        std::uint32_t ntfId;

        bool operator==(const NtfKey&) const = default;
    };

    struct NtfKeyHash
    {
        std::size_t operator()(const NtfKey& k) const
        {
            return std::hash<std::uint64_t>{}(
                (static_cast<std::uint64_t>(k.sessionId) << 32) | k.ntfId
                );
        }
    };

    // ============================================================
    // Check for pending NTFs for sending new word length
    // ============================================================
    std::mutex _pendingNtfNewWordLenMutex;
    std::unordered_map<NtfKey, PendingNTF, NtfKeyHash> _pendingNtfNewWordsLen;
    
    // ============================================================
    // Check for pending NTFs for sending new word
    // ============================================================
    std::mutex _pendingNtfNewWordMutex;
    std::unordered_map<NtfKey, PendingNTF, NtfKeyHash> _pendingNtfNewWords;

    // ============================================================
    // Check for pending NTFs for clear canvas
    // ============================================================
    std::mutex _pendingNtfClearCanvasMutex;
    std::unordered_map<NtfKey, PendingNTF, NtfKeyHash> _pendingNtfClearCanvases;

    // ============================================================
    // Check for pending NTFs for msgs
    // ============================================================
    std::mutex _pendingNtfMsgMutex;
    std::unordered_map<NtfKey, PendingNTF, NtfKeyHash> _pendingNtfMsg;

    // ============================================================
    // Check for pending NTFs for scoreboard
    // ============================================================
    std::mutex _pendingNtfUpdateScoreboardMutex;
    std::unordered_map<NtfKey, PendingNTF, NtfKeyHash> _pendingNtfUpdateScoreboards;

    // ============================================================
    // Check for pending NTFs for round end time
    // ============================================================
    std::mutex _pendingNtfRETMutex;
    std::unordered_map<NtfKey, PendingNTF, NtfKeyHash> _pendingNtfRET;

    // ============================================================
    // Check for pending NTFs for stroke history
    // ============================================================
    std::mutex _pendingNtfStrokeHistoryMutex;
    std::unordered_map<NtfKey, PendingNTF, NtfKeyHash> _pendingNtfStrokeHistory;

    // ============================================================
    // Check for pending NTFs for message history
    // ============================================================
    std::mutex _pendingNtfMessageHistoryMutex;
    std::unordered_map<NtfKey, PendingNTF, NtfKeyHash> _pendingNtfMessageHistory;

    // ============================================================
    // Check for pending NTFs for leaderboard
    // ============================================================
    std::mutex _pendingNtfLeaderboardMutex;
    std::unordered_map<NtfKey, PendingNTF, NtfKeyHash> _pendingNtfLeaderboard;

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
    // Message handlers - RULES (NO LOCKING MUTEXES DIRECTLY INSIDE THESE FUNCTIONS)
    // ============================================================
    void handle_reqLogin                (std::span<const char> udpPacketWithoutMID, sockaddr_in* sa);
    void handle_reqCreateAccount        (std::span<const char> udpPacketWithoutMID, sockaddr_in* sa);
    void handle_reqPlayGame             (std::span<const char> udpPacketWithoutMID, sockaddr_in* sa);
    void handle_reqQuitGame             (std::span<const char> udpPacketWithoutMID, sockaddr_in* sa);

    void handle_reqStartStroke          (std::span<const char> udpPacketWithoutMID, sockaddr_in* sa);
    void handle_reqEndStroke            (std::span<const char> udpPacketWithoutMID, sockaddr_in* sa);
    void handle_reqClearCanvas          (std::span<const char> udpPacketWithoutMID, sockaddr_in* sa);

    void handle_reqMsg                  (std::span<const char> udpPacketWithoutMID, sockaddr_in* sa);

    void handle_fafExtendStroke         (std::span<const char> udpPacketWithoutMID, sockaddr_in* sa);
    void handle_fafDisconnect           (std::span<const char> udpPacketWithoutMID, sockaddr_in* sa);

    void handle_ntfRcvClearCanvas       (std::span<const char> udpPacketWithoutMID, sockaddr_in* sa);
    void handle_ntfRcvMsg               (std::span<const char> udpPacketWithoutMID, sockaddr_in* sa);
    void handle_ntfRcvUpdateScoreboard  (std::span<const char> udpPacketWithoutMID, sockaddr_in* sa);
    void handle_ntfRcvRET               (std::span<const char> udpPacketWithoutMID, sockaddr_in* sa);
    void handle_ntfRcvSendWordLen       (std::span<const char> udpPacketWithoutMID, sockaddr_in* sa);
    void handle_ntfRcvSendWord          (std::span<const char> udpPacketWithoutMID, sockaddr_in* sa);
    void handle_ntfRcvStrokeHistory     (std::span<const char> udpPacketWithoutMID, sockaddr_in* sa);
    void handle_ntfRcvMsgHistory        (std::span<const char> udpPacketWithoutMID, sockaddr_in* sa);

    using MessageFn = void(Server::*)(std::span<const char>, sockaddr_in*);
    const std::unordered_map<MessageType, MessageFn> _messageTypeFns
    {
        std::make_pair(MessageType::REQ_LOGIN,                  &Server::handle_reqLogin                ),
        std::make_pair(MessageType::REQ_CREATE_ACCOUNT,         &Server::handle_reqCreateAccount        ),
        std::make_pair(MessageType::REQ_MSG,                    &Server::handle_reqMsg                  ),
        std::make_pair(MessageType::REQ_PLAY_GAME,              &Server::handle_reqPlayGame             ),
        std::make_pair(MessageType::REQ_QUIT_GAME,              &Server::handle_reqQuitGame             ),
        
        std::make_pair(MessageType::REQ_START_STROKE,           &Server::handle_reqStartStroke          ),
        std::make_pair(MessageType::REQ_END_STROKE,             &Server::handle_reqEndStroke            ),
        std::make_pair(MessageType::REQ_CLEAR_CANVAS,           &Server::handle_reqClearCanvas          ),
        
        std::make_pair(MessageType::FAF_EXTEND_STROKE,          &Server::handle_fafExtendStroke         ),
        std::make_pair(MessageType::FAF_DISCONNECT,             &Server::handle_fafDisconnect           ),

        std::make_pair(MessageType::NTF_RCV_CLEAR_CANVAS,       &Server::handle_ntfRcvClearCanvas       ),
        std::make_pair(MessageType::NTF_RCV_MSG,                &Server::handle_ntfRcvMsg               ),
        std::make_pair(MessageType::NTF_RCV_UPDATE_SCOREBOARD,  &Server::handle_ntfRcvUpdateScoreboard  ),
        std::make_pair(MessageType::NTF_RCV_ROUND_END_TIME,     &Server::handle_ntfRcvRET               ),
        std::make_pair(MessageType::NTF_RCV_SEND_WORD_LEN,      &Server::handle_ntfRcvSendWordLen       ),
        std::make_pair(MessageType::NTF_RCV_SEND_WORD,          &Server::handle_ntfRcvSendWord          ),
        std::make_pair(MessageType::NTF_RCV_STROKE_HISTORY,     &Server::handle_ntfRcvStrokeHistory     ),
        std::make_pair(MessageType::NTF_RCV_MSG_HISTORY,        &Server::handle_ntfRcvMsgHistory        ),
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
    void LOCK_broadcastPacket(Packet& pkt,FillFn fill)
    {
        std::lock_guard lock(_clientStorageMutex);
        NO_LOCK_broadcastPacket(pkt, fill);
    }
    template <typename Packet, typename FillFn>
    void NO_LOCK_broadcastPacket(Packet& pkt, FillFn fill)
    {
        for (auto& [sid, client] : _clientStorageMap)
        {
            if (!client.inGame) continue;
            fill(pkt, sid);

            bool success = sendWithRetry(pkt, client.sa);
            if (!success)
            {
                log(std::cerr,
                    std::format("[Server] Broadcast send failed for client session id: {}", sid));
            }
        }
    }
    void tickPendingNtf(std::mutex& mut, std::unordered_map<NtfKey, PendingNTF, NtfKeyHash>& map, std::string_view name);

    template <typename Fn>
    auto LOCK_clientStorage(Fn fn)
        -> decltype(fn(_clientStorageMap))
    {
        std::lock_guard lock(_clientStorageMutex);
        return fn(_clientStorageMap);
    }

    template <typename Fn>
    auto LOCK_gameVariables(Fn fn) 
        -> decltype(fn(_gameRunning, _drawerSessionId))
    {
        std::lock_guard lock(_gameMutex);
        return fn(_gameRunning, _drawerSessionId);
    }

    public:
    template <typename Fn>
    auto LOCK_gameVariablesANDclientStorage(Fn fn)
        -> decltype(fn(_clientStorageMap, _gameRunning, _drawerSessionId))
    {
        std::scoped_lock lock(_gameMutex, _clientStorageMutex);
        return fn(_clientStorageMap, _gameRunning, _drawerSessionId);
    }

};
