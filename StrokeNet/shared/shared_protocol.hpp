#pragma once
#include <string>
#include <cstdint>
// ========================================== PROTOCOL STUFF START
using SessionId = std::uint32_t;
inline constexpr SessionId InvalidSessionId = 0;
inline constexpr std::size_t MaxUdpPacketBytes = 65'536;
enum class MessageType : std::uint8_t
{
    REQ_REGISTER = 1,
    RSP_REGISTER,
    REQ_UNREGISTER,

    // need to handle rsp_unregister...
};
struct GameState
{

};
struct InputState
{

};
// ========================================== PROTOCOL STUFF END
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#include <Windows.h>
#include <winsock2.h>
#include <WS2tcpip.h>
#include <iphlpapi.h>
// ========================================== WIN SOCK STUFF START
inline std::string wsaErrorStr()
{
    int err = WSAGetLastError();
    char* msg = nullptr;

    FormatMessageA(
        FORMAT_MESSAGE_ALLOCATE_BUFFER |
        FORMAT_MESSAGE_FROM_SYSTEM |
        FORMAT_MESSAGE_IGNORE_INSERTS,
        nullptr,
        err,
        0,
        (LPSTR)&msg,
        0,
        nullptr
    );

    std::string result = msg ? msg : "Unknown error";
    LocalFree(msg);
    return result;
}
inline bool isRecoverableWSAError(int err)
{
    switch (err)
    {
        // --- Non-fatal / expected conditions ---
    case WSAEWOULDBLOCK:     // no data available (non-blocking socket)
    case WSAEINTR:           // interrupted call
    case WSAETIMEDOUT:       // timeout (common for UDP)
    case WSAECONNRESET:      // UDP: ICMP port unreachable
    case WSAENETRESET:       // connection dropped temporarily
    case WSAENOBUFS:         // buffer pressure, can retry
    case WSAEINPROGRESS:     // async still in progress
    case WSAEALREADY:        // operation already ongoing
        return true;

        // --- Everything else: treat as fatal ---
    default:
        return false;
    }
}
// ========================================== WIN SOCK STUFF END