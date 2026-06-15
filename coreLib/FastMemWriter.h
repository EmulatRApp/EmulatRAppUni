#ifndef FASTMEMWRITER_H
#define FASTMEMWRITER_H

#include <cstdint>
#include <cstring>
#include <type_traits>

class FastMemWriter {
    uint8_t* ptr;
public:
    explicit FastMemWriter(void* buffer) : ptr(static_cast<uint8_t*>(buffer)) {}

    template <typename T>
    FastMemWriter& operator<<(const T& val) noexcept {
        static_assert(std::is_trivially_copyable_v<T>, "Must be POD");
        std::memcpy(ptr, &val, sizeof(T));
        ptr += sizeof(T);
        return *this;
    }
};

class FastMemReader {
    const uint8_t* ptr;
public:
    explicit FastMemReader(const void* buffer) : ptr(static_cast<const uint8_t*>(buffer)) {}

    template <typename T>
    FastMemReader& operator>>(T& val) noexcept {
        static_assert(std::is_trivially_copyable_v<T>, "Must be POD");
        std::memcpy(&val, ptr, sizeof(T));
        ptr += sizeof(T);
        return *this;
    }
};
#endif // FASTMEMWRITER_H
