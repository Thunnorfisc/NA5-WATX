/* Start Header
***********************************************************************/

/*! \file   main.cpp
    \author Loh Boon Cheong, Timothy
    \par    email: loh.b@digipen.edu
    \date   20th March, 2026
    \brief  Copyright (C) 2026 DigiPen Institute of Technology

    Reproduction or diclosure of this file or its contents without the prior
    written consent of DigiPen Institute of Technology is prohibited. */

/* End Header
***********************************************************************/
#include "game.hpp"
#include "client.hpp"

#include <Windows.h>
#include <Shlwapi.h>

#include <string>
#include <thread>
#include <stdexcept>
#include <stop_token>
#include <filesystem>
int main()
{
    // set executable path as the working directory
    // https://www.codegenes.net/blog/how-do-i-get-the-directory-that-a-program-is-running-from/
    char buffer[MAX_PATH]{};
    DWORD length = GetModuleFileNameA(nullptr, buffer, MAX_PATH);

    if (length == 0 || length == MAX_PATH)
    {
        throw std::runtime_error("GetModuleFileNameA() failed or path truncated");
    }
    PathRemoveFileSpecA(buffer);
    std::filesystem::current_path(buffer);

    Client::initalize();
    playGame();
    Client::disconnect();
    Client::terminate();
}