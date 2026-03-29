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

#include <openssl/evp.h>

// ========================================== PROTOCOL STUFF START
using SessionId = std::uint32_t;
using SequenceNumber = std::uint32_t;
using InputBits = std::uint32_t;
using MousePosition = std::array<std::uint16_t, 2>;

inline constexpr SessionId   InvalidSessionId = 0;
inline constexpr std::size_t MaxUdpPacketBytes = 540;
inline constexpr std::size_t MAX_USERNAME_LEN = 32;
inline constexpr std::size_t MAX_PASSWORD_LEN = 32;
inline constexpr std::uint16_t ServerUdpPort = 32112;

inline constexpr std::size_t MAX_WORD_LEN = 255;
inline constexpr std::size_t MAX_PLAYERS_IN_GAME = 6;
inline constexpr std::size_t MAX_CHAT_HISTORY_SHOWN = 15;
inline constexpr std::size_t MAX_SHOWN_USERNAME_LEN = 15;
inline constexpr std::size_t MAX_CHARS_PER_CHAT_MSG = 84;

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

    //  1               4           4
    // [REQ_END_STROKE][SESSION_ID][CLEAR_ID]
    constexpr inline std::size_t REQ_CLEAR_CANVAS = 9;

    //  1                 4           4         
    // [RSP_START_STROKE][SESSION_ID][CLEAR_ID]
    constexpr inline std::size_t RSP_CLEAR_CANVAS = 9;

    //  1        4           4       1           VAR
    // [REQ_MSG][SESSION_ID][MSG_ID][MSG_LENGTH][MSG_BUFFER]
    constexpr inline std::size_t REQ_MSG_WITHOUT_BUFFER = 10;
    static_assert(REQ_MSG_WITHOUT_BUFFER + MAX_CHARS_PER_CHAT_MSG <= MaxUdpPacketBytes,
        "REQ_MSG_WITHOUT_BUFFER packet exceeds MaxUdpPacketBytes");
    
    //  1        4           4
    // [RSP_MSG][SESSION_ID][MSG_ID]
    constexpr inline std::size_t RSP_MSG = 9;

    //  1              4
    // [REQ_PLAY_GAME][SESSION_ID]
    constexpr inline std::size_t REQ_PLAY_GAME = 5;

    //  1              4           1
    // [RSP_PLAY_GAME][SESSION_ID][PLAY_GAME_STATUS]
    constexpr inline std::size_t RSP_PLAY_GAME = 6;

    //  1              4
    // [REQ_QUIT_GAME][SESSION_ID]
    constexpr inline std::size_t REQ_QUIT_GAME = 5;

    //  1              4
    // [RSP_QUIT_GAME][SESSION_ID]
    constexpr inline std::size_t RSP_QUIT_GAME = 5;

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

    //  1        4           4       1           VAR         1            VAR
    // [NTF_MSG][SESSION_ID][MSG_ID][MSG_LENGTH][MSG_BUFFER][NAME_LENGTH][NAME_BUFFER]
    constexpr inline std::size_t NTF_MSG_WITHOUT_BUFFER = 11;
    static_assert(NTF_MSG_WITHOUT_BUFFER + MAX_CHARS_PER_CHAT_MSG + MAX_SHOWN_USERNAME_LEN <= MaxUdpPacketBytes,
        "NTF_MSG_WITHOUT_BUFFER packet exceeds MaxUdpPacketBytes");

    //  1            4           4
    // [NTF_RCV_MSG][SESSION_ID][MSG_ID]
    constexpr inline std::size_t NTF_RCV_MSG = 9;

    //  1                 4           4
    // [NTF_CLEAR_CANVAS][SESSION_ID][CLEAR_ID]
    constexpr inline std::size_t NTF_CLEAR_CANVAS = 9;

    //  1                     4           4
    // [NTF_RCV_CLEAR_CANVAS][SESSION_ID][CLEAR_ID]
    constexpr inline std::size_t NTF_RCV_CLEAR_CANVAS = 9;

    //  1                 4            4            1
    // [NTF_SEND_WORD_LEN][SESSION_ID][WORD_LEN_ID][WORD_LEN]
    constexpr inline std::size_t NTF_SEND_WORD_LEN = 10;

    //  1                       4          4
    // [NTF_RCV_SEND_WORD_LEN][SESSION_ID][WORD_LEN_ID]
    constexpr inline std::size_t NTF_RCV_SEND_WORD_LEN = 9;

    //  1                 4            4        1        VAR
    // [NTF_SEND_WORD_LEN][SESSION_ID][WORD_ID][WORD_LEN][WORD]
    constexpr inline std::size_t NTF_SEND_WORD = 10;
    static_assert(NTF_SEND_WORD + MAX_WORD_LEN <= MaxUdpPacketBytes,
        "NTF_SEND_WORD packet exceeds MaxUdpPacketBytes");

    //  1                       4          4
    // [NTF_RCV_SEND_WORD_LEN][SESSION_ID][WORD_ID]
    constexpr inline std::size_t NTF_RCV_SEND_WORD = 9;

    // 1                      4           4         4             N                                    1           VAR
    // [NTF_UPDATE_SCOREBOARD][SESSION_ID][SCORE_ID][NUM_PLAYERS]{ NAME_LEN(1) | NAME(VAR) | SCORE(2) }[DRAWER_LEN][DRAWER_NAME]
    constexpr inline std::size_t NTF_UPDATE_SCOREBOARD_BASE = 14;
    static_assert(NTF_UPDATE_SCOREBOARD_BASE + 
        MAX_SHOWN_USERNAME_LEN +  // < drawer name
        (MAX_PLAYERS_IN_GAME * (1 * MAX_SHOWN_USERNAME_LEN + 2)) <= MaxUdpPacketBytes,
        "NTF_UPDATE_SCOREBOARD_BASE packet exceeds MaxUdpPacketBytes");

    // 1                     4           4        
    // [NTF_RCV_PLAYER_JOIN][SESSION_ID][SCORE_ID]
    constexpr inline std::size_t NTF_RCV_UPDATE_SCOREBOARD = 9;

    //  1                   4           4                  8
    // [NTF_ROUND_END_TIME][SESSION_ID][ROUND_END_TIME_ID][ROUND_END_TIME]
    constexpr inline std::size_t NTF_ROUND_END_TIME = 17;

    //  1                       4           4
    // [NTF_RCV_ROUND_END_TIME][SESSION_ID][ROUND_END_TIME_ID]
    constexpr inline std::size_t NTF_RCV_ROUND_END_TIME = 9;

    //  1                   4           4                      4                                               2                        4                  NUMBER_OF_HISTORY
    // [NTF_STROKE_HISTORY][SESSION_ID][NTF_STROKE_HISTORY_ID][NUMBER_OF_TOTAL_BYTES_IN_ALL_RAW_STROKE_CHUNKS][NTF_STROKE_CHUNK_NUMBER][NUMBER_OF_HISTORY]{ STROKE_TYPE(1) | DATA(VAR) }
    constexpr inline std::size_t NTF_STROKE_HISTORY_WITHOUT_DATA = 19;

    //  1                       4           4                     2
    // [NTF_RCV_STROKE_HISTORY][SESSION_ID][NTF_STROKE_HISTORY_ID][NTF_STROKE_CHUNK_NUMBER]
    constexpr inline std::size_t NTF_RCV_STROKE_HISTORY = 11;

    //  1                4           4                   4                                        2                     2                    NUMBER_OF_MESSAGES 
    // [NTF_MSG_HISTORY][SESSION_ID][NTF_MSG_HISTORY_ID][NUMBER_OF_TOTAL_BYTES_IN_ALL_MSG_CHUNKS][NTF_MSG_CHUNK_NUMBER][NUMBER_OF_MESSAGES]{ MSG_LEN(1) | MSG_BUFFER | NAME_LEN(1) | NAME_BUFFER }
    constexpr inline std::size_t NTF_MSG_HISTORY_WITHOUT_DATA = 17;

    //  1                    4           4                   2
    // [NTF_RCV_MSG_HISTORY][SESSION_ID][NTF_MSG_HISTORY_ID][NTF_MSG_CHUNK_NUMBER] 
    constexpr inline std::size_t NTF_RCV_MSG_HISTORY = 11;

    //  4          5
    // [MOUSE_POS][RGBAT]
    constexpr inline std::size_t PAST_HISTORY_START_STROKE = 9;

    //  4         
    // [MOUSE_POS]
    constexpr inline std::size_t PAST_HISTORY_EXTEND_STROKE = 4;

    constexpr inline std::size_t PAST_HISTORY_END_STROKE = 0;
}
enum class MessageType: std::uint8_t
{
    // ===================================
    // Require ack - client -> server
    // ===================================
    REQ_LOGIN = 1,                          // < Sent by client
    REQ_CREATE_ACCOUNT,                     // < Sent by client
    RSP_LOGIN_AND_CREATE_ACCOUNT,           // < Ack by server

    REQ_PLAY_GAME,                          // < Sent by client
    RSP_PLAY_GAME,                          // < Acked by server

    REQ_QUIT_GAME,                          // < Sent by client
    RSP_QUIT_GAME,                          // < Acked by server

    REQ_START_STROKE,                       // < Sent by client
    RSP_START_STROKE,                       // < Ack by server

    REQ_END_STROKE,                         // < Sent by client
    RSP_END_STROKE,                         // < Ack by server

    REQ_CLEAR_CANVAS,                       // < Sent by client
    RSP_CLEAR_CANVAS,                       // < Ack by server

    REQ_MSG,                                // < Sent by client
    RSP_MSG,                                // < Ack by server

    // ===================================
    // Require ack - server -> client
    // ===================================
    NTF_MSG,                                // < Sent by server
    NTF_RCV_MSG,                            // < Ack by client

    NTF_CLEAR_CANVAS,                       // < Sent by server
    NTF_RCV_CLEAR_CANVAS,                   // < Ack by client

    NTF_UPDATE_SCOREBOARD,                  // < Sent by server
    NTF_RCV_UPDATE_SCOREBOARD,              // < Ack by client

    NTF_ROUND_END_TIME,                     // < Sent by server
    NTF_RCV_ROUND_END_TIME,                 // < Ack by client

    NTF_SEND_WORD_LEN,                      // < Sent by server
    NTF_RCV_SEND_WORD_LEN,                  // < Ack by client

    NTF_SEND_WORD,                          // < Sent by server
    NTF_RCV_SEND_WORD,                      // < Ack by client

    NTF_MSG_HISTORY,                        // < Sent by server
    NTF_RCV_MSG_HISTORY,                    // < Ack by client

    NTF_STROKE_HISTORY,                     // < Sent by server
    NTF_RCV_STROKE_HISTORY,                 // < Ack by client

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
    ALREADY_LOGGED_IN
};

enum class PlayGameStatus : std::uint8_t
{
    SUCCESS = 0,
    TOO_MANY_PLAYERS,
    SERVER_NO_RESPONSE
};

struct PastStroke
{
    enum class Type : std::uint8_t { START_STROKE, EXTEND_STROKE, END_STROKE };
    Type _type;
    std::vector<char> _data;
};

struct PastMessage
{
    std::string _message;
    std::string _name;
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

    void writeSpan(std::span<const char> data)
    {
        assert(offset + data.size() <= buffer.size() && "ByteWriter Overflow");
        std::memcpy(buffer.data() + offset, data.data(), data.size());
        offset += data.size();
    }
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


    std::vector<char> readBytes(std::size_t N)
    {
        assert(offset + N <= buffer.size() && "ByteReader Overflow");
        std::vector<char> readBytes;
        readBytes.resize(N);
        std::memcpy(readBytes.data(), buffer.data() + offset, N);
        offset += N;
        return readBytes;
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

// SHA 256 encryption using openssl for password hashing
inline void hashbrown256(const std::string& str, unsigned char* digest) {
    EVP_MD_CTX* ctx = EVP_MD_CTX_new();
    EVP_DigestInit_ex(ctx, EVP_sha256(), NULL);
    EVP_DigestUpdate(ctx, str.c_str(), str.length());
    EVP_DigestFinal_ex(ctx, digest, NULL);
    EVP_MD_CTX_free(ctx);
}

inline void Encode_sha_2_64(std::string const& data, unsigned char* ret_encoded){
    //int encodedLen = 4 * ((len + 2) / 3);
    //std::string ret(encodedLen, '\0');

    EVP_EncodeBlock(ret_encoded, reinterpret_cast<unsigned char const *>(data.c_str()), static_cast<int>(data.size()));
}

inline std::string Decode_64_2_sha(const std::string& input){
    int len = static_cast<int>(input.size());
    std::string out(len, '\0');  // allocate properly

    int decodedLen = EVP_DecodeBlock(
        reinterpret_cast<unsigned char*>(&out[0]),
        reinterpret_cast<const unsigned char*>(input.c_str()),
        len
    );

    if (decodedLen < 0)
        return {};

    // handle padding
    int padding = 0;
    if (len >= 1 && input[len - 1] == '=') padding++;
    if (len >= 2 && input[len - 2] == '=') padding++;

    out.resize(decodedLen - padding);

    return out;
}
// ========================================== NETWORKING SHARED UTILITIES END