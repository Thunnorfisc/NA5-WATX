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

#include <set>
#include <span>
#include <queue>
#include <mutex>
#include <atomic>
#include <string>
#include <thread>
#include <vector>
#include <chrono>
#include <ostream>
#include <iostream>
#include <optional>
#include <stop_token>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

void log(std::ostream& os, std::string_view msg);
class Client
{
public:
    static void        initalize();
    static void        terminate();

    static LoginStatus loginViaBroadcast(const std::string& username, const std::string& password);
    static LoginStatus createAccountViaBroadcast(const std::string& username, const std::string& password);
    static void        disconnect();

    static PlayGameStatus       playGame();
    static bool                 quitGame();

    struct ReceivedStrokeCommand
    {
        enum class Type : std::uint8_t { START_STROKE, EXTEND_STROKE, END_STROKE, CLEAR_CANVAS };
        std::vector<char> _data;
        Type _type;
    };
    struct ReceivedChatMessage
    {
        std::string _name;
        std::string _message;
    };
    struct ReceivedScoreBoard 
    {
        std::vector<std::pair<std::string, std::uint16_t>> _users;
        std::string _currentDrawer;
        std::uint8_t _yourIndex{};
    };
    struct ReceivedStrokeHistory
    {
        std::vector<PastStroke> _strokeHistory;
    };
    struct ReceivedChatMessageHistory
    {
        std::vector<PastMessage> _chatMessageHistory;
    };
    struct ReceivedLeaderboard
    {
        std::vector<std::pair<std::string, std::uint16_t>> _leaderboardEntries;
        std::uint32_t _playerIndex;
        std::uint16_t _playerScore;
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
    static void sendClearCanvas(std::uint32_t clearid);
    // ============================================================
    // Drawing canvas thingies - receive
    // ============================================================
    static std::queue<ReceivedStrokeCommand> getReceivedStrokeCommands();

    // ============================================================
    // Chat message thingies - send
    // ============================================================
    static void sendChatMessage(std::uint32_t msgId,const std::string& message);

    // ============================================================
    // Chat message thingies - receive
    // ============================================================
    static std::queue<ReceivedChatMessage> getReceivedChatMessages();

    // ============================================================
    // Scoreboard - receive
    // ============================================================
    static std::optional<ReceivedScoreBoard> getLatestScoreboard();

    // ============================================================
    // Leaderboard - receive
    // ============================================================
    static std::optional<ReceivedLeaderboard> getLeaderboard();

    // ============================================================
    // Round end time - receive
    // ============================================================
    static std::int64_t getRoundEndTimeMs();

    // ============================================================
    // Word length - receive
    // ============================================================
    static std::int32_t getWordLength();

    // ============================================================
    // Word - receive
    // ============================================================
    static std::string getWord();

    // ============================================================
    // Stroke history - receive
    // ============================================================
    static std::optional<ReceivedStrokeHistory> getStrokeHistory();

    // ============================================================
    // Message history - receive
    // ============================================================
    static std::optional<ReceivedChatMessageHistory> getMessageHistory();
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

    static inline const double _recvTimeOut = 0.075;
    static inline const int    _maxRetries = 3;

    // ============================================================
    // Play game bool
    // ============================================================
    static inline bool _playingGame = false;

    // ============================================================
    // Round end time
    // ============================================================
    static inline std::atomic<std::int64_t> _roundEndTimeMs = 0;

    // ============================================================
    // Word
    // ============================================================
    static inline std::atomic<std::int32_t> _word_len = 0;
    static inline std::atomic<std::shared_ptr<std::string>> _word{};

    // ============================================================
    // Chat messages thingies - Seen msges id
    // ============================================================
    static inline std::set<std::uint32_t> _seenMsgesId;

    struct BufferedToSend
    {
        std::vector<char> _data;
        std::uint32_t _hostId;
    };
    // ============================================================
    // Drawing canvas thingies - BSSTS
    // ============================================================
    static inline std::mutex _bufferedStartStrokeMutex;
    static inline std::queue<BufferedToSend> _bufferedStartStrokeQueue;

    // ============================================================
    // Drawing canvas thingies - BESTS
    // ============================================================
    static inline std::mutex _bufferEndStrokeMutex;
    static inline std::queue<BufferedToSend> _bufferedEndStrokeQueue;

    // ============================================================
    // Drawing canvas thingies - BCCTS
    // ============================================================
    static inline std::mutex _bufferClearCanvasMutex;
    static inline std::queue<BufferedToSend> _bufferedClearCanvasQueue;

    // ============================================================
    // Chat messages thingies - BMTS
    // ============================================================
    static inline std::mutex _bmtsMsgesMutex;
    static inline std::queue<BufferedToSend> _bufferedMsgesQueue;

    // ============================================================
    // Pending acks
    // ============================================================

    struct PendingBSESTS
    {
        std::vector<char> _data; // what to resend

        std::chrono::steady_clock::time_point _nextSendTime; // when to retry
        std::chrono::steady_clock::time_point _giveUpTime; // when to abandon
    };

    // ============================================================
    // Keys are ids.
    // Pending unordered maps are basically a staging area until
    // the respective rsps's arrive and the listening thread 
    // erases them and pushes data into their respective queues OR
    // it times out and the listening thread erases them or retries sending
    // depending on which time timed out.
    // ============================================================
    static inline std::unordered_map<std::uint32_t, PendingBSESTS> _pendingStartStrokes;
    static inline std::unordered_map<std::uint32_t, PendingBSESTS> _pendingEndStrokes;
    static inline std::unordered_map<std::uint32_t, PendingBSESTS> _pendingClearCanvas;
    static inline std::unordered_map<std::uint32_t, PendingBSESTS> _pendingMsges;

    // ============================================================
    // Used for the game to check if there are any stroke commands
    // ============================================================
    static inline std::mutex _strokeCommandsReceivedMut;
    static inline std::queue<ReceivedStrokeCommand> _strokeCommandsReceived;

    // ============================================================
    // Used for the game to check if there are any messages
    // ============================================================
    static inline std::mutex _msgesReceivedMut;
    static inline std::queue<ReceivedChatMessage> _msgesReceived;

    // ============================================================
    // Used for the game to check if there are any scoreboard updates
    // ============================================================
    static inline std::mutex _scoreboardReceivedMut;
    static inline std::queue<ReceivedScoreBoard> _scoreboardReceived;

    // ============================================================
    // Used for the game to check if there are any leaderboard updates
    // ============================================================
    static inline std::mutex _leaderboardReceivedMut;
    static inline std::queue<ReceivedLeaderboard> _leaderboardReceived;

    // ============================================================
    // Used for the game to check if there are any stroke history received
    // ============================================================
    static inline std::mutex _strokeHistoryReceivedMut;
    static inline std::queue<ReceivedStrokeHistory> _strokeHistoryReceived;
    static inline std::pair<std::uint32_t,std::vector<char>> _strokeHistoryId_AND_bufferedStrokeHistoryMsgChunks;
    static inline std::uint16_t _expectedStrokeChunkId = 0;

    // ============================================================
    // Used for the game to check if there are any msg history received
    // ============================================================
    static inline std::mutex _msgHistoryReceivedMut;
    static inline std::queue<ReceivedChatMessageHistory> _msgHistoryReceived;
    static inline std::pair<std::uint32_t, std::vector<char>> _msgHistoryId_AND_bufferedChatMsgChunks;
    static inline std::uint16_t _expectedMsgChunkId = 0;

    // ============================================================
    // Listening thread
    // ============================================================
    static void handle_RSP_StartStroke(std::span<const char> msg);  // < drain from respective _pending
    static void handle_RSP_EndStroke(std::span<const char> msg);    // < drain from respective _pending
    static void handle_RSP_ClearCanvas(std::span<const char> msg);  // < drain from respective _pending
    static void handle_RSP_Msg(std::span<const char> msg);          // < drain from respective _pending

    static void handle_SVR_StartStroke(std::span<const char> msg);  // < push into _strokeCommandsRecv
    static void handle_SVR_EndStroke(std::span<const char> msg);    // < push into _strokeCommandsRecv
    static void handle_SVR_ExtendStroke(std::span<const char> msg); // < push into _strokeCommandsRecv

    static void handle_NTF_Msg(std::span<const char> msg);              // < send back ack to server
    static void handle_NTF_ClearCanvas(std::span<const char> msg);      // < send back ack to server
    static void handle_NTF_UpdateScoreboard(std::span<const char> msg); // < send back ack to server
    static void handle_NTF_UpdateLeaderboard(std::span<const char> msg); // < send back ack to server

    static void handle_NTF_RoundEndTime(std::span<const char> msg); // < send back ack to server
    
    static void handle_NTF_NewWordLen(std::span<const char> msg);   // < update word len and send ack back to server
    static void handle_NTF_NewWord(std::span<const char> msg);      // < update word len and send ack back to server

    static void handle_NTF_StrokeHistory(std::span<const char> msg);   // < send ack back to server
    static void handle_NTF_MsgHistory(std::span<const char> msg);      // < send ack back to server

    static void startListening(std::stop_token st);

    using ListenMsgFn = void(*)(std::span<const char>);
    static inline std::unordered_map<MessageType, ListenMsgFn> _listenMsgFns
    {
        { MessageType::RSP_START_STROKE,        &Client::handle_RSP_StartStroke         },
        { MessageType::RSP_END_STROKE,          &Client::handle_RSP_EndStroke           },
        { MessageType::RSP_CLEAR_CANVAS,        &Client::handle_RSP_ClearCanvas         },
        { MessageType::RSP_MSG,                 &Client::handle_RSP_Msg                 },

        { MessageType::SVR_START_STROKE,        &Client::handle_SVR_StartStroke         },
        { MessageType::SVR_END_STROKE,          &Client::handle_SVR_EndStroke           },
        { MessageType::SVR_EXTEND_STROKE,       &Client::handle_SVR_ExtendStroke        },

        { MessageType::NTF_MSG,                 &Client::handle_NTF_Msg                 },
        { MessageType::NTF_CLEAR_CANVAS,        &Client::handle_NTF_ClearCanvas         },
        { MessageType::NTF_UPDATE_SCOREBOARD,   &Client::handle_NTF_UpdateScoreboard    },
        { MessageType::NTF_UPDATE_LEADERBOARD,  &Client::handle_NTF_UpdateLeaderboard   },
        { MessageType::NTF_ROUND_END_TIME,      &Client::handle_NTF_RoundEndTime        },
        { MessageType::NTF_SEND_WORD_LEN,       &Client::handle_NTF_NewWordLen          },
        { MessageType::NTF_SEND_WORD,           &Client::handle_NTF_NewWord             },

        { MessageType::NTF_STROKE_HISTORY,      &Client::handle_NTF_StrokeHistory       },
        { MessageType::NTF_MSG_HISTORY,         &Client::handle_NTF_MsgHistory          },
    };

    // ============================================================
    // Helpers
    // ============================================================
    template <typename BufferedT>
    static void tickBuffered(std::mutex& mut,
        std::unordered_map<std::uint32_t, PendingBSESTS>& map,
        std::queue<BufferedT>& queue,
        std::chrono::steady_clock::time_point& now,
        std::string_view name)
    {
        if (auto lk = std::unique_lock(mut, std::try_to_lock); lk.owns_lock() && !queue.empty())
        {
            std::queue<BufferedT> cpyQueue;
            cpyQueue.swap(queue);
            lk.unlock();
            while (!cpyQueue.empty())
            {
                auto buffered = cpyQueue.front();
                cpyQueue.pop();
                sendto(_socket, buffered._data.data(), static_cast<int>(buffered._data.size()),
                    0, reinterpret_cast<sockaddr*>(&_serverAddr), sizeof(_serverAddr));

                // Add to pending with deadlines
                PendingBSESTS pending{
                    ._data = buffered._data,
                    ._nextSendTime = now + std::chrono::milliseconds(100),  // retry interval
                    ._giveUpTime = now + std::chrono::seconds(1),           // total wait
                };
                map[buffered._hostId] = std::move(pending);
            }
        }

        for (auto it = map.begin(); it != map.end();)
        {
            if (now >= it->second._giveUpTime) // no more retries, just give up
            {
                log(std::cerr, std::format("[Client] {} {} timed out",name, it->first));
                it = map.erase(it);
                continue;
            }

            // didnt get back an ack, but exceed attempt time
            // so, send data again, and reset the attempt timer
            if (now >= it->second._nextSendTime)
            {
                sendto(_socket, it->second._data.data(), static_cast<int>(it->second._data.size()),
                    0, reinterpret_cast<sockaddr*>(&_serverAddr), sizeof(_serverAddr));
                it->second._nextSendTime = now + std::chrono::milliseconds(100);
            }
            // move on to the next pending
            ++it;
        }
    }

    static bool sendWithRetry(std::span<const char> data);
};
