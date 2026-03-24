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
#include "server.hpp"
#include "shared_protocol.hpp"

#include <Windows.h>
#include <Shlwapi.h>
#pragma comment(lib, "Kernel32.lib")
#pragma comment(lib, "shlwapi.lib")

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

        auto handleCanvasDrawCommandFn = [&server](SessionId sessionIdHostOrder,
            const CanvasDrawCommand& cds)
            {
                // should determine if the client can draw using session id
                // to determine turn
                server.sendCanvasDrawCommand(cds); 
                // will need to refactor later
            };
        auto handleCanvasFnId = server.registerCdsFn(handleCanvasDrawCommandFn);
        server.startListening();
        server.load_wordlist();
        while (!server.isListeningThreadFinished())
        {
            if (server.word.first) {
                server.pick_word();
                //server.word.first = false;
            }


        }
        server.deregisterCdsFn(handleCanvasFnId);
    }
    catch (const std::exception& e)
    {
        std::cerr << "Exception caught from main: " << e.what() << '\n';
    }
}
