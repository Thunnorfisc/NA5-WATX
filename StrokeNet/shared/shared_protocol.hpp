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
    // +1 because includes message type byte
    constexpr inline std::size_t HEADER_SIZE = 1 + sizeof(SessionId) + sizeof(SequenceNumber); // 9

    //  1        5
    // [ACK][SEQ_NUMBER]
    constexpr inline std::size_t ACK = 5;

    //  1          32       32
    // [REQ_LOGIN][USERNAME][PASSWORD]
    constexpr inline std::size_t REQ_LOGIN = 1 + MAX_USERNAME_LEN + MAX_PASSWORD_LEN;

    //  1                  32        32
    // [REQ_CREATE_ACCOUNT][USERNAME][PASSWORD]
    constexpr inline std::size_t REQ_CREATE_ACCOUNT = 1 + MAX_USERNAME_LEN + MAX_PASSWORD_LEN;

    //  1         4           1
    // [RSP_LOGIN][SESSION_ID][STATUS]
    constexpr inline std::size_t RSP_LOGIN = 6;

    //  1               4
    // [REQ_UNREGISTER][SESSION_ID]
    constexpr inline std::size_t REQ_UNREGISTER = 5;

    //  1                4           4                4          4               5
    // [PF_START_STROKE][SESSION_ID][SEQUENCE_NUMBER][STROKE_ID][MOUSE_POSITION][RGBAT]
    constexpr inline std::size_t PF_START_STROKE = 22;

    //  1             4           4                4          4
    // [PF_ADD_POINT][SESSION_ID][SEQUENCE_NUMBER][STROKE_ID][MOUSE_POSITION]
    constexpr inline std::size_t PF_ADD_POINT = 17;

    //  1              4           4                4
    // [PF_END_STROKE][SESSION_ID][SEQUENCE_NUMBER][STROKE_ID]
    constexpr inline std::size_t PF_END_STROKE = 13;
}

enum class MessageType: std::uint8_t
{
    REQ_REGISTER = 1,
    RSP_REGISTER = 2,
    REQ_UNREGISTER = 3,
    // 4 was PF_INPUT_STATE — removed
    PF_START_STROKE = 5,
    PF_ADD_POINT = 6,
    PF_END_STROKE = 7,
    REQ_LOGIN = 8,
    REQ_CREATE_ACCOUNT = 9,
    RSP_LOGIN = 10,
    ACK = 11,
};

enum class LoginStatus: std::uint8_t
{
    SUCCESS = 0,
    INVALID_CREDENTIALS,
    USERNAME_TAKEN,
    USERNAME_TOO_LONG,
};

// Canvas draw command — passed between network layer and game logic.
// _msg holds the type-specific payload in HOST byte order (no session/seq header).
//
//   PF_START_STROKE  _msg: [strokeId u32][mousePos u16x2][RGBAT 5 bytes]  = 13 bytes
//   PF_ADD_POINT     _msg: [strokeId u32][mousePos u16x2]                 =  8 bytes
//   PF_END_STROKE    _msg: [strokeId u32]                                  =  4 bytes
struct CanvasDrawCommand
{
    MessageType    _type{};
    SequenceNumber _sqNumberHostOrder{};
    std::uint32_t  _strokeId{};        // stroke this command belongs to
    std::vector<char> _msg;            // type-specific payload (host byte order)
};

struct ReceivedChatMessage
{
    std::string    chatMessage;
    std::string    playerName;
    SequenceNumber serverSequenceHostOrder;
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
// ========================================== NETWORKING SHARED UTILITIES END
