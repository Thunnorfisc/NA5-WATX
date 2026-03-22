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
#include <cassert>
#include <optional>
// ========================================== PROTOCOL STUFF START
using SessionId = std::uint32_t; // 2 ^ 32 sessions
using SequenceNumber = std::uint32_t; // 2 ^ 32 sequences
using InputBits = std::uint32_t;
using MousePosition = std::array<std::uint16_t, 2>;
inline constexpr SessionId InvalidSessionId = 0;
inline constexpr std::size_t MaxUdpPacketBytes = 65'536;
namespace PacketSize
{
    // +1 because includes message type
    constexpr inline std::size_t HEADER_SIZE = 1 + sizeof(SessionId) + sizeof(SequenceNumber);
    //  1
    // [REQ_REGISTER]
    constexpr inline std::size_t REQ_REGISTER = 1;
    //  1             4
    // [RSP_REGISTER][SESSION_ID]
    constexpr inline std::size_t RSP_REGISTER = 5;
    //  1               4
    // [REQ_UNREGISTER][SESSION_ID]
    constexpr inline std::size_t REQ_UNREGISTER = 5;
    //  1               4           4                4           4
    // [PF_INPUT_STATE][SESSION_ID][SEQUENCE_NUMBER][INPUT_BITS][MOUSE_POSITION]
    constexpr inline std::size_t PF_INPUT_STATE = 17;
    //  1                4           4                4          4               4      1
    // [PF_START_STROKE][SESSION_ID][SEQUENCE_NUMBER][STROKE_ID][MOUSE_POSITION][COLOR][THICKNESS]
    constexpr inline std::size_t PF_START_STROKE = 22;
    //  1             4           4                4
    // [PF_ADD_POINT][SESSION_ID][SEQUENCE_NUMBER][MOUSE_POSITION]
    constexpr inline std::size_t PF_ADD_POINT = 13;
    //  1              4           4
    // [PF_END_STROKE][SESSION_ID][SEQUENCE_NUMBER]
    constexpr inline std::size_t PF_END_STROKE = 9;
}
enum class MessageType : std::uint8_t
{
    REQ_REGISTER = 1,
    RSP_REGISTER,
    REQ_UNREGISTER,

    // need to handle rsp_unregister maybe...

    PF_INPUT_STATE,
    
    PF_START_STROKE,
    PF_ADD_POINT,
    PF_END_STROKE
};
struct CanvasDrawState
{
    // type and msg size will be VALIDATED
    MessageType _type;
    std::vector<char> _msg;
    SequenceNumber _sqNumberHostOrder;

//    void assertCanvasDrawState() const
//    {
//        assert((_type == MessageType::PF_START_STROKE ||
//            _type == MessageType::PF_ADD_POINT ||
//            _type == MessageType::PF_END_STROKE) && "Canvas draw command holds invalid values");
//        // sanity check for the size of the payload for the message type
//#if _DEBUG
//        if (_type == MessageType::PF_ADD_POINT && (_msg.size() != 4))
//            assert(false && "PF_ADD_POINT expects 4 bytes! [uint16_t x][uint16_t y], in host order");
//        else if (_type == MessageType::PF_START_STROKE && (_msg.size() != 13))
//            assert(false && "PF_START_STROKE expects 13 bytes! [uint32_t id][uint16_t x][uint16_t y][uint8_t r][uint8_t g][uint8_t b][uint8_t a][uint8_t thickness], in host order");
//        else if (_type == MessageType::PF_END_STROKE && (_msg.size() != 0))
//            assert(false && "PF_START_STROKE expects 0 bytes!");
//#endif
//    }
};
struct InputState
{
    enum class Input { /*Empty at the moment, no use for it*/ };
    SequenceNumber currentSequenceNumber;
    InputBits currentInput = static_cast<InputBits>(0);
    MousePosition currentMousePos;
    constexpr static inline std::size_t SIZE_OF_INPUT_STATE =
        sizeof(currentSequenceNumber) + sizeof(currentInput) +
        sizeof(currentMousePos);
};
// helpers for input bits
inline void setBit(InputBits& inputBits,std::uint8_t index)
{
    assert((index < sizeof(InputBits) * 8) && "Index passed in must be less than 32!");
    InputBits mask = static_cast<InputBits>(1) << index;
    inputBits |= mask;
}
inline void setAllBits(InputBits& inputBits)
{
    inputBits = ~static_cast<InputBits>(0);
}
inline void clearBit(InputBits& inputBits, std::uint8_t index)
{
    assert((index < sizeof(InputBits) * 8) && "Index passed in must be less than 32!");
    InputBits mask = ~(static_cast<InputBits>(1) << index);
    inputBits &= mask;
}
inline void clearAllBits(InputBits& inputBits)
{
    inputBits = static_cast<InputBits>(0);
}
inline bool test(const InputBits& inputBits,std::uint8_t index)
{
    assert((index < sizeof(InputBits) * 8) && "Index passed in must be less than 32!");
    return static_cast<bool>((inputBits >> index) & static_cast<InputBits>(1));
}
inline bool test(const InputBits& inputBits, InputState::Input input)
{
    return test(inputBits,static_cast<std::uint8_t>(input));
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