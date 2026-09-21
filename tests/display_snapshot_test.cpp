#include "display/DisplayFrame.h"

#include <atomic>
#include <stdexcept>
#include <thread>

static void check(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

static void checkPacking(uint16_t height) {
    DisplayFrame frame;
    frame.capture(128, height, true, 3, true,
                  [height](uint16_t x, uint16_t y) {
                      return (x == 0 || x == 127) && (y == 0 || y == height - 1);
                  });
    check(frame.byteSize() == 16u * height, "packed size");
    check(frame.isButtonLayout && frame.flip == 3 && frame.invert, "active metadata");
    for (size_t i = 0; i < frame.pixels.size(); i++) {
        uint8_t expected = 0;
        if (i == 0 || i == 16u * (height - 1)) expected = 0x80;
        if (i == 15 || i == 16u * height - 1) expected = 0x01;
        check(frame.pixels[i] == expected, "corner pixel packing");
    }
    frame.capture(256, height, true, 3, true,
                  [](uint16_t, uint16_t) -> bool {
                      throw std::runtime_error("unsupported frame reads pixels");
                  });
    check(frame.byteSize() == 0 && frame.flip == 0 && !frame.invert, "invalid frame resets metadata");
}

static void checkLifecycle() {
    std::mutex mutex;
    DisplayFrameMailbox<std::mutex> mailbox(mutex);
    DisplayFrame result;
    mailbox.copy(result);
    check(result.byteSize() == 0, "first read must be empty");
    check(mailbox.beginCapture(), "initial capture requested");
    check(!mailbox.beginCapture(), "request must be consumed once");
    mailbox.pending().capture(128, 32, true, 2, false,
                              [](uint16_t x, uint16_t y) { return x == y; });
    mailbox.publish();
    check(!mailbox.beginCapture(), "idle display must not capture");
    mailbox.copy(result);
    check(result.height == 32 && result.isButtonLayout && result.flip == 2, "published frame");
    mailbox.copy(result);
    check(mailbox.beginCapture(), "reads request fresh capture");
    check(!mailbox.beginCapture(), "multiple reads must coalesce");
    mailbox.pending() = {};
    mailbox.copy(result);
    check(result.height == 32, "pending frame must remain unpublished");
    mailbox.publish();
    check(mailbox.beginCapture(), "request during capture must survive publication");
    mailbox.copy(result);
    check(result.byteSize() == 0 && !result.isButtonLayout, "unavailable display must clear frame");
    mailbox.pending().capture(128, 64, false, 1, true,
                              [](uint16_t, uint16_t) { return true; });
    mailbox.publish();
    mailbox.copy(result);
    check(result.height == 64 && !result.isButtonLayout && result.invert, "display recovery and mode");
    check(result.pixels.back() == 0xff, "recovered frame contents");
}

static void checkConcurrentPublication() {
    std::mutex mutex;
    DisplayFrameMailbox<std::mutex> mailbox(mutex);
    std::atomic<bool> done{false};
    std::thread producer([&] {
        for (unsigned sequence = 1; sequence <= 10000; sequence++) {
            while (!mailbox.beginCapture()) std::this_thread::yield();
            const uint8_t value = sequence % 251 + 1;
            auto& frame = mailbox.pending();
            frame.width = 128;
            frame.height = (value & 1) ? 32 : 64;
            frame.flip = value % 4;
            frame.invert = (value & 2) != 0;
            frame.isButtonLayout = (value & 4) != 0;
            frame.pixels.fill(value);
            mailbox.publish();
        }
        done = true;
    });
    size_t reads = 0;
    do {
        DisplayFrame frame;
        mailbox.copy(frame);
        if (frame.width == 0) continue;
        const uint8_t value = frame.pixels[0];
        check(frame.width == 128, "concurrent width");
        check(frame.height == ((value & 1) ? 32 : 64), "concurrent height");
        check(frame.flip == value % 4, "concurrent orientation");
        check(frame.invert == ((value & 2) != 0), "concurrent inversion");
        check(frame.isButtonLayout == ((value & 4) != 0), "concurrent mode");
        for (uint8_t pixel : frame.pixels) check(pixel == value, "torn frame");
        reads++;
    } while (!done);
    producer.join();
    check(reads > 0, "reader must observe publications");
}

int main() {
    checkPacking(32);
    checkPacking(64);
    checkLifecycle();
    checkConcurrentPublication();
}
