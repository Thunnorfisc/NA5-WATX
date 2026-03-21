/* Start Header
***********************************************************************/

/*! \file   client.hpp
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
#include <mutex>
#include <string>
#include <vector>
#include <stop_token>
#include <functional>
#include <unordered_map>
class Client
{
public:
    using RegCanvasStateFnId = int;
public:
    static void initalize();
    static void terminate();
    // separate listening thread
    static void startListening(std::stop_token st);
    // no separate thread, will do on the main thread (for now)
    static bool connect(std::string serverIp, std::string serverPort);
    // no separate thread, will do on the main thread (for now)
    static void sendInputState(const InputState& inputState);
    // no separate thread, will do on main thread (for now)
    static void sendCanvasCommand(const CanvasDrawState& drawState);
    // for getting the canvas state when it arrives
    static RegCanvasStateFnId registerCanvasStateCommandEvent(std::function<void(const CanvasDrawState&)> fn);
    static void deregisterCanvasStateCommandEvent(RegCanvasStateFnId id);
private:
    static inline SOCKET _socket = INVALID_SOCKET;
    static inline unsigned _portHostOrder = 0;
    static inline std::string _ip;
    static inline const double _recvTimeOut = 0.05; // 0.05 seconds before retrying, for 3 retry times, meaning 0.15 second max time for connect to return (6fps)
    static inline const int _maxRetries = 3;
    
    static inline SessionId _sessionId = InvalidSessionId;
    static inline sockaddr_in _serverAddr;

    static inline RegCanvasStateFnId _nextCanvasStateFnId = 1;
    static inline std::unordered_map<RegCanvasStateFnId,
        std::function<void(const CanvasDrawState&)>> _canvasDrawStateFunctions;
    static inline std::mutex _canvasDrawStateFunctionsMutex;
};
