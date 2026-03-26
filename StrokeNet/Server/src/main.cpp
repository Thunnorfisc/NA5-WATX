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
int main()
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

        const double budgetAdvanceTurn = 15.0; // advance turn every 15 seconds, just for testing
        const int budgetAdvanceTurnInt = static_cast<int>(budgetAdvanceTurn);
        auto advanceTurnNow = std::chrono::steady_clock::now();
        while (!server.isListeningThreadFinished())
        {
            auto now = std::chrono::steady_clock::now();

            if (!server.gameStarted() && server.getNumberOfPlayers() >= 2)
            {
                server.startGame();
                std::int64_t nowMs =
                    std::chrono::duration_cast<std::chrono::milliseconds>
                    (std::chrono::steady_clock::now().time_since_epoch()).count();
                nowMs += (budgetAdvanceTurnInt * 1000);
                server.resetRound(nowMs);
            }
            if (!server.gameStarted()) continue;

            //if (server.word.first) {
            //    server.pick_word();
            //    //server.word.first = false;
            //}

            if (std::chrono::duration<double>(std::chrono::steady_clock::now() - advanceTurnNow).count() >= budgetAdvanceTurn)
            {
                std::int64_t newEndRoundTimeMs =
                    std::chrono::duration_cast<std::chrono::milliseconds>
                    (std::chrono::steady_clock::now().time_since_epoch()).count();
                newEndRoundTimeMs += (budgetAdvanceTurnInt * 1000);
                server.resetRound(newEndRoundTimeMs);
                advanceTurnNow = std::chrono::steady_clock::now();
            }
        }
        server.stopGame();
    }
    catch (const std::exception& e)
    {
        std::cerr << "Exception caught from main: " << e.what() << '\n';
    }
}
