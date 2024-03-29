#ifndef MEMBUFF_HPP
#define MEMBUFF_HPP

#include <cstring>
#include <string>
#include <vector>
#include <cstddef>
#include <cstdint>
#include "meta/compiler_macros.hpp"

namespace membuff
{

struct Common {
    size_t ptr = {};
    size_t capacity = {};
    long LastError = {};
};

struct In : Common
{
    const char* buffer = {};

    size_t Available() const noexcept;
    char ReadByte(size_t growAmount = 0);
    size_t Read(char* buff, size_t size, size_t growAmount = 0);
    size_t Read(void* buff, size_t size, size_t growAmount = 0);
    virtual void Refill(size_t amountHint) = 0;
    virtual ~In() = default;
};

struct Out : Common
{
    char* buffer = {};

    void Write(const char* data, size_t size, size_t growAmount = 0);
    void Write(const void* data, size_t size, size_t growAmount = 0);
    void Write(std::string_view data, size_t growAmount = 0);
    void Write(char byte, size_t growAmount = 0);
    void Write(uint8_t byte, size_t growAmount = 0) {
        return Write(char(byte), growAmount);
    }
    virtual void Grow(size_t amountHint) = 0;
    virtual ~Out() = default;
};

template<typename String = std::string>
struct StringOut final: Out
{
    using size_type = typename String::size_type;
    String Consume() noexcept {
        str.resize(size_type(ptr));
        return std::move(str);
    }
    void Grow(size_t amount) override {
        auto newStr = str;
        newStr.resize(str.size() + size_type(amount));
        str.swap(newStr);
        buffer = reinterpret_cast<char*>(str.data());
        capacity = size_t(str.size());
    }
    StringOut(size_t startSize = 512) {
        str.resize(size_type(startSize));
        buffer = reinterpret_cast<char*>(str.data());
        capacity = size_t(str.size());
    }
protected:
    String str;
};

inline void Out::Write(const char *data, size_t size, size_t growAmount)
{
    if (ptr + size >= capacity) {
        do {
            Grow(growAmount ? growAmount : capacity);
            if (meta_Unlikely(LastError)) {
                return;
            }
            auto left = capacity - ptr;
            auto min = std::min meta_NO_MACRO (size, left);
            ::memcpy(buffer + ptr, data, min);
            size -= min;
            ptr += min;
            data += min;
        } while (size);
    } else {
        ::memcpy(buffer + ptr, data, size);
        ptr += size;
    }
}

inline void Out::Write(const void *data, size_t size, size_t growAmount)
{
    Write(static_cast<const char*>(data), size, growAmount);
}

inline void Out::Write(std::string_view data, size_t growAmount)
{
    return Write(data.data(), data.size(), growAmount);
}

inline void Out::Write(char byte, size_t growAmount) {
    LastError = 0;
    buffer[ptr++] = byte;
    if (meta_Unlikely(ptr == capacity)) {
        Grow(growAmount ? growAmount : capacity);
    }
}

inline size_t In::Available() const noexcept
{
    return capacity - ptr;
}

inline char In::ReadByte(size_t growAmount)
{
    LastError = 0;
    if (meta_Unlikely(ptr == capacity)) {
        Refill(growAmount ? growAmount : capacity);
        if (meta_Unlikely(LastError)) {
            return {};
        }
    }
    char res = buffer[ptr++];
    return res;
}

inline size_t In::Read(char *buff, size_t size, size_t growAmount)
{
    if (ptr + size >= capacity) {
        size_t read = 0;
        do {
            Refill(growAmount ? growAmount : capacity);
            if (meta_Unlikely(LastError)) {
                auto left = Available();
                ::memcpy(buff + read, buffer + ptr, left);
                return read + left;
            }
            auto left = capacity - ptr;
            auto min = std::min meta_NO_MACRO (size, left);
            ::memcpy(buff + read, buffer + ptr, min);
            size -= min;
            ptr += min;
            read += min;
        } while (size);
        return read;
    } else {
        ::memcpy(buff, buffer + ptr, size);
        ptr += size;
        return size;
    }
}

inline size_t In::Read(void *buff, size_t size, size_t growAmount)
{
    return Read(static_cast<char*>(buff), size, growAmount);
}

} //jv

#endif //MEMBUFF_HPP
