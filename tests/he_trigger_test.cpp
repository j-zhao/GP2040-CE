#ifdef NDEBUG
#undef NDEBUG
#endif

#include <cassert>

#include "addons/he_trigger_math.h"

static void testTravelConversion() {
    const int32_t positiveScale = heTravelScaleQ10(100, 1100);
    assert(heTravelFromRaw(100, 100, positiveScale) == 0);
    assert(heTravelFromRaw(600, 100, positiveScale) == 500);
    assert(heTravelFromRaw(1100, 100, positiveScale) == 1000);
    assert(heTravelFromRaw(1200, 100, positiveScale) == 1000);

    const int32_t negativeScale = heTravelScaleQ10(1100, 100);
    assert(heTravelFromRaw(1100, 1100, negativeScale) == 0);
    assert(heTravelFromRaw(600, 1100, negativeScale) == 500);
    assert(heTravelFromRaw(100, 1100, negativeScale) == 1000);
    assert(heTravelScaleQ10(100, 115) == 0);
}

static void testConfigurationMath() {
    assert(heValidMuxChannels(1));
    assert(heValidMuxChannels(4));
    assert(heValidMuxChannels(8));
    assert(heValidMuxChannels(16));
    assert(!heValidMuxChannels(0));
    assert(!heValidMuxChannels(2));
    assert(heClampSmoothingFactor(-1) == 1);
    assert(heClampSmoothingFactor(1000) == 99);
    assert(heClampNoiseFloor(1000) == 100);
    assert(heClampMuxSettleMicros(-1) == 0);
    assert(heClampMuxSettleMicros(1000) == 255);
    assert(heClampMuxSettleMicros(INT64_MAX) == 255);

    assert(heNoiseFromRaw(30, 100, 1100) == 30);
    assert(heNoiseFromRaw(30, 1100, 100) == 30);
    assert(heNoiseFromRaw(30, 100, 110) == 0);
    assert(heNoiseFromRaw(-1, 100, 1100) == 0);
    assert(heClampRaw(-1) == 0);
    assert(heClampRaw(5000) == 4095);

    assert(hePressedFromSweep(1000, 200, 1200) == 200);
    assert(hePressedFromSweep(1000, 800, 1800) == 1800);
}

static void testThresholdResolution() {
    const HEThresholds thresholds = heResolveThresholds(450, 600, 10, 0, 1, 30);
    assert(thresholds.actuation == 450);
    assert(thresholds.deactuation == 450);
    assert(thresholds.pressSensitivity == 30);
    assert(thresholds.releaseSensitivity == 30);
    assert(thresholds.releaseFloor == 450);

    const HEThresholds continuous = heResolveThresholds(450, 300, 40, 50, 2, 20);
    assert(continuous.releaseFloor == 20);
}

static void testFixedTrigger() {
    const HEThresholds thresholds = heResolveThresholds(450, 300, 30, 30, 0, 10);
    bool active = false;
    uint16_t peak = 0;
    uint16_t valley = 0;

    heUpdateDigitalState(active, peak, valley, 449, thresholds);
    assert(!active);
    heUpdateDigitalState(active, peak, valley, 450, thresholds);
    assert(active);
    heUpdateDigitalState(active, peak, valley, 300, thresholds);
    assert(active);
    heUpdateDigitalState(active, peak, valley, 299, thresholds);
    assert(!active);
}

static void testRapidTrigger() {
    const HEThresholds thresholds = heResolveThresholds(400, 200, 30, 40, 1, 10);
    bool active = false;
    uint16_t peak = 0;
    uint16_t valley = 0;

    heUpdateDigitalState(active, peak, valley, 399, thresholds);
    assert(!active);
    heUpdateDigitalState(active, peak, valley, 430, thresholds);
    assert(active);
    heUpdateDigitalState(active, peak, valley, 700, thresholds);
    assert(active);
    heUpdateDigitalState(active, peak, valley, 660, thresholds);
    assert(!active);
    heUpdateDigitalState(active, peak, valley, 689, thresholds);
    assert(!active);
    heUpdateDigitalState(active, peak, valley, 690, thresholds);
    assert(active);
}

static void testContinuousRapidTrigger() {
    const HEThresholds thresholds = heResolveThresholds(400, 200, 30, 30, 2, 10);
    bool active = false;
    uint16_t peak = 0;
    uint16_t valley = 100;

    heUpdateDigitalState(active, peak, valley, 130, thresholds);
    assert(active);
    heUpdateDigitalState(active, peak, valley, 100, thresholds);
    assert(!active);
}

int main() {
    testTravelConversion();
    testConfigurationMath();
    testThresholdResolution();
    testFixedTrigger();
    testRapidTrigger();
    testContinuousRapidTrigger();
    return 0;
}
