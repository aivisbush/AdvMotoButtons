/* Board profile: Seeed XIAO ESP32C3 with an RGB LED.
 * Build with the XIAO_ESP32C3 board setting and MB_BOARD_ESP32C3 defined.
 */
#pragma once

#if !CONFIG_IDF_TARGET_ESP32C3
#error "This board profile needs an ESP32-C3 board setting"
#endif

constexpr char BOARD_NAME[] = "ESP32-C3";
constexpr char BOARD_ID_TAG[] = "MBBOARD=esp32c3";
constexpr uint16_t BOARD_PRODUCT_ID = 0x4002;

/* XIAO D pins. Buttons and joystick switch to 3V3; the joystick centre
 * switches to GND. D8 (DOWN) and D9 (CENTER) are strapping pins: do not
 * hold them while powering on or uploading.
 */
constexpr uint8_t PIN_LED_RED = D0;
constexpr uint8_t PIN_BUTTON_C = D1;
constexpr uint8_t PIN_BUTTON_B = D2;
constexpr uint8_t PIN_JOYSTICK_UP = D3;
constexpr uint8_t PIN_JOYSTICK_LEFT = D4;
constexpr uint8_t PIN_BUTTON_A = D5;
constexpr uint8_t PIN_LED_BLUE = D6;
constexpr uint8_t PIN_LED_GREEN = D7;
constexpr uint8_t PIN_JOYSTICK_DOWN = D8;    // strapping pin
constexpr uint8_t PIN_JOYSTICK_CENTER = D9;  // strapping pin
constexpr uint8_t PIN_JOYSTICK_RIGHT = D10;
constexpr uint8_t PIN_STATUS_LED = PIN_NONE;
constexpr uint8_t PIN_OLED_SDA = PIN_NONE;
constexpr uint8_t PIN_OLED_SCL = PIN_NONE;

constexpr bool BUTTONS_ACTIVE_LOW = false;
constexpr bool CENTER_ACTIVE_LOW = true;
constexpr bool JOYSTICK_ADC = false;

constexpr bool BOARD_HAS_OLED = false;
constexpr bool BOARD_HAS_RGB_LED = true;
constexpr bool STATUS_LED_ACTIVE_LOW = false;
