#ifndef UTILCPP_MEMORY_BUFFER_HPP
#define UTILCPP_MEMORY_BUFFER_HPP

#include <cstring>
#include <string>
#include <vector>
#include <cstddef>
#include "meta.hpp"

namespace util
{

struct MemoryBuffer
{
    char* buffer = {};
    size_t ptr = {};
    size_t capacity = {};

    bool Write(const void* data, size_t size, size_t growAmount = 0);
    bool Write(std::string_view data, size_t growAmount = 0);
    bool Write(char byte, size_t growAmount = 0);
    bool Write(uint8_t byte, size_t growAmount = 0) {
        return Write(char(byte), growAmount);
    }
    virtual bool Grow(size_t amount) = 0;
    virtual ~MemoryBuffer() = default;
};

template<typename String = std::string>
struct StringMemoryBuffer final: MemoryBuffer
{
    using size_type = typename String::size_type;
    String Consume() noexcept {
        str.resize(size_type(ptr));
        return std::move(str);
    }
    bool Grow(size_t amount) override {
        auto newStr = str;
        newStr.resize(str.size() + size_type(amount));
        str.swap(newStr);
        buffer = reinterpret_cast<char*>(str.data());
        capacity = size_t(str.size());
        return true;
    }
    StringMemoryBuffer(size_t startSize = 512) {
        str.resize(size_type(startSize));
        buffer = reinterpret_cast<char*>(str.data());
        capacity = size_t(str.size());
    }
protected:
    String str;
};

inline bool MemoryBuffer::Write(const void *data, size_t size, size_t growAmount)
{
    if (util_Unlikely(ptr + size >= capacity)) {
        do {
            if (util_Unlikely(!Grow(growAmount ? growAmount : capacity))) {
                return false;
            }
            auto left = capacity - ptr;
            auto min = std::min(size, left);
            ::memcpy(buffer + ptr, data, min);
            size -= min;
            ptr += min;
            reinterpret_cast<const char*&>(data) += min;
        } while (size);
    } else {
        ::memcpy(buffer + ptr, data, size);
        ptr += size;
    }
    return true;
}

inline bool MemoryBuffer::Write(std::string_view data, size_t growAmount)
{
    return Write(data.data(), data.size(), growAmount);
}

inline bool MemoryBuffer::Write(char byte, size_t growAmount) {
    buffer[ptr++] = byte;
    if (util_Unlikely(ptr == capacity)) {
        return Grow(growAmount ? growAmount : capacity);
    }
    return true;
}

} //util

#endif //UTILCPP_MEMORY_BUFFER_HPP