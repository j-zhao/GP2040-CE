#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <mutex>

struct DisplayFrame {
    uint16_t width = 0;
    uint16_t height = 0;
    bool isButtonLayout = false;
    uint8_t flip = 0;
    bool invert = false;
    std::array<uint8_t, 1024> pixels{};

    size_t byteSize() const { return static_cast<size_t>(width) * height / 8; }

    template<class ReadPixel>
    void capture(uint16_t frameWidth, uint16_t frameHeight, bool buttons,
                 uint8_t orientation, bool inverted, ReadPixel readPixel) {
        *this = {};
        if (frameWidth != 128 || (frameHeight != 32 && frameHeight != 64)) return;
        width = frameWidth;
        height = frameHeight;
        isButtonLayout = buttons;
        flip = orientation;
        invert = inverted;
        for (uint16_t y = 0; y < height; y++) {
            for (uint16_t x = 0; x < width; x++) {
                if (readPixel(x, y)) pixels[(y * width + x) / 8] |= 0x80 >> (x % 8);
            }
        }
    }
};

// One producer fills the pending slot. Readers only copy the published slot.
template<class Mutex>
class DisplayFrameMailbox {
public:
    explicit DisplayFrameMailbox(Mutex& mutex) : mutex(mutex) {}

    bool beginCapture() {
        std::lock_guard<Mutex> lock(mutex);
        if (!requested) return false;
        requested = false;
        return true;
    }

    DisplayFrame& pending() { return frames[current ^ 1]; }

    void publish() {
        std::lock_guard<Mutex> lock(mutex);
        current ^= 1;
    }

    void copy(DisplayFrame& destination) {
        std::lock_guard<Mutex> lock(mutex);
        destination = frames[current];
        requested = true;
    }

private:
    Mutex& mutex;
    std::array<DisplayFrame, 2> frames{};
    size_t current = 0;
    bool requested = true;
};
