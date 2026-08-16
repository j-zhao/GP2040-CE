#ifndef _HE_Trigger_H
#define _HE_Trigger_H

#include "gpaddon.h"

#define HETRIGGER_COUNT 32

// Travel is expressed in tenths of a percent of the calibrated idle-to-pressed
// span, 0-1000. The span carries its own direction: a sensor that reads high at
// rest simply calibrates with pressed below idle, which makes the scale negative
// and needs no separate polarity branch when sampling.
#define HETRIGGER_TRAVEL_MAX 1000

// Smallest usable idle-to-pressed span in ADC counts. Anything narrower cannot be
// scaled without amplifying noise past the point of being useful.
#define HETRIGGER_MIN_SPAN 16

// The noise floor is the lower bound on both rapid trigger sensitivities, so it
// has to stay well under full travel or rapid trigger could never fire.
#define HETRIGGER_NOISE_FLOOR_MAX 100

// Scale is Q10 so that the largest product, a full 12-bit swing times the scale of
// the narrowest allowed span, still fits in an int32_t.
static inline int32_t heTravelScaleQ10(int32_t idle, int32_t pressed) {
    const int32_t span = pressed - idle;
    if (span > -HETRIGGER_MIN_SPAN && span < HETRIGGER_MIN_SPAN)
        return 0; // uncalibrated, every reading maps to zero travel
    return (HETRIGGER_TRAVEL_MAX << 10) / span;
}

static inline uint16_t heTravelFromRaw(int32_t raw, int32_t idle, int32_t scaleQ10) {
    const int32_t travel = ((raw - idle) * scaleQ10) >> 10;
    if (travel < 0)
        return 0;
    if (travel > HETRIGGER_TRAVEL_MAX)
        return HETRIGGER_TRAVEL_MAX;
    return (uint16_t)travel;
}

// The thresholds a channel actually runs on, once the stored values have been
// resolved. Kept here so that the per-channel settings and the per-profile
// override cannot resolve the same rules differently.
struct HEThresholds {
    uint16_t actuation;
    uint16_t deactuation;
    uint16_t pressSensitivity;
    uint16_t releaseSensitivity;
    // Travel below which a rapid trigger press always releases, whatever the
    // release sensitivity says.
    uint16_t releaseFloor;
    uint8_t  rtMode;
};

// A stored 0 means "mirror the paired value". An actuation point of 0 would be
// satisfied by a key at rest, a deactuation point deeper than actuation would
// never release, and a sensitivity below the noise floor would fire on noise.
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

    // Rapid trigger releases on movement away from the deepest point, but a
    // release sensitivity larger than the depth reached would never be
    // satisfied and would latch the key down. Normal mode already has the
    // deactuation point as its floor; continuous mode drops that gate, so it
    // falls back to "the key is all the way up".
    resolved.releaseFloor = (rtMode == 2 /* HE_RT_CONTINUOUS */)
        ? (noiseFloor ? noiseFloor : 1)
        : resolved.deactuation;
    return resolved;
}

#ifndef HETRIGGER_ENABLED
#define HETRIGGER_ENABLED 0
#endif

#ifndef HETRIGGER_MUX_CHANNELS
#define HETRIGGER_MUX_CHANNELS 8
#endif

#ifndef HETRIGGER_S0_PIN
#define HETRIGGER_S0_PIN -1
#endif
#ifndef HETRIGGER_S1_PIN
#define HETRIGGER_S1_PIN -1
#endif
#ifndef HETRIGGER_S2_PIN
#define HETRIGGER_S2_PIN -1
#endif
#ifndef HETRIGGER_S3_PIN
#define HETRIGGER_S3_PIN -1
#endif

#ifndef HETRIGGER_ADC0
#define HETRIGGER_ADC0 -1
#endif
#ifndef HETRIGGER_ADC1
#define HETRIGGER_ADC1 -1
#endif
#ifndef HETRIGGER_ADC2
#define HETRIGGER_ADC2 -1
#endif
#ifndef HETRIGGER_ADC3
#define HETRIGGER_ADC3 -1
#endif

#ifndef HETRIGGER_SMOOTHING_ENABLED
#define HETRIGGER_SMOOTHING_ENABLED 1
#endif

#ifndef HETRIGGER_SMOOTHING_FACTOR
#define HETRIGGER_SMOOTHING_FACTOR 5
#endif

#ifndef HETRIGGER_DEFAULT_IDLE
#define HETRIGGER_DEFAULT_IDLE 150
#endif

#ifndef HETRIGGER_DEFAULT_PRESSED
#define HETRIGGER_DEFAULT_PRESSED 3500
#endif

// Travel-based defaults, in tenths of a percent of calibrated travel (0-1000)
#ifndef HETRIGGER_DEFAULT_ACTUATION
#define HETRIGGER_DEFAULT_ACTUATION 450
#endif

// 0 mirrors the actuation point
#ifndef HETRIGGER_DEFAULT_DEACTUATION
#define HETRIGGER_DEFAULT_DEACTUATION 0
#endif

#ifndef HETRIGGER_DEFAULT_RT_MODE
#define HETRIGGER_DEFAULT_RT_MODE HERapidTriggerMode::HE_RT_OFF
#endif

#ifndef HETRIGGER_DEFAULT_RT_PRESS_SENS
#define HETRIGGER_DEFAULT_RT_PRESS_SENS 30
#endif

// 0 mirrors the press sensitivity
#ifndef HETRIGGER_DEFAULT_RT_RELEASE_SENS
#define HETRIGGER_DEFAULT_RT_RELEASE_SENS 0
#endif

// 0 is no partner, otherwise the partner channel index plus one
#ifndef HETRIGGER_DEFAULT_SOCD_PARTNER
#define HETRIGGER_DEFAULT_SOCD_PARTNER 0
#endif

#ifndef HETRIGGER_NOISE_FLOOR
#define HETRIGGER_NOISE_FLOOR 10
#endif

#ifndef HETRIGGER_ANALOG_DEADZONE
#define HETRIGGER_ANALOG_DEADZONE 50
#endif

#ifndef HETRIGGER_ANALOG_CURVE
#define HETRIGGER_ANALOG_CURVE HEAnalogCurve::HE_CURVE_LINEAR
#endif

#ifndef HETRIGGER_MUX_SETTLE_US
#define HETRIGGER_MUX_SETTLE_US 3
#endif

#ifndef HETRIGGER_ANALOG_PROPORTIONAL
#define HETRIGGER_ANALOG_PROPORTIONAL 0
#endif

// 32 possible HE triggers
#ifndef HETRIGGER_HE0_ACTION
#define HETRIGGER_HE0_ACTION GpioAction::NONE
#endif
#ifndef HETRIGGER_HE0_IDLE
#define HETRIGGER_HE0_IDLE HETRIGGER_DEFAULT_IDLE
#endif
#ifndef HETRIGGER_HE0_PRESSED
#define HETRIGGER_HE0_PRESSED HETRIGGER_DEFAULT_PRESSED
#endif
#ifndef HETRIGGER_HE1_ACTION
#define HETRIGGER_HE1_ACTION GpioAction::NONE
#endif
#ifndef HETRIGGER_HE1_IDLE
#define HETRIGGER_HE1_IDLE HETRIGGER_DEFAULT_IDLE
#endif
#ifndef HETRIGGER_HE1_PRESSED
#define HETRIGGER_HE1_PRESSED HETRIGGER_DEFAULT_PRESSED
#endif
#ifndef HETRIGGER_HE2_ACTION
#define HETRIGGER_HE2_ACTION GpioAction::NONE
#endif
#ifndef HETRIGGER_HE2_IDLE
#define HETRIGGER_HE2_IDLE HETRIGGER_DEFAULT_IDLE
#endif
#ifndef HETRIGGER_HE2_PRESSED
#define HETRIGGER_HE2_PRESSED HETRIGGER_DEFAULT_PRESSED
#endif
#ifndef HETRIGGER_HE3_ACTION
#define HETRIGGER_HE3_ACTION GpioAction::NONE
#endif
#ifndef HETRIGGER_HE3_IDLE
#define HETRIGGER_HE3_IDLE HETRIGGER_DEFAULT_IDLE
#endif
#ifndef HETRIGGER_HE3_PRESSED
#define HETRIGGER_HE3_PRESSED HETRIGGER_DEFAULT_PRESSED
#endif
#ifndef HETRIGGER_HE4_ACTION
#define HETRIGGER_HE4_ACTION GpioAction::NONE
#endif
#ifndef HETRIGGER_HE4_IDLE
#define HETRIGGER_HE4_IDLE HETRIGGER_DEFAULT_IDLE
#endif
#ifndef HETRIGGER_HE4_PRESSED
#define HETRIGGER_HE4_PRESSED HETRIGGER_DEFAULT_PRESSED
#endif
#ifndef HETRIGGER_HE5_ACTION
#define HETRIGGER_HE5_ACTION GpioAction::NONE
#endif
#ifndef HETRIGGER_HE5_IDLE
#define HETRIGGER_HE5_IDLE HETRIGGER_DEFAULT_IDLE
#endif
#ifndef HETRIGGER_HE5_PRESSED
#define HETRIGGER_HE5_PRESSED HETRIGGER_DEFAULT_PRESSED
#endif
#ifndef HETRIGGER_HE6_ACTION
#define HETRIGGER_HE6_ACTION GpioAction::NONE
#endif
#ifndef HETRIGGER_HE6_IDLE
#define HETRIGGER_HE6_IDLE HETRIGGER_DEFAULT_IDLE
#endif
#ifndef HETRIGGER_HE6_PRESSED
#define HETRIGGER_HE6_PRESSED HETRIGGER_DEFAULT_PRESSED
#endif
#ifndef HETRIGGER_HE7_ACTION
#define HETRIGGER_HE7_ACTION GpioAction::NONE
#endif
#ifndef HETRIGGER_HE7_IDLE
#define HETRIGGER_HE7_IDLE HETRIGGER_DEFAULT_IDLE
#endif
#ifndef HETRIGGER_HE7_PRESSED
#define HETRIGGER_HE7_PRESSED HETRIGGER_DEFAULT_PRESSED
#endif
#ifndef HETRIGGER_HE8_ACTION
#define HETRIGGER_HE8_ACTION GpioAction::NONE
#endif
#ifndef HETRIGGER_HE8_IDLE
#define HETRIGGER_HE8_IDLE HETRIGGER_DEFAULT_IDLE
#endif
#ifndef HETRIGGER_HE8_PRESSED
#define HETRIGGER_HE8_PRESSED HETRIGGER_DEFAULT_PRESSED
#endif
#ifndef HETRIGGER_HE9_ACTION
#define HETRIGGER_HE9_ACTION GpioAction::NONE
#endif
#ifndef HETRIGGER_HE9_IDLE
#define HETRIGGER_HE9_IDLE HETRIGGER_DEFAULT_IDLE
#endif
#ifndef HETRIGGER_HE9_PRESSED
#define HETRIGGER_HE9_PRESSED HETRIGGER_DEFAULT_PRESSED
#endif
#ifndef HETRIGGER_HE10_ACTION
#define HETRIGGER_HE10_ACTION GpioAction::NONE
#endif
#ifndef HETRIGGER_HE10_IDLE
#define HETRIGGER_HE10_IDLE HETRIGGER_DEFAULT_IDLE
#endif
#ifndef HETRIGGER_HE10_PRESSED
#define HETRIGGER_HE10_PRESSED HETRIGGER_DEFAULT_PRESSED
#endif
#ifndef HETRIGGER_HE11_ACTION
#define HETRIGGER_HE11_ACTION GpioAction::NONE
#endif
#ifndef HETRIGGER_HE11_IDLE
#define HETRIGGER_HE11_IDLE HETRIGGER_DEFAULT_IDLE
#endif
#ifndef HETRIGGER_HE11_PRESSED
#define HETRIGGER_HE11_PRESSED HETRIGGER_DEFAULT_PRESSED
#endif
#ifndef HETRIGGER_HE12_ACTION
#define HETRIGGER_HE12_ACTION GpioAction::NONE
#endif
#ifndef HETRIGGER_HE12_IDLE
#define HETRIGGER_HE12_IDLE HETRIGGER_DEFAULT_IDLE
#endif
#ifndef HETRIGGER_HE12_PRESSED
#define HETRIGGER_HE12_PRESSED HETRIGGER_DEFAULT_PRESSED
#endif
#ifndef HETRIGGER_HE13_ACTION
#define HETRIGGER_HE13_ACTION GpioAction::NONE
#endif
#ifndef HETRIGGER_HE13_IDLE
#define HETRIGGER_HE13_IDLE HETRIGGER_DEFAULT_IDLE
#endif
#ifndef HETRIGGER_HE13_PRESSED
#define HETRIGGER_HE13_PRESSED HETRIGGER_DEFAULT_PRESSED
#endif
#ifndef HETRIGGER_HE14_ACTION
#define HETRIGGER_HE14_ACTION GpioAction::NONE
#endif
#ifndef HETRIGGER_HE14_IDLE
#define HETRIGGER_HE14_IDLE HETRIGGER_DEFAULT_IDLE
#endif
#ifndef HETRIGGER_HE14_PRESSED
#define HETRIGGER_HE14_PRESSED HETRIGGER_DEFAULT_PRESSED
#endif
#ifndef HETRIGGER_HE15_ACTION
#define HETRIGGER_HE15_ACTION GpioAction::NONE
#endif
#ifndef HETRIGGER_HE15_IDLE
#define HETRIGGER_HE15_IDLE HETRIGGER_DEFAULT_IDLE
#endif
#ifndef HETRIGGER_HE15_PRESSED
#define HETRIGGER_HE15_PRESSED HETRIGGER_DEFAULT_PRESSED
#endif
#ifndef HETRIGGER_HE16_ACTION
#define HETRIGGER_HE16_ACTION GpioAction::NONE
#endif
#ifndef HETRIGGER_HE16_IDLE
#define HETRIGGER_HE16_IDLE HETRIGGER_DEFAULT_IDLE
#endif
#ifndef HETRIGGER_HE16_PRESSED
#define HETRIGGER_HE16_PRESSED HETRIGGER_DEFAULT_PRESSED
#endif
#ifndef HETRIGGER_HE17_ACTION
#define HETRIGGER_HE17_ACTION GpioAction::NONE
#endif
#ifndef HETRIGGER_HE17_IDLE
#define HETRIGGER_HE17_IDLE HETRIGGER_DEFAULT_IDLE
#endif
#ifndef HETRIGGER_HE17_PRESSED
#define HETRIGGER_HE17_PRESSED HETRIGGER_DEFAULT_PRESSED
#endif
#ifndef HETRIGGER_HE18_ACTION
#define HETRIGGER_HE18_ACTION GpioAction::NONE
#endif
#ifndef HETRIGGER_HE18_IDLE
#define HETRIGGER_HE18_IDLE HETRIGGER_DEFAULT_IDLE
#endif
#ifndef HETRIGGER_HE18_PRESSED
#define HETRIGGER_HE18_PRESSED HETRIGGER_DEFAULT_PRESSED
#endif
#ifndef HETRIGGER_HE19_ACTION
#define HETRIGGER_HE19_ACTION GpioAction::NONE
#endif
#ifndef HETRIGGER_HE19_IDLE
#define HETRIGGER_HE19_IDLE HETRIGGER_DEFAULT_IDLE
#endif
#ifndef HETRIGGER_HE19_PRESSED
#define HETRIGGER_HE19_PRESSED HETRIGGER_DEFAULT_PRESSED
#endif
#ifndef HETRIGGER_HE20_ACTION
#define HETRIGGER_HE20_ACTION GpioAction::NONE
#endif
#ifndef HETRIGGER_HE20_IDLE
#define HETRIGGER_HE20_IDLE HETRIGGER_DEFAULT_IDLE
#endif
#ifndef HETRIGGER_HE20_PRESSED
#define HETRIGGER_HE20_PRESSED HETRIGGER_DEFAULT_PRESSED
#endif
#ifndef HETRIGGER_HE21_ACTION
#define HETRIGGER_HE21_ACTION GpioAction::NONE
#endif
#ifndef HETRIGGER_HE21_IDLE
#define HETRIGGER_HE21_IDLE HETRIGGER_DEFAULT_IDLE
#endif
#ifndef HETRIGGER_HE21_PRESSED
#define HETRIGGER_HE21_PRESSED HETRIGGER_DEFAULT_PRESSED
#endif
#ifndef HETRIGGER_HE22_ACTION
#define HETRIGGER_HE22_ACTION GpioAction::NONE
#endif
#ifndef HETRIGGER_HE22_IDLE
#define HETRIGGER_HE22_IDLE HETRIGGER_DEFAULT_IDLE
#endif
#ifndef HETRIGGER_HE22_PRESSED
#define HETRIGGER_HE22_PRESSED HETRIGGER_DEFAULT_PRESSED
#endif
#ifndef HETRIGGER_HE23_ACTION
#define HETRIGGER_HE23_ACTION GpioAction::NONE
#endif
#ifndef HETRIGGER_HE23_IDLE
#define HETRIGGER_HE23_IDLE HETRIGGER_DEFAULT_IDLE
#endif
#ifndef HETRIGGER_HE23_PRESSED
#define HETRIGGER_HE23_PRESSED HETRIGGER_DEFAULT_PRESSED
#endif
#ifndef HETRIGGER_HE24_ACTION
#define HETRIGGER_HE24_ACTION GpioAction::NONE
#endif
#ifndef HETRIGGER_HE24_IDLE
#define HETRIGGER_HE24_IDLE HETRIGGER_DEFAULT_IDLE
#endif
#ifndef HETRIGGER_HE24_PRESSED
#define HETRIGGER_HE24_PRESSED HETRIGGER_DEFAULT_PRESSED
#endif
#ifndef HETRIGGER_HE25_ACTION
#define HETRIGGER_HE25_ACTION GpioAction::NONE
#endif
#ifndef HETRIGGER_HE25_IDLE
#define HETRIGGER_HE25_IDLE HETRIGGER_DEFAULT_IDLE
#endif
#ifndef HETRIGGER_HE25_PRESSED
#define HETRIGGER_HE25_PRESSED HETRIGGER_DEFAULT_PRESSED
#endif
#ifndef HETRIGGER_HE26_ACTION
#define HETRIGGER_HE26_ACTION GpioAction::NONE
#endif
#ifndef HETRIGGER_HE26_IDLE
#define HETRIGGER_HE26_IDLE HETRIGGER_DEFAULT_IDLE
#endif
#ifndef HETRIGGER_HE26_PRESSED
#define HETRIGGER_HE26_PRESSED HETRIGGER_DEFAULT_PRESSED
#endif
#ifndef HETRIGGER_HE27_ACTION
#define HETRIGGER_HE27_ACTION GpioAction::NONE
#endif
#ifndef HETRIGGER_HE27_IDLE
#define HETRIGGER_HE27_IDLE HETRIGGER_DEFAULT_IDLE
#endif
#ifndef HETRIGGER_HE27_PRESSED
#define HETRIGGER_HE27_PRESSED HETRIGGER_DEFAULT_PRESSED
#endif
#ifndef HETRIGGER_HE28_ACTION
#define HETRIGGER_HE28_ACTION GpioAction::NONE
#endif
#ifndef HETRIGGER_HE28_IDLE
#define HETRIGGER_HE28_IDLE HETRIGGER_DEFAULT_IDLE
#endif
#ifndef HETRIGGER_HE28_PRESSED
#define HETRIGGER_HE28_PRESSED HETRIGGER_DEFAULT_PRESSED
#endif
#ifndef HETRIGGER_HE29_ACTION
#define HETRIGGER_HE29_ACTION GpioAction::NONE
#endif
#ifndef HETRIGGER_HE29_IDLE
#define HETRIGGER_HE29_IDLE HETRIGGER_DEFAULT_IDLE
#endif
#ifndef HETRIGGER_HE29_PRESSED
#define HETRIGGER_HE29_PRESSED HETRIGGER_DEFAULT_PRESSED
#endif
#ifndef HETRIGGER_HE30_ACTION
#define HETRIGGER_HE30_ACTION GpioAction::NONE
#endif
#ifndef HETRIGGER_HE30_IDLE
#define HETRIGGER_HE30_IDLE HETRIGGER_DEFAULT_IDLE
#endif
#ifndef HETRIGGER_HE30_PRESSED
#define HETRIGGER_HE30_PRESSED HETRIGGER_DEFAULT_PRESSED
#endif
#ifndef HETRIGGER_HE31_ACTION
#define HETRIGGER_HE31_ACTION GpioAction::NONE
#endif
#ifndef HETRIGGER_HE31_IDLE
#define HETRIGGER_HE31_IDLE HETRIGGER_DEFAULT_IDLE
#endif
#ifndef HETRIGGER_HE31_PRESSED
#define HETRIGGER_HE31_PRESSED HETRIGGER_DEFAULT_PRESSED
#endif

// HETrigger Module Name
#define HETriggerAddonName "Hall Effect Trigger"

class HETriggerAddon : public GPAddon {
public:
    virtual bool available();
    virtual void setup();
    virtual void process() {}
    virtual void preprocess();
    virtual void postprocess(bool sent) {}
    virtual void reinit() {}
    virtual std::string name() { return HETriggerAddonName; }
private:
    // One entry per configured channel, built once at setup and ordered so that
    // consecutive samples share an ADC input wherever possible. Everything the
    // sample loop needs is resolved here so that no config lookup, division or
    // floating point happens per frame.
    struct ScanEntry {
        uint8_t  trigger;             // index into the configured trigger list
        uint8_t  adcInput;            // ADC input 0-3
        uint8_t  muxChannel;
        uint8_t  socdPartner;         // 0 is none, otherwise partner index plus one
        int32_t  action;
        int32_t  idle;
        int32_t  scaleQ10;
        HEThresholds thresholds;
    };

    struct ChannelState {
        // The smoothing accumulator is kept in Q8 so that small movements are
        // not lost to truncation, which would otherwise bias the reading.
        uint32_t smoothedQ8;
        uint16_t travel;
        uint16_t peak;                // deepest point since the press began
        uint16_t valley;              // shallowest point since the release began
        bool     active;
        // Set by depth based SOCD to mask an output without disturbing the
        // rapid trigger state, so the channel resumes cleanly once it wins.
        bool     suppressed;
    };

    void selectEntry(const ScanEntry& entry);
    void waitForSettle();
    void resolveProfileOverride();
    void updateChannel(const ScanEntry& entry, uint16_t raw);
    void resolveDepthSOCD();
    void applyActions(Gamepad* gamepad);
    uint16_t shapeAnalog(uint16_t travel);
    void applyAnalog(Gamepad* gamepad, int32_t action, uint16_t travel);

    Pin_t muxPinArray[4];
    Pin_t selectPinArray[4];
    int selectPins;
    int lastADCSelected;

    ScanEntry scan[HETRIGGER_COUNT];
    uint8_t scanCount;
    ChannelState channelState[HETRIGGER_COUNT];

    uint16_t emaFactorQ8;             // 0 disables smoothing
    bool     hasAnalogTrigger;        // any channel drives LT or RT
    uint16_t analogDeadzone;
    int32_t  deadzoneScaleQ10;
    uint8_t  analogCurve;
    bool     analogProportional;
    uint32_t settleMicros;
    uint32_t selectedAt;              // time_us_32 of the last channel select

    // The active profile can override the thresholds for every channel at once.
    // Resolved once per frame rather than once per channel.
    bool         overrideActive;
    HEThresholds overrideThresholds;
    uint16_t     noiseFloor;          // lower bound on the override sensitivities
};

#endif  // _HE_Trigger_H