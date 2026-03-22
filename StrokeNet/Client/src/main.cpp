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
    std::stop_source ss;


    Client::initalize();
    Client::connect("192.168.68.56", "55374");
    std::jthread listeningThread{
        [st = ss.get_token()]()
        {
            Client::startListening(st);
        }
    };
    playGame();
    Client::disconnect();

    if (listeningThread.joinable())
    {
        ss.request_stop();
        listeningThread.join();
    }

    Client::terminate();
}