/* Board profile: ESP32-C3 OLED Mini (72x40 OLED, one status LED).
 * The default board; build with the XIAO_ESP32C3 board setting.
 */
#pragma once

#if !CONFIG_IDF_TARGET_ESP32C3
#error "This board profile needs an ESP32-C3 board setting"
#endif

constexpr char BOARD_NAME[] = "ESP32-C3 OLED";
constexpr char BOARD_ID_TAG[] = "MBBOARD=esp32c3-oled";
constexpr uint16_t BOARD_PRODUCT_ID = 0x4001;

/* Board labels match raw GPIO numbers.
 *
 * GPIO2, GPIO8 and GPIO9 are ESP32-C3 boot strapping pins. GPIO2 (RIGHT)
 * must be high and GPIO9 (button B) selects download mode when low, so
 * neither may be held while power is applied. Orientation is therefore
 * chosen in a window after boot, never during power-up.
 *
 * Only GPIO0..GPIO4 reach ADC1, which is exactly the joystick.
 * GPIO20 = RX and GPIO21 = TX are left free.
 */
constexpr uint8_t PIN_JOYSTICK_UP = 0;
constexpr uint8_t PIN_JOYSTICK_CENTER = 1;
constexpr uint8_t PIN_JOYSTICK_RIGHT = 2;    // strapping pin
constexpr uint8_t PIN_JOYSTICK_DOWN = 3;
constexpr uint8_t PIN_JOYSTICK_LEFT = 4;
constexpr uint8_t PIN_OLED_SDA = 5;          // reserved by the onboard OLED
constexpr uint8_t PIN_OLED_SCL = 6;          // reserved by the onboard OLED
constexpr uint8_t PIN_BUTTON_A = 7;
constexpr uint8_t PIN_STATUS_LED = 8;        // onboard LED, strapping pin
constexpr uint8_t PIN_BUTTON_B = 9;          // strapping pin
constexpr uint8_t PIN_BUTTON_C = 10;
constexpr uint8_t PIN_LED_RED = PIN_NONE;
constexpr uint8_t PIN_LED_GREEN = PIN_NONE;
constexpr uint8_t PIN_LED_BLUE = PIN_NONE;

// All button commons are wired to GND.
constexpr bool BUTTONS_ACTIVE_LOW = true;
constexpr bool CENTER_ACTIVE_LOW = true;
// Joystick lines are measured with the ADC (see inputs.cpp).
constexpr bool JOYSTICK_ADC = true;

constexpr bool BOARD_HAS_OLED = true;
constexpr bool BOARD_HAS_RGB_LED = false;
constexpr bool STATUS_LED_ACTIVE_LOW = true;
