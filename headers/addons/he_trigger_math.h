#ifndef _HE_TRIGGER_MATH_H_
#define _HE_TRIGGER_MATH_H_

#include <stdint.h>

#define HETRIGGER_COUNT 32
#define HETRIGGER_TRAVEL_MAX 1000
#define HETRIGGER_MIN_SPAN 16
#define HETRIGGER_NOISE_FLOOR_MAX 100

struct HEThresholds {
    uint16_t actuation;
    uint16_t deactuation;
    uint16_t pressSensitivity;
    uint16_t releaseSensitivity;
    uint16_t releaseFloor;
    uint8_t rtMode;
};

static inline uint16_t heClampTravel(int64_t value) {
    if (value < 0)
        return 0;
    if (value > HETRIGGER_TRAVEL_MAX)
        return HETRIGGER_TRAVEL_MAX;
    return (uint16_t)value;
}

static inline uint16_t heClampRaw(int32_t value) {
    if (value < 0)
        return 0;
    if (value > 4095)
        return 4095;
    return (uint16_t)value;
}

static inline bool heValidMuxChannels(int32_t channels) {
    return channels == 1 || channels == 4 || channels == 8 || channels == 16;
}

static inline int32_t heClampSmoothingFactor(int32_t value) {
    if (value < 1)
        return 1;
    if (value > 99)
        return 99;
    return value;
}

static inline uint16_t heClampNoiseFloor(uint32_t value) {
    return value > HETRIGGER_NOISE_FLOOR_MAX ? HETRIGGER_NOISE_FLOOR_MAX : (uint16_t)value;
}

static inline uint8_t heClampMuxSettleMicros(int64_t value) {
    if (value < 0)
        return 0;
    if (value > 255)
        return 255;
    return (uint8_t)value;
}

static inline int32_t heTravelScaleQ10(int32_t idle, int32_t pressed) {
    const int64_t span = (int64_t)pressed - idle;
    if (span > -HETRIGGER_MIN_SPAN && span < HETRIGGER_MIN_SPAN)
        return 0;
    return (int32_t)((HETRIGGER_TRAVEL_MAX << 10) / span);
}

static inline uint16_t heTravelFromRaw(int32_t raw, int32_t idle, int32_t scaleQ10) {
    return heClampTravel((((int64_t)raw - idle) * scaleQ10) >> 10);
}

static inline uint16_t heNoiseFromRaw(int32_t noise, int32_t idle, int32_t pressed) {
    const int64_t span = (int64_t)pressed - idle;
    const int64_t magnitude = (span < 0) ? -span : span;
    if (noise <= 0 || magnitude < HETRIGGER_MIN_SPAN)
        return 0;
    const int64_t scaled = ((int64_t)noise * HETRIGGER_TRAVEL_MAX) / magnitude;
    return scaled > HETRIGGER_TRAVEL_MAX ? HETRIGGER_TRAVEL_MAX : (uint16_t)scaled;
}

static inline uint16_t hePressedFromSweep(uint16_t baseline, uint16_t minimum, uint16_t maximum) {
    const uint16_t fromMinimum = baseline - minimum;
    const uint16_t fromMaximum = maximum - baseline;
    return (fromMaximum >= fromMinimum) ? maximum : minimum;
}

static inline HEThresholds heResolveThresholds(uint16_t actuation, uint16_t deactuation,
                                               uint16_t pressSensitivity, uint16_t releaseSensitivity,
                                               uint8_t rtMode, uint16_t noiseFloor) {
    HEThresholds resolved;
    resolved.actuation = actuation ? actuation : 1;
    resolved.deactuation = deactuation ? deactuation : resolved.actuation;
    if (resolved.deactuation > resolved.actuation)
        resolved.deactuation = resolved.actuation;

    const uint16_t release = releaseSensitivity ? releaseSensitivity : pressSensitivity;
    resolved.pressSensitivity = (pressSensitivity > noiseFloor) ? pressSensitivity : noiseFloor;
    resolved.releaseSensitivity = (release > noiseFloor) ? release : noiseFloor;
    resolved.rtMode = rtMode;
    resolved.releaseFloor = (rtMode == 2) ? (noiseFloor ? noiseFloor : 1) : resolved.deactuation;
    return resolved;
}

static inline void heUpdateDigitalState(bool& active, uint16_t& peak, uint16_t& valley,
                                        uint16_t travel, const HEThresholds& thresholds) {
    if (thresholds.rtMode == 0) {
        active = active ? (travel >= thresholds.deactuation) : (travel >= thresholds.actuation);
        return;
    }

    if (active) {
        if (travel > peak)
            peak = travel;
        if ((travel + thresholds.releaseSensitivity <= peak) || (travel < thresholds.releaseFloor)) {
            active = false;
            valley = travel;
        }
        return;
    }

    if (travel < valley)
        valley = travel;
    const bool armed = thresholds.rtMode == 2 || travel >= thresholds.actuation;
    if (armed && travel >= valley + thresholds.pressSensitivity) {
        active = true;
        peak = travel;
    }
}

#endif
