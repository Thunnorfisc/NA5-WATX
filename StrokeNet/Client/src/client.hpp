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
    Client();
    ~Client();
private:
    SOCKET _socket = INVALID_SOCKET;
    unsigned _portHostOrder = 0;
    std::string _ip;
};
