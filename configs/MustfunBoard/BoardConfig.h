/*
 * SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: Copyright (c) 2024 OpenStickCommunity (gp2040-ce.info)
 */

#ifndef PICO_BOARD_CONFIG_H_
#define PICO_BOARD_CONFIG_H_

#include "enums.pb.h"
#include "class/hid/hid.h"

#define BOARD_CONFIG_LABEL "MustfunBoard"

// Main pin mapping Configuration
//                                                 // GP2040 | Xinput | Switch  | PS3/4/5  | Dinput | Arcade |
#define GPIO_PIN_02 GpioAction::BUTTON_PRESS_A2      // A2     |        |         | Touchpad |        |        |
#define GPIO_PIN_03 GpioAction::BUTTON_PRESS_B3      // B3     | X      | Y       | Square   | 1      | P1     |
#define GPIO_PIN_04 GpioAction::BUTTON_PRESS_B4      // B4     | Y      | X       | Triangle | 4      | P2     |
#define GPIO_PIN_05 GpioAction::BUTTON_PRESS_R1      // R1     | RB     | R       | R1       | 6      | P3     |
#define GPIO_PIN_06 GpioAction::BUTTON_PRESS_L1      // L1     | LB     | L       | L1       | 5      | P4     |
#define GPIO_PIN_07 GpioAction::BUTTON_PRESS_B1      // B1     | A      | B       | Cross    | 2      | K1     |
#define GPIO_PIN_08 GpioAction::BUTTON_PRESS_B2      // B2     | B      | A       | Circle   | 3      | K2     |
#define GPIO_PIN_09 GpioAction::BUTTON_PRESS_R2      // R2     | RT     | ZR      | R2       | 8      | K3     |
#define GPIO_PIN_10 GpioAction::BUTTON_PRESS_L2      // L2     | LT     | ZL      | L2       | 7      | K4     |
#define GPIO_PIN_11 GpioAction::BUTTON_PRESS_R3      // R3     | RS     | RS      | R3       | 12     | RS     |
#define GPIO_PIN_12 GpioAction::BUTTON_PRESS_UP      // UP     | UP     | UP      | UP       | UP     | UP     |
#define GPIO_PIN_13 GpioAction::BUTTON_PRESS_L3      // L3     | LS     | LS      | L3       | 11     | LS     |
#define GPIO_PIN_14 GpioAction::BUTTON_PRESS_RIGHT   // RIGHT  | RIGHT  | RIGHT   | RIGHT    | RIGHT  | RIGHT  |
#define GPIO_PIN_15 GpioAction::BUTTON_PRESS_DOWN    // DOWN   | DOWN   | DOWN    | DOWN     | DOWN   | DOWN   |
#define GPIO_PIN_16 GpioAction::BUTTON_PRESS_LEFT    // LEFT   | LEFT   | LEFT    | LEFT     | LEFT   | LEFT   |
#define GPIO_PIN_17 GpioAction::BUTTON_PRESS_MACRO_1 // M1     | M1     | M1      | M1       | M1     | M1     |
#define GPIO_PIN_20 GpioAction::BUTTON_PRESS_A2      // A2     | ~      | Capture | ~        | 14     | ~      |
#define GPIO_PIN_21 GpioAction::BUTTON_PRESS_S1      // S1     | Back   | Minus   | Share    | 9      | Coin   |
#define GPIO_PIN_23 GpioAction::BUTTON_PRESS_S2      // S2     | Start  | Plus    | Options  | 10     | Start  |
#define GPIO_PIN_24 GpioAction::BUTTON_PRESS_A1      // A1     | Guide  | Home    | PS       | 13     | ~      |

// Setting GPIO pins to assigned by add-on
//
#define GPIO_PIN_00 GpioAction::ASSIGNED_TO_ADDON // I2C
#define GPIO_PIN_01 GpioAction::ASSIGNED_TO_ADDON // I2C
#define GPIO_PIN_18 GpioAction::ASSIGNED_TO_ADDON // LED

#define USB_PERIPHERAL_ENABLED 1

#define USB_PERIPHERAL_PIN_DPLUS 26
#define USB_PERIPHERAL_PIN_ORDER 0

#define DEFAULT_INPUT_MODE_R1 INPUT_MODE_XBONE
#define DEFAULT_INPUT_MODE_B4 INPUT_MODE_PS5
#define DEFAULT_PS5AUTHENTICATION_TYPE INPUT_MODE_AUTH_TYPE_USB

#define TURBO_ENABLED 1
#define GPIO_PIN_19 GpioAction::BUTTON_PRESS_TURBO

#define BOARD_LEDS_PIN 18
#define LED_BRIGHTNESS_MAXIMUM 100
#define LED_BRIGHTNESS_STEPS 5
#define LED_BRIGHTNESS_DEFAULT 5
#define LED_FORMAT LEDFormat_Proto_LED_FORMAT_GRB
#define LEDS_PER_PIXEL 1
#define LEDS_BASE_ANIMATION_INDEX 1
// #define LEDS_BUTTON_A1 0
#define LEDS_DPAD_LEFT 1
#define LEDS_DPAD_DOWN 2
#define LEDS_DPAD_RIGHT 3
#define LEDS_BUTTON_L3 4
#define LEDS_DPAD_UP 5
#define LEDS_BUTTON_R3 6
#define LEDS_BUTTON_B1 7
#define LEDS_BUTTON_B2 8
#define LEDS_BUTTON_R2 9
#define LEDS_BUTTON_L2 10
#define LEDS_BUTTON_L1 11
#define LEDS_BUTTON_R1 12
#define LEDS_BUTTON_B4 13
#define LEDS_BUTTON_B3 14
#define LEDS_BUTTON_A2 15
// #define LEDS_BUTTON_A1 16
// #define LEDS_BUTTON_S2 17
// #define LEDS_BUTTON_S1 18
// #define LEDS_BUTTON_A2 19
// #define LEDS_BUTTON_TURBO 20

#define HAS_I2C_DISPLAY 1
#define I2C0_ENABLED 1
#define I2C0_PIN_SDA 0
#define I2C0_PIN_SCL 1
#define SPLASH_MODE SPLASH_MODE_STATIC
#define SPLASH_DURATION 3000
// #define INPUT_HISTORY_ENABLED 1
// #define INPUT_HISTORY_SIZE 21

// #define BUTTON_LAYOUT BUTTON_LAYOUT_BOARD_DEFINED_A
// #define BUTTON_LAYOUT_RIGHT BUTTON_LAYOUT_BOARD_DEFINED_B

// Keyboard Host enabled by default
#define KEYBOARD_HOST_ENABLED 1

#define BOARD_LED_ENABLED 1
#define BOARD_LED_TYPE ON_BOARD_LED_MODE_MODE_INDICATOR

#endif