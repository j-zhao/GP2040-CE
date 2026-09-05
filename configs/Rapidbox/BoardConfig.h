/*
 * SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: Copyright (c) 2024 OpenStickCommunity (gp2040-ce.info)
 */

#ifndef PICO_BOARD_CONFIG_H_
#define PICO_BOARD_CONFIG_H_

#include "enums.pb.h"
#include "class/hid/hid.h"

#define BOARD_CONFIG_LABEL "Rapidbox"

// The 15 gameplay buttons are magnetic (hall effect) switches. A single
// 16-channel multiplexer reads them through one ADC pin. Only the three
// utility buttons are wired to GPIO.
//                                                     // GP2040 | Xinput | Switch  | PS3/4/5  | Dinput | Arcade |
#define GPIO_PIN_00 GpioAction::BUTTON_PRESS_S2        // S2     | Start  | Plus    | Options  | 10     | Start  |
#define GPIO_PIN_01 GpioAction::BUTTON_PRESS_S1        // S1     | Back   | Minus   | Share    | 9      | Coin   |
#define GPIO_PIN_02 GpioAction::BUTTON_PRESS_A2        // A2     | ~      | Capture | Touchpad | 14     | ~      |

// Setting GPIO pins to assigned by add-on
//
#define GPIO_PIN_10 GpioAction::ASSIGNED_TO_ADDON // I2C
#define GPIO_PIN_11 GpioAction::ASSIGNED_TO_ADDON // I2C
#define GPIO_PIN_13 GpioAction::ASSIGNED_TO_ADDON // Multiplexer select 0
#define GPIO_PIN_14 GpioAction::ASSIGNED_TO_ADDON // Multiplexer select 1
#define GPIO_PIN_15 GpioAction::ASSIGNED_TO_ADDON // Multiplexer select 2
#define GPIO_PIN_26 GpioAction::ASSIGNED_TO_ADDON // Multiplexer select 3
#define GPIO_PIN_27 GpioAction::ASSIGNED_TO_ADDON // USB
#define GPIO_PIN_29 GpioAction::ASSIGNED_TO_ADDON // Multiplexer ADC

// Enable Hall multiplexer addon
#define HETRIGGER_ENABLED 1
#define HETRIGGER_S0_PIN 13
#define HETRIGGER_S1_PIN 14
#define HETRIGGER_S2_PIN 15
#define HETRIGGER_S3_PIN 26
#define HETRIGGER_ADC0 29

// One Multiplexer with 16-channels
#define HETRIGGER_MUX_CHANNELS 16

// The magnetic switches settle fast enough that smoothing is not needed
#define HETRIGGER_SMOOTHING_ENABLED 0

// Multiplexer 1 mappings (Channels 1-16)
#define HETRIGGER_HE0_ACTION GpioAction::BUTTON_PRESS_UP
#define HETRIGGER_HE1_ACTION GpioAction::BUTTON_PRESS_R3
#define HETRIGGER_HE2_ACTION GpioAction::BUTTON_PRESS_FN
#define HETRIGGER_HE3_ACTION GpioAction::BUTTON_PRESS_L2
#define HETRIGGER_HE4_ACTION GpioAction::BUTTON_PRESS_R2
#define HETRIGGER_HE5_ACTION GpioAction::BUTTON_PRESS_B2
#define HETRIGGER_HE6_ACTION GpioAction::BUTTON_PRESS_B1
#define HETRIGGER_HE7_ACTION GpioAction::BUTTON_PRESS_L1
#define HETRIGGER_HE8_ACTION GpioAction::BUTTON_PRESS_R1
#define HETRIGGER_HE9_ACTION GpioAction::BUTTON_PRESS_B4
#define HETRIGGER_HE10_ACTION GpioAction::BUTTON_PRESS_B3
#define HETRIGGER_HE11_ACTION GpioAction::BUTTON_PRESS_RIGHT
#define HETRIGGER_HE12_ACTION GpioAction::BUTTON_PRESS_DOWN
#define HETRIGGER_HE13_ACTION GpioAction::BUTTON_PRESS_LEFT
#define HETRIGGER_HE14_ACTION GpioAction::BUTTON_PRESS_L3
// Channel 16 is unpopulated

// Threshold for Trigger Active, as travel in tenths of a percent. This is the
// 2000-count threshold this board shipped with, over the default 150-3500 span.
#define HETRIGGER_DEFAULT_ACTUATION 552

// Hall effect switches do not bounce
#define DEFAULT_DEBOUNCE_DELAY 1

// Keyboard Mapping Configuration
//                                            // GP2040 | Xinput | Switch  | PS3/4/5  | Dinput | Arcade |
#define KEY_DPAD_UP     HID_KEY_ARROW_UP      // UP     | UP     | UP      | UP       | UP     | UP     |
#define KEY_DPAD_DOWN   HID_KEY_ARROW_DOWN    // DOWN   | DOWN   | DOWN    | DOWN     | DOWN   | DOWN   |
#define KEY_DPAD_RIGHT  HID_KEY_ARROW_RIGHT   // RIGHT  | RIGHT  | RIGHT   | RIGHT    | RIGHT  | RIGHT  |
#define KEY_DPAD_LEFT   HID_KEY_ARROW_LEFT    // LEFT   | LEFT   | LEFT    | LEFT     | LEFT   | LEFT   |
#define KEY_BUTTON_B1   HID_KEY_SHIFT_LEFT    // B1     | A      | B       | Cross    | 2      | K1     |
#define KEY_BUTTON_B2   HID_KEY_Z             // B2     | B      | A       | Circle   | 3      | K2     |
#define KEY_BUTTON_R2   HID_KEY_X             // R2     | RT     | ZR      | R2       | 8      | K3     |
#define KEY_BUTTON_L2   HID_KEY_V             // L2     | LT     | ZL      | L2       | 7      | K4     |
#define KEY_BUTTON_B3   HID_KEY_CONTROL_LEFT  // B3     | X      | Y       | Square   | 1      | P1     |
#define KEY_BUTTON_B4   HID_KEY_ALT_LEFT      // B4     | Y      | X       | Triangle | 4      | P2     |
#define KEY_BUTTON_R1   HID_KEY_SPACE         // R1     | RB     | R       | R1       | 6      | P3     |
#define KEY_BUTTON_L1   HID_KEY_C             // L1     | LB     | L       | L1       | 5      | P4     |
#define KEY_BUTTON_S1   HID_KEY_5             // S1     | Back   | Minus   | Select   | 9      | Coin   |
#define KEY_BUTTON_S2   HID_KEY_1             // S2     | Start  | Plus    | Start    | 10     | Start  |
#define KEY_BUTTON_L3   HID_KEY_EQUAL         // L3     | LS     | LS      | L3       | 11     | LS     |
#define KEY_BUTTON_R3   HID_KEY_MINUS         // R3     | RS     | RS      | R3       | 12     | RS     |
#define KEY_BUTTON_A1   HID_KEY_9             // A1     | Guide  | Home    | PS       | 13     | ~      |
#define KEY_BUTTON_A2   HID_KEY_F2            // A2     | ~      | Capture | ~        | 14     | ~      |
#define KEY_BUTTON_FN   -1                    // Hotkey Function                                        |

#define HAS_I2C_DISPLAY 1
#define DISPLAY_I2C_BLOCK i2c1
#define I2C1_ENABLED 1
#define I2C1_PIN_SDA 10
#define I2C1_PIN_SCL 11
#define DISPLAY_FLIP 1

#define BUTTON_LAYOUT BUTTON_LAYOUT_STICKLESS_14
#define BUTTON_LAYOUT_RIGHT BUTTON_LAYOUT_STICKLESS_14B

// Status bar: input mode, d-pad mode, SOCD mode and the profile number are
// worth the space on this board; turbo and macros are not used.
#define DISPLAY_INPUT_MODE 1
#define DISPLAY_TURBO_MODE 0
#define DISPLAY_DPAD_MODE 1
#define DISPLAY_SOCD_MODE 1
#define DISPLAY_MACRO_MODE 0
#define DISPLAY_PROFILE_MODE 1

// Input history takes the bottom row, which shrinks the button layout to the
// band above it.
#define INPUT_HISTORY_ENABLED 1
#define INPUT_HISTORY_LENGTH 21
#define INPUT_HISTORY_COL 0
#define INPUT_HISTORY_ROW 7

#define USB_PERIPHERAL_ENABLED 1
#define USB_PERIPHERAL_PIN_DPLUS 27
#define USB_PERIPHERAL_PIN_ORDER 0

#define DEFAULT_PS4AUTHENTICATION_TYPE INPUT_MODE_AUTH_TYPE_USB
#define DEFAULT_PS5AUTHENTICATION_TYPE INPUT_MODE_AUTH_TYPE_USB

#endif
