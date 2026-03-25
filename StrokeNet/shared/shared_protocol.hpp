/* Start Header
***********************************************************************/

/*! \file   shared_protocol.hpp
    \author Loh Boon Cheong, Timothy
    \par    email: loh.b@digipen.edu
    \co-author William Wibisana Dumanauw
    \par    email: williamwibisana.d@digipen.edu
    \date   20th March, 2026
    \brief  Copyright (C) 2026 DigiPen Institute of Technology

    Reproduction or diclosure of this file or its contents without the prior
    written consent of DigiPen Institute of Technology is prohibited. */

/* End Header
***********************************************************************/
#pragma once
#include <span>
#include <array>
#include <string>
#include <vector>
#include <bitset>
#include <format>
#include <cassert>
#include <cstdint>
#include <cstddef>
#include <optional>

// ========================================== PROTOCOL STUFF START
using SessionId = std::uint32_t;
using SequenceNumber = std::uint32_t;
using InputBits = std::uint32_t;
using MousePosition = std::array<std::uint16_t, 2>;

inline constexpr SessionId   InvalidSessionId = 0;
inline constexpr std::size_t MaxUdpPacketBytes = 65'536;
inline constexpr std::size_t MAX_USERNAME_LEN = 32;
inline constexpr std::size_t MAX_PASSWORD_LEN = 32;
inline constexpr std::uint16_t ServerUdpPort = 32112;

namespace PacketSize
{
    //  1          32       32
    // [REQ_LOGIN][USERNAME][PASSWORD]
    constexpr inline std::size_t REQ_LOGIN = 1 + MAX_USERNAME_LEN + MAX_PASSWORD_LEN;

    //  1                  32        32
    // [REQ_CREATE_ACCOUNT][USERNAME][PASSWORD]
    constexpr inline std::size_t REQ_CREATE_ACCOUNT = 1 + MAX_USERNAME_LEN + MAX_PASSWORD_LEN;

    //  1                             4           1
    // [RSP_LOGIN_AND_CREATE_ACCOUNT][SESSION_ID][STATUS]
    constexpr inline std::size_t RSP_LOGIN_AND_CREATE_ACCOUNT = 6;

    //  1                 4           4          4          5
    // [REQ_START_STROKE][SESSION_ID][STROKE_ID][MOUSE_POS][RGBAT]
    constexpr inline std::size_t REQ_START_STROKE = 18;
    
    //  1                 4           4
    // [RSP_START_STROKE][SESSION_ID][STROKE_ID]
    constexpr inline std::size_t RSP_START_STROKE = 9;

    //  1               4           4
    // [REQ_END_STROKE][SESSION_ID][STROKE_ID]
    constexpr inline std::size_t REQ_END_STROKE = 9;

    //  1                 4           4         
    // [RSP_START_STROKE][SESSION_ID][STROKE_ID]
    constexpr inline std::size_t RSP_END_STROKE = 9;

    //  1        4           4       1           VAR
    // [REQ_MSG][SESSION_ID][MSG_ID][MSG_LENGTH][MSG_BUFFER]
    constexpr inline std::size_t REQ_MSG_WITHOUT_BUFFER = 10;
    
    //  1        4           4
    // [RSP_MSG][SESSION_ID][MSG_ID]
    constexpr inline std::size_t RSP_MSG = 9;

    //  1                  4           4          4
    // [FAF_EXTEND_STROKE][SESSION_ID][STROKE_ID][MOUSE_POS]
    constexpr inline std::size_t FAF_EXTEND_STROKE = 13;

    //  1               4
    // [FAF_DISCONNECT][SESSION_ID]
    constexpr inline std::size_t FAF_DISCONNECT = 5;

    //  1                 4           4          5
    // [SVR_START_STROKE][SESSION_ID][MOUSE_POS][RGBAT]
    constexpr inline std::size_t SVR_START_STROKE = 14;

    //  1               4
    // [SVR_END_STROKE][SESSION_ID]
    constexpr inline std::size_t SVR_END_STROKE = 5;

    //  1                  4           4
    // [SVR_EXTEND_STROKE][SESSION_ID][MOUSE_POS]
    constexpr inline std::size_t SVR_EXTEND_STROKE = 9;

    //  1        4           4       1           VAR
    // [NTF_MSG][SESSION_ID][MSG_ID][MSG_LENGTH][MSG_BUFFER]
    constexpr inline std::size_t NTF_MSG_WITHOUT_BUFFER = 10;

    //  1            4           4
    // [NTF_RCV_MSG][SESSION_ID][MSG_ID]
    constexpr inline std::size_t NTF_RCV_MSG = 9;
}
enum class MessageType: std::uint8_t
{
    // ===================================
    // Require ack - client -> server
    // ===================================
    REQ_LOGIN = 1,                          // < Sent by client
    REQ_CREATE_ACCOUNT,                     // < Sent by client
    RSP_LOGIN_AND_CREATE_ACCOUNT,           // < Ack by server

    REQ_START_STROKE,                       // < Sent by client
    RSP_START_STROKE,                       // < Ack by server

    REQ_END_STROKE,                         // < Sent by client
    RSP_END_STROKE,                         // < Ack by server

    REQ_MSG,                                // < Sent by client
    RSP_MSG,                                // < Ack by server

    // ===================================
    // Require ack - server -> client
    // ===================================
    NTF_MSG,                                // < Sent by server
    NTF_RCV_MSG,                            // < Ack by client

    // ===================================
    // Best effort - client -> server
    // ===================================
    FAF_EXTEND_STROKE,                      // < Sent by client
    FAF_DISCONNECT,                         // < Sent by client

    // ===================================
    // Best effort server -> client(s)
    // ===================================

    SVR_START_STROKE,                       // < Sent by server
    SVR_END_STROKE,                         // < Sent by server
    SVR_EXTEND_STROKE,                      // < Sent by server
};

enum class LoginStatus: std::uint8_t
{
    SUCCESS = 0,
    INVALID_CREDENTIALS,
    USERNAME_TAKEN,
    USERNAME_TOO_LONG,
};

// ========================================== helpers for input bits
inline void setBit(InputBits& inputBits, std::uint8_t index)
{
    assert((index < sizeof(InputBits) * 8) && "Index passed in must be less than 32!");
    inputBits |= static_cast<InputBits>(1) << index;
}
inline void setAllBits(InputBits& inputBits) { inputBits = ~static_cast<InputBits>(0); }
inline void clearBit(InputBits& inputBits, std::uint8_t index)
{
    assert((index < sizeof(InputBits) * 8) && "Index passed in must be less than 32!");
    inputBits &= ~(static_cast<InputBits>(1) << index);
}
inline void clearAllBits(InputBits& inputBits) { inputBits = static_cast<InputBits>(0); }
inline bool test(const InputBits& inputBits, std::uint8_t index)
{
    assert((index < sizeof(InputBits) * 8) && "Index passed in must be less than 32!");
    return static_cast<bool>((inputBits >> index) & static_cast<InputBits>(1));
}

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
        FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
        nullptr, err, 0, (LPSTR)&msg, 0, nullptr);
    std::string result = msg ? msg : "Unknown error";
    LocalFree(msg);
    return result;
}

inline bool isRecoverableWSAError(int err)
{
    switch(err)
    {
    case WSAEWOULDBLOCK:
    case WSAEINTR:
    case WSAETIMEDOUT:
    case WSAECONNRESET:
    case WSAENETRESET:
    case WSAENOBUFS:
    case WSAEINPROGRESS:
    case WSAEALREADY:
        return true;
    default:
        return false;
    }
}

inline bool isRetryableSelectError(int err)
{
    return err == WSAEINTR;
}
// ========================================== WIN SOCK STUFF END

// ========================================== NETWORKING SHARED UTILITIES START
struct ByteWriter
{
    std::vector<char>& buffer;
    std::size_t offset = 0;

    template <typename T>
    void write(T val)
    {
        assert(offset + sizeof(T) <= buffer.size() && "ByteWriter Overflow");
        std::memcpy(buffer.data() + offset, &val, sizeof(T));
        offset += sizeof(T);
    }
};

struct ByteReader
{
    std::span<const char> buffer;
    std::size_t offset = 0;

    template <typename T>
    T read()
    {
        assert(offset + sizeof(T) <= buffer.size() && "ByteReader Overflow");
        T val{};
        std::memcpy(&val, buffer.data() + offset, sizeof(T));
        offset += sizeof(T);
        return val;
    }
};
template <std::size_t N>
struct ByteWriterN
{
    std::array<char,N>& buffer;
    std::size_t offset = 0;

    template <typename T>
    void write(T val)
    {
        assert(offset + sizeof(T) <= buffer.size() && "ByteWriter Overflow");
        std::memcpy(buffer.data() + offset, &val, sizeof(T));
        offset += sizeof(T);
    }
};

template <std::size_t N>
struct ByteReaderN
{
    std::array<const char,N> buffer;
    std::size_t offset = 0;

    template <typename T>
    T read()
    {
        assert(offset + sizeof(T) <= buffer.size() && "ByteReader Overflow");
        T val{};
        std::memcpy(&val, buffer.data() + offset, sizeof(T));
        offset += sizeof(T);
        return val;
    }
};
// ========================================== NETWORKING SHARED UTILITIES END