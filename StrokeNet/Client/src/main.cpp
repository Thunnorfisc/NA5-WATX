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
    //Client::connectViaBroadcast(); // auto check lan for connection via a fixed port
    //Client::connectViaIpAndPort("192.168.68.54","49013"); // connect directly
    playGame();
    Client::disconnect();
    Client::terminate();
}