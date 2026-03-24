#pragma once
#include "shared_protocol.hpp"
#include <winsock2.h>
#include <span>
#include <queue>
#include <mutex>
#include <atomic>
#include <string>
#include <thread>
#include <vector>
#include <chrono>
#include <optional>
#include <stop_token>
#include <unordered_map>
#include <unordered_set>

class Client
{
public:
    struct CanvasCommandSend
    {
        enum class Type { START_STROKE, ADD_POINT, END_STROKE };
        Type              _type;
        std::vector<char> _data; // payload in host byte order
        //
        // Expected _data layouts (host byte order):
        //   START_STROKE : [strokeId u32][mousePos u16x2][RGBAT 5 bytes]  = 13 bytes
        //   ADD_POINT    : [strokeId u32][mousePos u16x2]                 =  8 bytes
        //   END_STROKE   : [strokeId u32]                                  =  4 bytes
    };

    struct CanvasCommandRecv
    {
        enum class Type { START_STROKE, ADD_POINT, END_STROKE };
        Type              _type;
        std::vector<char> _data;        // payload in host byte order (same layout as CanvasCommandSend)
        SequenceNumber    _serverSeqNumber;
        std::uint32_t     _strokeId{};  // stroke this command belongs to
    };

public:
    static void        initalize();
    static void        terminate();

    static LoginStatus loginViaBroadcast(const std::string& username, const std::string& password);
    static LoginStatus createAccountViaBroadcast(const std::string& username, const std::string& password);
    static void        disconnect();

    static void        sendCanvasCommand(const CanvasCommandSend& cmd);

    // Drain inbound canvas commands from the game thread each frame.
    static std::optional<CanvasCommandRecv> popCanvasCommand()
    {
        std::lock_guard lock(_canvasCommandMutex);
        if(_canvasCommands.empty()) return std::nullopt;
        auto cmd = std::move(_canvasCommands.front());
        _canvasCommands.pop();
        return cmd;
    }

private:
    // ------------------------------------------------------------------
    // Reliable send bookkeeping
    // ------------------------------------------------------------------
    struct PendingReliable
    {
        std::vector<char>                     packet;
        sockaddr_in                           dest;
        std::chrono::steady_clock::time_point lastSentAt;
        int                                   attempts{0};
    };

    static inline std::mutex                                           _pendingAcksMutex;
    static inline std::unordered_map<SequenceNumber, PendingReliable> _pendingAcks;

    static constexpr double RetransmitIntervalSec = 0.15;
    static constexpr int    MaxReliableAttempts = 5;

    // ------------------------------------------------------------------
    // Outbound unreliable queue
    // ------------------------------------------------------------------
    static inline std::mutex                                             _sendQueueMutex;
    static inline std::queue<std::pair<std::vector<char>, sockaddr_in>> _sendQueue;

    // ------------------------------------------------------------------
    // Inbound canvas commands (listening thread → game thread)
    // ------------------------------------------------------------------
    static inline std::mutex                    _canvasCommandMutex;
    static inline std::queue<CanvasCommandRecv> _canvasCommands;

    // ------------------------------------------------------------------
    // Stroke ordering buffer (receive side)
    //
    // ADD_POINT and END_STROKE may race ahead of the reliable START_STROKE.
    // Commands for a stroke are buffered here until START_STROKE arrives,
    // then flushed (sorted by server sequence number) into _canvasCommands.
    //
    // Key = strokeId.  Because only one player draws at a time in a
    // skribbl-style game, strokeId collisions between concurrent players
    // are not a concern in practice.
    // ------------------------------------------------------------------
    struct StrokeBuffer
    {
        bool                          started{false};
        std::vector<CanvasCommandRecv> pending;
    };

    static inline std::mutex                                  _strokeBufferMutex;
    static inline std::unordered_map<uint32_t, StrokeBuffer> _strokeBuffers;

    // ------------------------------------------------------------------
    // Sequence counter
    // ------------------------------------------------------------------
    static inline std::atomic<SequenceNumber> _nextSeq{0};
    static SequenceNumber allocSeq()
    {
        return _nextSeq.fetch_add(1, std::memory_order_relaxed);
    }

    // ------------------------------------------------------------------
    // Socket / session
    // ------------------------------------------------------------------
    static inline SOCKET      _socket = INVALID_SOCKET;
    static inline unsigned    _portHostOrder = 0;
    static inline std::string _ip;
    static inline SessionId   _sessionId = InvalidSessionId;
    static inline sockaddr_in _serverAddr{};
    static inline std::string _serverIpAndPort;

    static inline std::jthread    _listeningThread;
    static inline std::jthread    _sendingThread;
    static inline std::stop_source _stopSource;

    static inline const double _recvTimeOut = 0.05;
    static inline const int    _maxRetries = 3;

    // ------------------------------------------------------------------
    // Background threads
    // ------------------------------------------------------------------
    static void startListening(std::stop_token st);
    static void startSending(std::stop_token st);

    // ------------------------------------------------------------------
    // Internal send helpers
    // ------------------------------------------------------------------
    static void sendReliable(SequenceNumber seq, std::vector<char> packet);
    static void sendUnreliable(std::vector<char> packet);
    static void doSend(const std::vector<char>& packet, const sockaddr_in& dest);

    // ------------------------------------------------------------------
    // Inbound message handlers (called on listening thread)
    // ------------------------------------------------------------------
    static void handle_Ack(std::span<const char> body);
    static void handle_StartStroke(std::span<const char> body);
    static void handle_AddPoint(std::span<const char> body);
    static void handle_EndStroke(std::span<const char> body);

    // Push a received command into _canvasCommands (already holds _canvasCommandMutex).
    static void pushCommand(CanvasCommandRecv cmd);

    using ListenMsgFn = void(*)(std::span<const char>);
    static inline std::unordered_map<MessageType, ListenMsgFn> _listenMsgFns
    {
        { MessageType::ACK,             handle_Ack         },
        { MessageType::PF_START_STROKE, handle_StartStroke },
        { MessageType::PF_ADD_POINT,    handle_AddPoint    },
        { MessageType::PF_END_STROKE,   handle_EndStroke   },
    };
};
