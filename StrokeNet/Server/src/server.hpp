/* Start Header **************************************************************/
/*! \file   server.hpp
    \author Loh Boon Cheong, Timothy
    \par    email: loh.b@digipen.edu
    \date   20th March, 2026
    \brief  Copyright (C) 2026 DigiPen Institute of Technology
            Reproduction or disclosure of this file or its contents without
            the prior written consent of DigiPen Institute of Technology is
            prohibited. */
            /* End Header ****************************************************************/
#pragma once
#include "shared_protocol.hpp"
#include "login.hpp"

#include <winsock2.h>

#include <span>
#include <mutex>
#include <atomic>
#include <chrono>
#include <string>
#include <thread>
#include <vector>
#include <cstdint>
#include <stop_token>
#include <functional>
#include <unordered_map>
#include "login.hpp"
#include <unordered_set>

class Server
{
public:
    using RegCdsFnId = int;

public:
    Server();
    ~Server();

    void startListening();
    bool isListeningThreadFinished() noexcept;

    // Relay a canvas draw command to all connected clients.
    // START_STROKE and END_STROKE are sent reliably (retransmitted until ACKed).
    // ADD_POINT is fire-and-forget.
    void sendCanvasDrawCommand(const CanvasDrawCommand& cds);

    RegCdsFnId registerCdsFn(std::function<void(SessionId, const CanvasDrawCommand&)> fn);
    void       deregisterCdsFn(RegCdsFnId id);

public: // Game logic
    std::vector<std::string>     word_list{};
    std::pair<bool, std::string> word{true, {}};
    uint32_t                     max_len{};

    void load_wordlist();
    void pick_word();
    int  word_heuristic();

private:
    struct Client
    {
        std::string ipPort;
        sockaddr_in sa;
    };

    // ------------------------------------------------------------------
    // Reliable send bookkeeping (server → client)
    // ------------------------------------------------------------------
    struct PendingReliable
    {
        std::vector<char>                     packet;
        sockaddr_in                           dest;
        SessionId                             destSessionId;
        std::chrono::steady_clock::time_point lastSentAt;
        int                                   attempts{0};
    };

    static constexpr double RetransmitIntervalSec = 0.15;
    static constexpr int    MaxReliableAttempts = 5;

    std::mutex                                          _pendingAcksMutex;
    std::unordered_map<SequenceNumber, PendingReliable> _pendingAcks;

    std::atomic<SequenceNumber> _nextServerSeq{0};
    SequenceNumber allocServerSeq()
    {
        return _nextServerSeq.fetch_add(1, std::memory_order_relaxed);
    }

    // ------------------------------------------------------------------
    // Stroke ordering buffer (server receive side)
    //
    // ADD_POINT and END_STROKE for a given stroke may arrive before the
    // corresponding START_STROKE (because START_STROKE is reliable and may
    // be in-flight while unreliable ADD_POINTs race ahead).
    //
    // Key = (sessionId << 32) | strokeId so strokes from different clients
    // never collide even if they reuse the same strokeId counter.
    // ------------------------------------------------------------------
    struct StrokeBuffer
    {
        bool                          started{false};
        std::vector<CanvasDrawCommand> pending; // buffered ADD_POINT / END_STROKE
    };

    std::mutex                              _strokeBufferMutex;
    std::unordered_map<uint64_t, StrokeBuffer> _strokeBuffers;

    static uint64_t strokeKey(SessionId sid, std::uint32_t strokeId) noexcept
    {
        return (static_cast<uint64_t>(sid) << 32) | strokeId;
    }

    // ------------------------------------------------------------------
    // Socket / session state
    // ------------------------------------------------------------------
    UserStore   _userStore;
    SOCKET      _socket = INVALID_SOCKET;
    unsigned    _portHostOrder = 0;
    std::string _ip;

    std::thread       _thread;
    std::thread       _retransmitThread;
    std::atomic<bool> _threadFinished{false};
    std::stop_source  _stopSource;

    const int    _maxRetry = 5;
    const double _recvTimeOut = 0.1;

    SessionId _nextSessionIdHostOrder = InvalidSessionId + 1;

    std::mutex                            _clientStorageMutex;
    std::unordered_map<SessionId, Client> _sessionIdToClient;

    // ------------------------------------------------------------------
    // Canvas draw command callbacks
    // ------------------------------------------------------------------
    std::mutex    _cdsFnsMutex;
    RegCdsFnId    _nextRegCdsFnId = 1;
    std::unordered_map<RegCdsFnId,
        std::function<void(SessionId, const CanvasDrawCommand&)>> _cdsFns;

    // ------------------------------------------------------------------
    // Internal send helpers
    // ------------------------------------------------------------------
    void sendReliableTo(SequenceNumber serverSeq, std::vector<char> packet,
        const sockaddr_in& dest, SessionId destSessionId);
    void doSend(const std::vector<char>& packet, const sockaddr_in& dest);
    void sendAck(const sockaddr_in& dest, SequenceNumber seqNetworkOrder);
    void retransmitLoop(std::stop_token st) noexcept;

    // ------------------------------------------------------------------
    // Inbound message handlers
    // ------------------------------------------------------------------
    void handle_ack(std::span<const char> body, sockaddr_in* sa);
    void handle_reqUnregister(std::span<const char> body, sockaddr_in* sa);
    void handle_pfStartStroke(std::span<const char> body, sockaddr_in* sa);
    void handle_pfAddPoint(std::span<const char> body, sockaddr_in* sa);
    void handle_pfEndStroke(std::span<const char> body, sockaddr_in* sa);

    // Enforces stroke ordering, then fires registered callbacks.
    void handle_CanvasDrawingCommand(CanvasDrawCommand cds, SessionId sessionIdHostOrder);
    // Dispatch to callbacks unconditionally (called after ordering is satisfied).
    void dispatchCanvasCommand(SessionId sessionIdHostOrder, const CanvasDrawCommand& cds);

    void handle_reqLogin(std::span<const char> body, sockaddr_in* sa);
    void handle_reqCreateAccount(std::span<const char> body, sockaddr_in* sa);

    using MessageFn = void(Server::*)(std::span<const char>, sockaddr_in*);
    const std::unordered_map<MessageType, MessageFn> _messageTypeFns
    {
        { MessageType::ACK,               &Server::handle_ack             },
        { MessageType::REQ_UNREGISTER,    &Server::handle_reqUnregister   },
        { MessageType::PF_START_STROKE,   &Server::handle_pfStartStroke   },
        { MessageType::PF_ADD_POINT,      &Server::handle_pfAddPoint      },
        { MessageType::PF_END_STROKE,     &Server::handle_pfEndStroke     },
        { MessageType::REQ_LOGIN,         &Server::handle_reqLogin        },
        { MessageType::REQ_CREATE_ACCOUNT,&Server::handle_reqCreateAccount},
    };

    // ------------------------------------------------------------------
    // Session helpers
    // ------------------------------------------------------------------
    SessionId getNextSessionIdHostOrder();
    bool      destroySessionIdHostOrder(SessionId id, const std::string& ipPort);
    void      actualStartListening(std::stop_token st) noexcept;
};
