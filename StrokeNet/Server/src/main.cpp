/* Start Header
***********************************************************************/

/*! \file   main.cpp
    \author Loh Boon Cheong, Timothy
    \par    email: loh.b@digipen.edu
    \co-author Xavier Koh Zhi Kuang
    \par    email: z.koh@digipen.edu
    \date   20th March, 2026
    \brief  Copyright (C) 2026 DigiPen Institute of Technology

    Reproduction or diclosure of this file or its contents without the prior
    written consent of DigiPen Institute of Technology is prohibited. */

/* End Header
***********************************************************************/
#include "server.hpp"
#include "shared_protocol.hpp"

#include <Windows.h>
#include <Shlwapi.h>
#pragma comment(lib, "Kernel32.lib")
#pragma comment(lib, "shlwapi.lib")

#include <chrono>
#include <string>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <filesystem>
    /*! \brief Loads the UDP configuration, collects server startup input, and launches the server.
        \return Process exit code supplied by the operating system.
    */
std::int64_t _roundEndTime{};
int realMain()
{
    try
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

        Server server;
        server.startListening();
        server.load_wordlist();

        const double fixedFps = 60.0;
        const double budget = 1.0 / 60.0;

        while (!server.isListeningThreadFinished())
        {
            
            if (!server.gameStarted() && server.LOCK_getNumberOfPlayers() >= 1)
            {
                server.startGame();
                _roundEndTime =
                    std::chrono::duration_cast<std::chrono::milliseconds>
                    (std::chrono::steady_clock::now().time_since_epoch()).count();
                _roundEndTime += (server.budgetAdvanceTurnInt * 1000);
                server.advanceTurnNow = std::chrono::steady_clock::now();
                server.resetRound(true);
            }
            else if (server.gameStarted() && server.LOCK_getNumberOfPlayers() == 0) {
                server.stopGame();
            }
            else if (server.gameStarted() && !server.LOCK_getCurrentRound()) {
                server.LOCK_gameVariablesANDclientStorage([&server](auto& map, auto&, auto&) {
                    for (auto& [_sid, client] : map)
                    {
                        if (client.inGame) client.inGame = false;
                    }
                    });
                server.stopGame();
            }

            if (!server.gameStarted()) continue;

            //if (server.word.first) {
            //    server.pick_word();
            //    //server.word.first = false;
            //}

            if (std::chrono::duration<double>(std::chrono::steady_clock::now() - server.advanceTurnNow.load()).count() >= server.budgetAdvanceTurn ||
                server.LOCK_gameVariablesANDclientStorage([&server](auto& map, auto&, auto& drawerSessionOpt) {
                    bool anyInGame = false;
                    for (auto& [_sid, client] : map) {
                        if (!client.inGame || drawerSessionOpt == _sid) continue;
                        anyInGame = true;
                        if (!client.wordAlreadyGuessed) return false;
                    }
                    return anyInGame;
                    }))
            {
                _roundEndTime =
                    std::chrono::duration_cast<std::chrono::milliseconds>
                    (std::chrono::steady_clock::now().time_since_epoch()).count();
                _roundEndTime += (server.budgetAdvanceTurnInt * 1000);
                //server.LOCK_forceEndStroke();
                server.resetRound();
                server.advanceTurnNow = std::chrono::steady_clock::now();
            }
        }
        server.stopGame();
    }
    catch (const std::exception& e)
    {
        std::cerr << "Exception caught from main: " << e.what() << '\n';
    }
    return 1;
}

#ifdef _DEBUG
int main()
{
    return realMain();
}
#else
int
#if defined(_M_CEE_PURE)
__clrcall
#else
WINAPI
#endif
wWinMain(
    _In_ HINSTANCE ,
    _In_opt_ HINSTANCE ,
    _In_ LPWSTR ,
    _In_ int 
)
{
    return realMain();
}
#endif