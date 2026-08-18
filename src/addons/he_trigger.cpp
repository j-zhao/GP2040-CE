#include "addons/he_trigger.h"
#include "storagemanager.h"

#include "hardware/adc.h"
#include "pico/stdlib.h"
#include "pico/time.h"

#define ADC_MAX ((1 << 12) - 1) // 4095

// Actions with no mapping are stored as -10 by the web configurator.
#define HETRIGGER_ACTION_NONE -10


bool HETriggerAddon::available() {
    return Storage::getInstance().getAddonOptions().heTriggerOptions.enabled;
}

void HETriggerAddon::setup() {
    HETriggerOptions & options = Storage::getInstance().getAddonOptions().heTriggerOptions;

    scanCount = 0;
    hasAnalogTrigger = false;
    // Channels outside the scan list are never sampled, but a SOCD partner can
    // still point at one, so every slot needs a defined state.
    for(uint8_t he = 0; he < HETRIGGER_COUNT; he++) {
        channelState[he] = ChannelState{0, 0, 0, 0, false, false};
    }

    if ( options.muxChannels < 1 )
        return;

    // Direct = 4, 4-Channel = 4, 8-Channel = 3, 16-Channel = 2
    int muxTotal = 32 / options.muxChannels;
    if ( muxTotal > 4 )
        muxTotal = 4;

    // Init the ADC options
    muxPinArray[0] = options.muxADCPin0;
    muxPinArray[1] = options.muxADCPin1;
    muxPinArray[2] = options.muxADCPin2;
    muxPinArray[3] = options.muxADCPin3;
    for(int i = 0; i < muxTotal; i++) {
        if ( muxPinArray[i] >= 26 && muxPinArray[i] <= 29 ) {
            adc_gpio_init(muxPinArray[i]);
        }
    }

    // Init our select pins
    switch(options.muxChannels) {
        case 4:
            this->selectPins = 2;
            break;
        case 8:
            this->selectPins = 3;
            break;
        case 16:
            this->selectPins = 4;
            break;
        case 1:
        default:
            this->selectPins = 0;
            break;
    }

    selectPinArray[0] = options.selectPin0;
    selectPinArray[1] = options.selectPin1;
    selectPinArray[2] = options.selectPin2;
    selectPinArray[3] = options.selectPin3;
    for(int i = 0; i < selectPins; i++) {
        if ( selectPinArray[i] != -1 ) {
            gpio_init(selectPinArray[i]);
            gpio_set_dir(selectPinArray[i], GPIO_OUT);
            gpio_put(selectPinArray[i], 0);
        }
    }

    lastADCSelected = -1;
    selectedAt = 0;

    // Board-wide shaping, resolved once so the sample loop never reads config
    emaFactorQ8 = options.emaSmoothing ? ((uint16_t)options.smoothingFactor * 256) / 100 : 0;
    noiseFloor = options.noiseFloor;
    overrideActive = false;
    analogDeadzone = options.analogDeadzone;
    analogCurve = options.analogCurve;
    analogProportional = options.analogProportional;
    settleMicros = options.muxSettleMicros;
    deadzoneScaleQ10 = (analogDeadzone < HETRIGGER_TRAVEL_MAX)
        ? (HETRIGGER_TRAVEL_MAX << 10) / (HETRIGGER_TRAVEL_MAX - analogDeadzone)
        : 0;

    // Build the scan list, grouped by ADC input so that consecutive samples
    // rarely need to switch the ADC. Channels with no action are left out
    // entirely rather than being skipped every frame.
    for(int input = 0; input < 4; input++) {
        for(uint8_t he = 0; he < HETRIGGER_COUNT; he++) {
            HETriggerInfo & trigger = options.triggers[he];
            if (trigger.action == HETRIGGER_ACTION_NONE)
                continue;

            const int mux = he / options.muxChannels;
            if (mux >= muxTotal)
                continue;
            if (muxPinArray[mux] < 26 || muxPinArray[mux] > 29)
                continue;
            if ((muxPinArray[mux] - 26) != input)
                continue;

            // A channel that was never calibrated still has to work rather than
            // silently disappear, so an uncalibrated channel with no valid span
            // falls back to the full ADC range. A channel that went through the
            // calibration sweep but was never pressed has a real idle reading and
            // no reason to actuate, so it is left out of the scan list entirely.
            int32_t scaleQ10 = heTravelScaleQ10(trigger.idle, trigger.pressed);
            int32_t idle = trigger.idle;
            if (scaleQ10 == 0) {
                if (trigger.calibrated)
                    continue;
                idle = 0;
                scaleQ10 = heTravelScaleQ10(0, ADC_MAX);
            }

            ScanEntry & entry = scan[scanCount++];
            entry.trigger = he;
            entry.adcInput = (uint8_t)input;
            entry.muxChannel = (uint8_t)(he % options.muxChannels);
            entry.socdPartner = (uint8_t)trigger.socdPartner;
            entry.action = trigger.action;
            entry.idle = idle;
            entry.scaleQ10 = scaleQ10;
            entry.thresholds = heResolveThresholds(trigger.actuationPoint, trigger.deactuationPoint,
                                                   trigger.rtPressSensitivity, trigger.rtReleaseSensitivity,
                                                   (uint8_t)trigger.rtMode, noiseFloor);

            if (entry.action == GpioAction::ANALOG_TRIGGER_LT || entry.action == GpioAction::ANALOG_TRIGGER_RT)
                hasAnalogTrigger = true;
        }
    }

    // Seed every channel from a real reading so the first frame does not report
    // a spurious press. This runs for all channels, not just smoothed ones.
    for(uint8_t i = 0; i < scanCount; i++) {
        const ScanEntry & entry = scan[i];
        selectEntry(entry);
        waitForSettle();
        const uint16_t raw = adc_read();
        ChannelState & state = channelState[entry.trigger];
        state.smoothedQ8 = (uint32_t)raw << 8;
        state.travel = heTravelFromRaw(raw, entry.idle, entry.scaleQ10);
        state.peak = state.travel;
        state.valley = state.travel;
        state.active = false;
    }
}

void HETriggerAddon::selectEntry(const ScanEntry& entry) {
    for(int i = 0; i < selectPins; i++) {
        if ( selectPinArray[i] != -1 ) {
            gpio_put(selectPinArray[i], (entry.muxChannel >> i) & 0x01);
        }
    }
    if ( lastADCSelected != entry.adcInput ) {
        adc_select_input(entry.adcInput);
        lastADCSelected = entry.adcInput;
    }
    selectedAt = time_us_32();
}

// The mux settles while the previous channel is being processed, so this
// normally returns without waiting at all. It only burns cycles when the
// configured settle time is longer than the work that happened in between.
void HETriggerAddon::waitForSettle() {
    while ((time_us_32() - selectedAt) < settleMicros) {
        tight_loop_contents();
    }
}

// The active profile may override the thresholds for every channel. Read it once
// per frame so that a profile switch takes effect without a reboot, and so that
// the sample loop never touches storage.
void HETriggerAddon::resolveProfileOverride() {
    const ProfileSettings* profileSettings = Storage::getInstance().getCurrentProfileSettings();
    if ( profileSettings == nullptr || !profileSettings->heSettings.enabled ) {
        overrideActive = false;
        return;
    }

    const HEProfileSettings & he = profileSettings->heSettings;
    overrideActive = true;
    overrideThresholds = heResolveThresholds(he.actuationPoint, he.deactuationPoint,
                                             he.rtPressSensitivity, he.rtReleaseSensitivity,
                                             (uint8_t)he.rtMode, noiseFloor);
}

void HETriggerAddon::updateChannel(const ScanEntry& entry, uint16_t raw) {
    ChannelState & state = channelState[entry.trigger];
    state.suppressed = false;

    const HEThresholds & th = overrideActive ? overrideThresholds : entry.thresholds;

    // Fixed point EMA. The float form this replaces called into the soft float
    // routines four times per channel per frame. The accumulator stays in Q8 so
    // that movements smaller than one whole count are carried rather than
    // truncated away, which would drag the reading toward one end of travel.
    if ( emaFactorQ8 ) {
        const int32_t delta = ((int32_t)raw << 8) - (int32_t)state.smoothedQ8;
        state.smoothedQ8 = (uint32_t)((int32_t)state.smoothedQ8 + ((delta * emaFactorQ8) >> 8));
    } else {
        state.smoothedQ8 = (uint32_t)raw << 8;
    }

    const uint16_t travel = heTravelFromRaw((int32_t)(state.smoothedQ8 >> 8), entry.idle, entry.scaleQ10);
    state.travel = travel;

    if ( th.rtMode == HERapidTriggerMode::HE_RT_OFF ) {
        // Releasing on the threshold itself rather than below it would make a
        // key resting exactly on the actuation point alternate every frame.
        state.active = state.active ? (travel >= th.deactuation) : (travel >= th.actuation);
        return;
    }

    // Rapid trigger works off direction of travel rather than a fixed point:
    // press once the key moves down far enough from its shallowest point, and
    // release once it moves up far enough from its deepest point.
    if ( state.active ) {
        if ( travel > state.peak )
            state.peak = travel;

        // Either moving up far enough from the deepest point, or coming back
        // past the release floor, releases the key. The floor is what stops a
        // release sensitivity larger than the depth reached from latching it.
        if ( (travel + th.releaseSensitivity <= state.peak) || (travel < th.releaseFloor) ) {
            state.active = false;
            state.valley = travel;
        }
    } else {
        if ( travel < state.valley )
            state.valley = travel;

        const bool armed = (th.rtMode == HERapidTriggerMode::HE_RT_CONTINUOUS) || (travel >= th.actuation);
        if ( armed && (travel >= state.valley + th.pressSensitivity) ) {
            state.active = true;
            state.peak = travel;
        }
    }
}

// Depth based SOCD: of two opposing channels, the one pressed deeper wins.
// Pressing both to the same depth leaves both active.
//
// This masks the output rather than clearing the state machine. Clearing it
// would leave the losing channel with a stale peak and valley, so it would
// misbehave for a stroke or two after it wins again.
void HETriggerAddon::resolveDepthSOCD() {
    for(uint8_t i = 0; i < scanCount; i++) {
        const ScanEntry & entry = scan[i];
        if ( entry.socdPartner == 0 )
            continue;

        const uint8_t partner = entry.socdPartner - 1;
        if ( partner >= HETRIGGER_COUNT || partner == entry.trigger )
            continue;

        // Suppressing the shallower channel is idempotent, so it does not
        // matter whether only one side or both sides of a pair name the other.
        ChannelState & self = channelState[entry.trigger];
        ChannelState & other = channelState[partner];
        if ( !self.active || !other.active )
            continue;

        if ( self.travel > other.travel ) {
            other.suppressed = true;
        } else if ( other.travel > self.travel ) {
            self.suppressed = true;
        }
    }
}

uint16_t HETriggerAddon::shapeAnalog(uint16_t travel) {
    if ( travel <= analogDeadzone )
        return 0;

    int32_t value = ((int32_t)(travel - analogDeadzone) * deadzoneScaleQ10) >> 10;
    if ( value > HETRIGGER_TRAVEL_MAX )
        value = HETRIGGER_TRAVEL_MAX;

    switch (analogCurve) {
        case HEAnalogCurve::HE_CURVE_EXPONENTIAL:
            // value squared, in the same 0-1000 scale
            value = (value * value * 1049) >> 20;
            break;
        case HEAnalogCurve::HE_CURVE_S: {
            // smoothstep: v * v * (3 - 2v)
            const int32_t squared = (value * value * 1049) >> 20;
            value = (squared * (3 * HETRIGGER_TRAVEL_MAX - 2 * value) * 1049) >> 20;
            break;
        }
        case HEAnalogCurve::HE_CURVE_LINEAR:
        default:
            break;
    }

    if ( value > HETRIGGER_TRAVEL_MAX )
        value = HETRIGGER_TRAVEL_MAX;
    return (uint16_t)value;
}

void HETriggerAddon::applyAnalog(Gamepad* gamepad, int32_t action, uint16_t travel) {
    const uint16_t shaped = shapeAnalog(travel);
    // A channel at rest contributes nothing. Without this, an idle channel
    // would write the centre value back over a deflection that the opposing
    // channel on the same axis had already applied.
    if ( shaped == 0 )
        return;

    // 0-1000 to 0-255 and 0-1000 to a half stick range, both by multiply and
    // shift so the hot path stays free of division.
    const uint8_t trigger = (uint8_t)(((shaped * 261) + 512) >> 10);
    const int32_t deviation = ((shaped * 33553) + 512) >> 10;

    // Resolve which axis this action drives and in which direction, then apply
    // the "furthest deflection wins" rule once rather than restating it per case.
    uint16_t * axis = nullptr;
    int32_t sign = 1;
    switch (action) {
        case GpioAction::ANALOG_TRIGGER_LT:
            if ( trigger > gamepad->state.lt ) gamepad->state.lt = trigger;
            return;
        case GpioAction::ANALOG_TRIGGER_RT:
            if ( trigger > gamepad->state.rt ) gamepad->state.rt = trigger;
            return;
        case GpioAction::ANALOG_DIRECTION_LS_X_NEG: axis = &gamepad->state.lx; sign = -1; break;
        case GpioAction::ANALOG_DIRECTION_LS_X_POS: axis = &gamepad->state.lx; break;
        case GpioAction::ANALOG_DIRECTION_LS_Y_NEG: axis = &gamepad->state.ly; sign = -1; break;
        case GpioAction::ANALOG_DIRECTION_LS_Y_POS: axis = &gamepad->state.ly; break;
        case GpioAction::ANALOG_DIRECTION_RS_X_NEG: axis = &gamepad->state.rx; sign = -1; break;
        case GpioAction::ANALOG_DIRECTION_RS_X_POS: axis = &gamepad->state.rx; break;
        case GpioAction::ANALOG_DIRECTION_RS_Y_NEG: axis = &gamepad->state.ry; sign = -1; break;
        case GpioAction::ANALOG_DIRECTION_RS_Y_POS: axis = &gamepad->state.ry; break;
        default: return;
    }

    // Compare how far each channel pushes the axis from centre, not the raw
    // axis value. A negative direction always reads below a positive one, so a
    // raw comparison would let the shallower of an opposing pair win.
    const int32_t current = (int32_t)*axis - GAMEPAD_JOYSTICK_MID;
    const int32_t currentDeflection = (current < 0) ? -current : current;
    if ( deviation > currentDeflection )
        *axis = (uint16_t)(GAMEPAD_JOYSTICK_MID + (sign * deviation));
}

void HETriggerAddon::applyActions(Gamepad* gamepad) {
    for (uint8_t i = 0; i < scanCount; i++) {
        const ScanEntry & entry = scan[i];
        const ChannelState & state = channelState[entry.trigger];

        if (state.suppressed)
            continue;

        // The analog trigger actions are always proportional. The stick
        // direction actions only become proportional when the board opts in,
        // so existing setups keep snapping to the extremes.
        const bool isAnalogTrigger = (entry.action == GpioAction::ANALOG_TRIGGER_LT) ||
                                     (entry.action == GpioAction::ANALOG_TRIGGER_RT);
        const bool isAnalogStick = (entry.action >= GpioAction::ANALOG_DIRECTION_LS_X_NEG) &&
                                   (entry.action <= GpioAction::ANALOG_DIRECTION_RS_Y_POS);
        if ( isAnalogTrigger || (isAnalogStick && analogProportional) ) {
            applyAnalog(gamepad, entry.action, state.travel);
            continue;
        }

        if (!state.active)
            continue;

        switch (entry.action) {
            case GpioAction::BUTTON_PRESS_UP: gamepad->state.dpad |= GAMEPAD_MASK_UP; break;
            case GpioAction::BUTTON_PRESS_DOWN: gamepad->state.dpad |= GAMEPAD_MASK_DOWN; break;
            case GpioAction::BUTTON_PRESS_LEFT: gamepad->state.dpad |= GAMEPAD_MASK_LEFT; break;
            case GpioAction::BUTTON_PRESS_RIGHT: gamepad->state.dpad |= GAMEPAD_MASK_RIGHT; break;
            case GpioAction::BUTTON_PRESS_B1: gamepad->state.buttons |= GAMEPAD_MASK_B1; break;
            case GpioAction::BUTTON_PRESS_B2: gamepad->state.buttons |= GAMEPAD_MASK_B2; break;
            case GpioAction::BUTTON_PRESS_B3: gamepad->state.buttons |= GAMEPAD_MASK_B3; break;
            case GpioAction::BUTTON_PRESS_B4: gamepad->state.buttons |= GAMEPAD_MASK_B4; break;
            case GpioAction::BUTTON_PRESS_L1: gamepad->state.buttons |= GAMEPAD_MASK_L1; break;
            case GpioAction::BUTTON_PRESS_R1: gamepad->state.buttons |= GAMEPAD_MASK_R1; break;
            case GpioAction::BUTTON_PRESS_L2: gamepad->state.buttons |= GAMEPAD_MASK_L2; break;
            case GpioAction::BUTTON_PRESS_R2: gamepad->state.buttons |= GAMEPAD_MASK_R2; break;
            case GpioAction::BUTTON_PRESS_S1: gamepad->state.buttons |= GAMEPAD_MASK_S1; break;
            case GpioAction::BUTTON_PRESS_S2: gamepad->state.buttons |= GAMEPAD_MASK_S2; break;
            case GpioAction::BUTTON_PRESS_L3: gamepad->state.buttons |= GAMEPAD_MASK_L3; break;
            case GpioAction::BUTTON_PRESS_R3: gamepad->state.buttons |= GAMEPAD_MASK_R3; break;
            case GpioAction::BUTTON_PRESS_A1: gamepad->state.buttons |= GAMEPAD_MASK_A1; break;
            case GpioAction::BUTTON_PRESS_A2: gamepad->state.buttons |= GAMEPAD_MASK_A2; break;
            case GpioAction::BUTTON_PRESS_A3: gamepad->state.buttons |= GAMEPAD_MASK_A3; break;
            case GpioAction::BUTTON_PRESS_A4: gamepad->state.buttons |= GAMEPAD_MASK_A4; break;
            case GpioAction::BUTTON_PRESS_E1: gamepad->state.buttons |= GAMEPAD_MASK_E1; break;
            case GpioAction::BUTTON_PRESS_E2: gamepad->state.buttons |= GAMEPAD_MASK_E2; break;
            case GpioAction::BUTTON_PRESS_E3: gamepad->state.buttons |= GAMEPAD_MASK_E3; break;
            case GpioAction::BUTTON_PRESS_E4: gamepad->state.buttons |= GAMEPAD_MASK_E4; break;
            case GpioAction::BUTTON_PRESS_E5: gamepad->state.buttons |= GAMEPAD_MASK_E5; break;
            case GpioAction::BUTTON_PRESS_E6: gamepad->state.buttons |= GAMEPAD_MASK_E6; break;
            case GpioAction::BUTTON_PRESS_E7: gamepad->state.buttons |= GAMEPAD_MASK_E7; break;
            case GpioAction::BUTTON_PRESS_E8: gamepad->state.buttons |= GAMEPAD_MASK_E8; break;
            case GpioAction::BUTTON_PRESS_E9: gamepad->state.buttons |= GAMEPAD_MASK_E9; break;
            case GpioAction::BUTTON_PRESS_E10: gamepad->state.buttons |= GAMEPAD_MASK_E10; break;
            case GpioAction::BUTTON_PRESS_E11: gamepad->state.buttons |= GAMEPAD_MASK_E11; break;
            case GpioAction::BUTTON_PRESS_E12: gamepad->state.buttons |= GAMEPAD_MASK_E12; break;
            case GpioAction::ANALOG_DIRECTION_LS_X_NEG:	gamepad->state.lx = GAMEPAD_JOYSTICK_MIN; break;
		    case GpioAction::ANALOG_DIRECTION_LS_X_POS:	gamepad->state.lx = GAMEPAD_JOYSTICK_MAX; break;
		    case GpioAction::ANALOG_DIRECTION_LS_Y_NEG:	gamepad->state.ly = GAMEPAD_JOYSTICK_MIN; break;
		    case GpioAction::ANALOG_DIRECTION_LS_Y_POS:	gamepad->state.ly = GAMEPAD_JOYSTICK_MAX; break;
		    case GpioAction::ANALOG_DIRECTION_RS_X_NEG:	gamepad->state.rx = GAMEPAD_JOYSTICK_MIN; break;
		    case GpioAction::ANALOG_DIRECTION_RS_X_POS:	gamepad->state.rx = GAMEPAD_JOYSTICK_MAX; break;
            case GpioAction::ANALOG_DIRECTION_RS_Y_NEG:	gamepad->state.ry = GAMEPAD_JOYSTICK_MIN; break;
            case GpioAction::ANALOG_DIRECTION_RS_Y_POS:	gamepad->state.ry = GAMEPAD_JOYSTICK_MAX; break;
            case GpioAction::BUTTON_PRESS_FN:	gamepad->state.aux |= AUX_MASK_FUNCTION; break;
            case GpioAction::MENU_NAVIGATION_UP: EventManager::getInstance().triggerEvent(new GPMenuNavigateEvent(GpioAction::MENU_NAVIGATION_UP)); break;
            case GpioAction::MENU_NAVIGATION_DOWN: EventManager::getInstance().triggerEvent(new GPMenuNavigateEvent(GpioAction::MENU_NAVIGATION_DOWN)); break;
            case GpioAction::MENU_NAVIGATION_LEFT: EventManager::getInstance().triggerEvent(new GPMenuNavigateEvent(GpioAction::MENU_NAVIGATION_LEFT)); break;
            case GpioAction::MENU_NAVIGATION_RIGHT: EventManager::getInstance().triggerEvent(new GPMenuNavigateEvent(GpioAction::MENU_NAVIGATION_RIGHT)); break;
            case GpioAction::MENU_NAVIGATION_SELECT: EventManager::getInstance().triggerEvent(new GPMenuNavigateEvent(GpioAction::MENU_NAVIGATION_SELECT)); break;
            case GpioAction::MENU_NAVIGATION_BACK: EventManager::getInstance().triggerEvent(new GPMenuNavigateEvent(GpioAction::MENU_NAVIGATION_BACK)); break;
            case GpioAction::MENU_NAVIGATION_TOGGLE: EventManager::getInstance().triggerEvent(new GPMenuNavigateEvent(GpioAction::MENU_NAVIGATION_TOGGLE)); break;
            default: break;
        }
    }
}

void HETriggerAddon::preprocess() {
    if ( scanCount == 0 )
        return;

    Gamepad * gamepad = Storage::getInstance().GetGamepad();
    if ( hasAnalogTrigger )
        gamepad->hasAnalogTriggers = true;

    resolveProfileOverride();

    selectEntry(scan[0]);
    for (uint8_t i = 0; i < scanCount; i++) {
        waitForSettle();
        const uint16_t raw = adc_read();

        // Point the mux at the next channel before doing this channel's maths,
        // so that it settles during work that had to happen anyway.
        if ( (i + 1) < scanCount )
            selectEntry(scan[i + 1]);

        updateChannel(scan[i], raw);
    }

    resolveDepthSOCD();
    applyActions(gamepad);
}
