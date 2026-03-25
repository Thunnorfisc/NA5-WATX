/* Start Header
***********************************************************************/

/*! \file   client.hpp
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
    static void        initalize();
    static void        terminate();

    static LoginStatus loginViaBroadcast(const std::string& username, const std::string& password);
    static LoginStatus createAccountViaBroadcast(const std::string& username, const std::string& password);
    static void        disconnect();

    // ============================================================
    // Drawing canvas thingies
    // ============================================================
    struct ReceivedStrokeCommand
    {
        enum class Type : std::uint8_t { START_STROKE, EXTEND_STROKE, END_STROKE };
        std::vector<char> _data;
        Type _type;
    };
    // ============================================================
    // Drawing canvas thingies - send
    // ============================================================
    static void sendStartStroke(
        std::uint32_t strokeid, std::array<std::uint16_t, 2> mousePos,
        std::array<std::uint8_t, 5> rgbat
    );
    static void sendExtendStroke(
        std::uint32_t strokeid,
        std::array<std::uint16_t, 2> mousePos
    );
    static void sendEndStroke(std::uint32_t strokeid);
    // ============================================================
    // Drawing canvas thingies - receive
    // ============================================================
    static std::queue<ReceivedStrokeCommand> getReceivedStrokeCommands();

private:
    // ============================================================
    // Socket / session
    // ============================================================
    static inline SOCKET      _socket = INVALID_SOCKET;
    static inline unsigned    _portHostOrder = 0;
    static inline std::string _ip;
    static inline std::atomic<SessionId> _sessionId = InvalidSessionId;
    static inline sockaddr_in _serverAddr{};
    static inline std::string _serverIpAndPort;

    static inline std::jthread    _listeningThread;
    static inline std::stop_source _stopSource;

    static inline const double _recvTimeOut = 0.05;
    static inline const int    _maxRetries = 3;

    //static inline std::atomic<SequenceNumber> _nextSeq = 0; // this one idk use for what, nid to evaluate again

    // ============================================================
    // Drawing canvas thingies
    // ============================================================
    struct BufferedStartEndStrokeToSend
    {
        enum class Type : uint8_t { START_STROKE, END_STROKE };
        std::vector<char> _data;
        std::uint32_t _hostStrokeId;
        Type _type;
    };
    static inline std::mutex _bsestsMutex;
    static inline std::queue<BufferedStartEndStrokeToSend> _bsestsQueue;

    struct PendingBSESTS
    {
        std::vector<char> _data; // what to resend

        std::chrono::steady_clock::time_point _nextSendTime; // when to retry
        std::chrono::steady_clock::time_point _giveUpTime; // when to abandon
    };

    // ============================================================
    // Keys are stroke ids.
    // Pending unordered maps are basically a staging area until
    // the respective rsp's arrive and the listening thread 
    // erases them and pushes data into _strokeCommandsReceived OR
    // it times out and the listening thread erases them or retries sending
    // depending on which time timed out.
    // ============================================================
    static inline std::unordered_map<std::uint32_t, PendingBSESTS> _pendingStartStrokes;
    static inline std::unordered_map<std::uint32_t, PendingBSESTS> _pendingEndStrokes;

    // ============================================================
    // Used for the game to check if there are any stroke commands
    // ============================================================
    static inline std::mutex _strokeCommandsReceivedMut;
    static inline std::queue<ReceivedStrokeCommand> _strokeCommandsReceived;

    // ============================================================
    // Listening thread
    // ============================================================
    static void handle_RSP_StartStroke(std::span<const char> msg); // < drain from _pending
    static void handle_RSP_EndStroke(std::span<const char> msg); // < drain from _pending

    static void handle_SVR_StartStroke(std::span<const char> msg); // < push into _strokeCommandsRecv
    static void handle_SVR_EndStroke(std::span<const char> msg); // < push into _strokeCommandsRecv
    static void handle_SVR_ExtendStroke(std::span<const char> msg); // < push into _strokeCommandsRecv

    static void startListening(std::stop_token st);

    using ListenMsgFn = void(*)(std::span<const char>);
    static inline std::unordered_map<MessageType, ListenMsgFn> _listenMsgFns
    {
        { MessageType::RSP_START_STROKE, &Client::handle_RSP_StartStroke },
        { MessageType::RSP_END_STROKE, &Client::handle_RSP_EndStroke },

        { MessageType::SVR_START_STROKE, &Client::handle_SVR_StartStroke },
        { MessageType::SVR_END_STROKE, &Client::handle_SVR_EndStroke },
        { MessageType::SVR_EXTEND_STROKE, &Client::handle_SVR_ExtendStroke },
    };
};
