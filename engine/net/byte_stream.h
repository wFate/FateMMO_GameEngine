#pragma once
#include <cstdint>
#include <cstring>
#include <string>
#include "engine/core/types.h"

namespace fate {

// ============================================================================
// ByteWriter — writes binary data into a fixed-size buffer
// ============================================================================
class ByteWriter {
public:
    ByteWriter(uint8_t* buffer, size_t capacity)
        : buffer_(buffer), capacity_(capacity) {}

    void writeU8(uint8_t v) { writeRaw(&v, sizeof(v)); }
    void writeU16(uint16_t v) { writeRaw(&v, sizeof(v)); }
    void writeU32(uint32_t v) { writeRaw(&v, sizeof(v)); }
    void writeI32(int32_t v) { writeRaw(&v, sizeof(v)); }
    void writeFloat(float v) { writeRaw(&v, sizeof(v)); }

    void writeVec2(const Vec2& v) {
        writeFloat(v.x);
        writeFloat(v.y);
    }

    void writeString(const std::string& s) {
        uint16_t len = static_cast<uint16_t>(s.size());
        writeU16(len);
        writeBytes(s.data(), len);
    }

    void writeBytes(const void* data, size_t len) {
        writeRaw(data, len);
    }

    size_t size() const { return pos_; }
    bool overflowed() const { return overflow_; }
    const uint8_t* data() const { return buffer_; }

private:
    void writeRaw(const void* data, size_t len) {
        if (overflow_ || pos_ + len > capacity_) {
            overflow_ = true;
            return;
        }
        std::memcpy(buffer_ + pos_, data, len);
        pos_ += len;
    }

    uint8_t* buffer_ = nullptr;
    size_t capacity_ = 0;
    size_t pos_ = 0;
    bool overflow_ = false;
};

// ============================================================================
// ByteReader — reads binary data from a buffer
// ============================================================================
class ByteReader {
public:
    ByteReader(const uint8_t* buffer, size_t size)
        : buffer_(buffer), size_(size) {}

    uint8_t readU8() { uint8_t v = 0; readRaw(&v, sizeof(v)); return v; }
    uint16_t readU16() { uint16_t v = 0; readRaw(&v, sizeof(v)); return v; }
    uint32_t readU32() { uint32_t v = 0; readRaw(&v, sizeof(v)); return v; }
    int32_t readI32() { int32_t v = 0; readRaw(&v, sizeof(v)); return v; }
    float readFloat() { float v = 0; readRaw(&v, sizeof(v)); return v; }

    Vec2 readVec2() {
        float x = readFloat();
        float y = readFloat();
        return {x, y};
    }

    std::string readString() {
        uint16_t len = readU16();
        if (overflow_ || pos_ + len > size_) {
            overflow_ = true;
            return {};
        }
        std::string s(reinterpret_cast<const char*>(buffer_ + pos_), len);
        pos_ += len;
        return s;
    }

    void readBytes(void* dst, size_t len) {
        readRaw(dst, len);
    }

    size_t remaining() const { return overflow_ ? 0 : size_ - pos_; }
    size_t position() const { return pos_; }
    bool overflowed() const { return overflow_; }

private:
    void readRaw(void* dst, size_t len) {
        if (overflow_ || pos_ + len > size_) {
            overflow_ = true;
            std::memset(dst, 0, len);
            return;
        }
        std::memcpy(dst, buffer_ + pos_, len);
        pos_ += len;
    }

    const uint8_t* buffer_ = nullptr;
    size_t size_ = 0;
    size_t pos_ = 0;
    bool overflow_ = false;
};

} // namespace fate
