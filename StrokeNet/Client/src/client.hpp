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
#include <string>

class Client
{
public:
public:
    static void initalize();
    static void terminate();
    // no separate thread, will do on the main thread
    static bool connect(std::string serverIp, std::string serverPort);
    // no separate thread, will do on the main thread
    static void sendInputState(const InputState& inputState);
private:
    static inline SOCKET _socket = INVALID_SOCKET;
    static inline unsigned _portHostOrder = 0;
    static inline std::string _ip;
    static inline const double _recvTimeOut = 0.05; // 0.05 seconds before retrying, for 3 retry times, meaning 0.15 second max time for connect to return (6fps)
    static inline const int _maxRetries = 3;
    
    static inline SessionId _sessionId = InvalidSessionId;
    static inline sockaddr_in _serverAddr;
};
