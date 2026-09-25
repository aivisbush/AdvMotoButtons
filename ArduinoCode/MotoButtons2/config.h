/*********************************************************************
 MotoButtons 2 - compile-time configuration.

 Everything a builder may want to tune lives here: pins, timings,
 thresholds, HID codes and the Bluetooth identity. Behaviour lives in the
 other units of the sketch:

   inputs.*    reading and debouncing the eight inputs, orientation
   keymap.*    per-mode key tables and the HID report engine
   chords.*    button combinations (mode, display, restart, reset)
   oled.*      the 72x40 status display
   ble_hid.*   Bluetooth HID keyboard and consumer control
   settings.*  persistent settings in NVS
   debug.h     serial logging helpers
*********************************************************************/
#pragma once

#include <Arduino.h>

// Shown on the boot screen and printed to serial at start-up. The tag
// prefix lets the update tools read the version straight from the .bin.
constexpr char FIRMWARE_VERSION_TAG[] = "MBFWVER=2.4.0";
constexpr const char *FIRMWARE_VERSION = FIRMWARE_VERSION_TAG + 8;

/* Which board this build is for, so the update app picks the right .bin.
 * The phone reads it as the product ID of the Bluetooth PnP ID; the tag
 * marks the .bin itself. One pair per board:
 *   esp32c3-oled 0x4001   esp32c3 0x4002   esp32c6 0x4003   nrf52840 0x4004
 */
constexpr char BOARD_ID_TAG[] = "MBBOARD=esp32c3-oled";
constexpr uint16_t BOARD_PRODUCT_ID = 0x4001;

/*------------------------------ DEBUG -------------------------------*/
// General serial logging at 115200 baud. Output is dropped while no USB
// host is attached, so leaving it on costs nothing on the bike.
constexpr bool DEBUG = true;

/* Millivolt diagnostics for the inputs: a snapshot of every input once a
 * second plus a line on every debounced transition, with the measured
 * voltage of each joystick line. Those numbers tell a real press (a few
 * mV) apart from a leaky line (hundreds of mV). See
 * Docs/joystick-input-notes.md. Leave it false for normal riding.
 */
constexpr bool DEBUG_INPUTS = false;
constexpr uint16_t DEBUG_INPUTS_SNAPSHOT_MS = 1000;

/*------------------------- HARDWARE PIN MAP -------------------------*/
/* ESP32-C3 OLED Mini board labels match raw GPIO numbers. GPIO numbers
 * are used instead of Arduino D aliases because the aliases move between
 * board profiles.
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

// All button commons are wired to GND: every input is active low.

/*---------------------------- STATUS LED ----------------------------*/
constexpr bool STATUS_LED_ACTIVE_LOW = true;
constexpr uint16_t STATUS_LED_PULSE_PERIOD_MS = 1200;
// Brightness once a phone is connected (0..255). Full brightness is a
// distraction at night.
constexpr uint8_t STATUS_LED_CONNECTED_BRIGHTNESS = 12;

/*--------------------------- INPUT TIMING ---------------------------*/
/* Debounce: a reading must hold this long before it becomes the state.
 * The joystick already passes through millivolt hysteresis, so both
 * values can stay short. Every press and release is delayed by this.
 */
constexpr uint16_t DEBOUNCE_JOYSTICK_MS = 50;
constexpr uint16_t DEBOUNCE_BUTTON_MS = 40;

/* A lone A, B or C press is reported only after this grace period, so a
 * chord whose second button lands within it does not leak the first key
 * to the phone. Longer catches more chords but adds latency to every
 * single press.
 */
constexpr uint16_t CHORD_GRACE_MS = 50;

// How long an edge-triggered keyboard key (OsmAnd C) stays down.
constexpr uint16_t KEY_TAP_MS = 20;

// Key-up gap inside a zoom-key repeat cycle, and the gap between press and
// release of a consumer (media) key.
constexpr uint16_t REPEAT_RELEASE_GAP_MS = 5;

// Firmware repeat intervals. DMD2 gets none: it implements repeat itself.
constexpr uint16_t ABC_REPEAT_INTERVAL_MS = 250;
constexpr uint16_t VOLUME_REPEAT_INTERVAL_MS = 150;

/* OsmAnd map scrolling. While an arrow is down, OsmAnd scrolls the map
 * itself at a fixed 1 px per 3 ms, and it adds a 200 px nudge whenever a
 * press shorter than 250 ms is released. The firmware therefore taps the
 * arrow while the joystick is held: key down for KEY_DOWN, key up for
 * KEY_UP, again and again, so every cycle ends in exactly one nudge. The
 * speed is 200 px plus the smooth scroll during the press, per cycle.
 *
 * The Bluetooth link matters more than the numbers themselves. Reports
 * leave once per connection event, and an up and a down that share one
 * event reach OsmAnd less than a millisecond apart, where its 3 ms scroll
 * loop misses the release: no nudge that cycle, and the motion stutters.
 * Keep both phases at or above one connection interval (logged at connect
 * when DEBUG is on; typically 7.5 to 15 ms on Android) and OsmAnd sees
 * every tap. Whole multiples of the interval give the most even cadence.
 *
 * KEY_DOWN 0 turns the tapping off: the arrow is simply held and OsmAnd
 * scrolls smoothly but slowly.
 */
constexpr uint16_t OSMAND_DIRECTION_KEY_DOWN_MS = 30;
constexpr uint16_t OSMAND_DIRECTION_KEY_UP_MS = 30;

// Chord hold times.
constexpr uint16_t MODE_TOGGLE_MS = 1000;    // B+C mode change, A+B display toggle
constexpr uint16_t MODE_RESET_MS = 5000;     // A+B+C factory reset, A+C restart

// Main loop pause, so the CPU idles between input scans.
constexpr uint16_t LOOP_TICK_MS = 1;

/*--------------------------- JOYSTICK ADC ---------------------------*/
/* The joystick lines are measured instead of read digitally. Leakage in
 * the pod drags an idle line to roughly 0.7-2.2 V, inside the input's
 * undefined band, so digitalRead() flips at random. A closed contact
 * reads 3-5 mV, an idle line at the rail reads about 2984 mV (the ADC
 * ceiling on this chip). Pressed at or below PRESS, released at or above
 * RELEASE, and the band between keeps the previous state.
 */
constexpr uint16_t JOYSTICK_PRESS_MV = 200;
constexpr uint16_t JOYSTICK_RELEASE_MV = 500;
constexpr uint8_t JOYSTICK_ADC_SAMPLES = 3;
constexpr uint16_t JOYSTICK_ADC_SETTLE_US = 200;

/*--------------------------- ORIENTATION ----------------------------*/
/* Which physical joystick direction acts as UP. Four mounting positions:
 *   0 three buttons on top      2 three buttons on bottom
 *   1 three buttons on left     3 three buttons on right (default)
 * Chosen by holding a direction during the boot window.
 */
constexpr uint8_t ORIENTATION_COUNT = 4;
constexpr uint8_t DEFAULT_ORIENTATION = 3;
constexpr uint16_t ORIENTATION_WINDOW_MS = 2500;  // how long after boot a direction is accepted
constexpr uint16_t ORIENTATION_HOLD_MS = 400;     // how long it must be held steadily

/*------------------------------- OLED -------------------------------*/
constexpr uint16_t OLED_TRANSIENT_MS = 2000;
constexpr uint16_t OLED_ORIENTATION_MESSAGE_MS = 3000;
constexpr uint16_t OLED_SPLASH_MESSAGE_MS = 3000;
constexpr uint16_t OLED_CHORD_FEEDBACK_MS = 1500;
constexpr uint8_t OLED_DEFAULT_CONTRAST = 255;
// Burn-in protection: dim after a quiet spell, blank when nothing has
// happened and no phone is connected. Any press or connection wakes it.
constexpr uint8_t OLED_DIM_CONTRAST = 40;
constexpr uint32_t OLED_DIM_AFTER_MS = 30000;
constexpr uint32_t OLED_BLANK_DISCONNECTED_MS = 60000;

/*------------------------------- BLE --------------------------------*/
/* DMD2 takes plain keys only as its paid Generic Remote Controller. Its
 * DMD Remote slots are fed by the DMD Manage app, which accepts a remote
 * whose name contains "DMD-Remote1".."4". Remote 3 takes plain HID keys;
 * its buttons are assigned in DMD2's remote settings.
 * Any name change needs the phone to forget the pairing before
 * re-pairing. See Docs/dmd2-recognition-notes.md.
 */
constexpr char BLE_DEVICE_NAME[] = "DMD-Remote3";
constexpr char BLE_MANUFACTURER[] = "Bush";
constexpr int8_t BLE_TX_POWER_DBM = 9;

/* Generic HID, not HID keyboard: Samsung takes a keyboard appearance as a
 * physical keyboard and hides the on-screen one. The phone stores it at
 * pairing, so a change needs forget and re-pair.
 */
constexpr uint16_t BLE_APPEARANCE = 0x03C0;

/* Once a phone has bonded, advertise to bonded phones only so that a
 * stranger's phone cannot grab the controller at a fuel stop. If no
 * bonded phone connects within the fallback time, advertising opens up
 * again until the next disconnect. Clearing bonds (A+B+C) reopens
 * pairing. Set false to advertise openly at all times.
 */
constexpr bool BLE_WHITELIST_BONDED = true;
constexpr uint32_t BLE_WHITELIST_OPEN_AFTER_MS = 90000;

// Keyboard report: six simultaneous keys.
constexpr uint8_t KEY_REPORT_SIZE = 6;

/* Firmware update over Bluetooth (ota.cpp). A larger MTU lets the phone
 * send up to MTU - 3 image bytes per write. After a verified image the
 * controller waits a moment so the phone can read the result, then
 * restarts into the new firmware.
 */
constexpr uint16_t BLE_MTU = 247;
constexpr uint16_t OTA_RESTART_DELAY_MS = 1500;
constexpr uint16_t OTA_SCREEN_MS = 3000;

/*----------------------------- WATCHDOG -----------------------------*/
// The main loop must come round at least this often or the chip resets.
constexpr uint32_t WATCHDOG_TIMEOUT_MS = 10000;

/*------------------------------ MODES -------------------------------*/
/* DMD2:   arrows, F5 (centre), F6, F7, Enter - raw key down/up, DMD2
 *         owns repeat and long press
 * OsmAnd: arrows, '+' zoom in, '-' zoom out, 'c' my location
 * Locus:  arrows, volume up (zoom in), '-' zoom out, 'c' follow GPS
 *         on/off (Locus moves the map on the next fix)
 * Media:  volume, track skip, mute, play/pause, screen brightness
 * The tables themselves are in keymap.cpp. The values are saved, so a new
 * mode takes the next number, whatever its place in the B+C cycle.
 */
enum class Mode : uint8_t
{
  DMD2 = 1,
  OsmAnd = 2,
  Media = 3,
  Locus = 4
};
constexpr Mode LAST_MODE = Mode::Locus;
constexpr Mode DEFAULT_MODE = Mode::DMD2;

/*---------------------------- HID CODES -----------------------------*/
// USB HID keyboard usage IDs.
constexpr uint8_t HID_KEY_NONE = 0x00;
constexpr uint8_t HID_KEY_C = 0x06;
constexpr uint8_t HID_KEY_ENTER = 0x28;
constexpr uint8_t HID_KEY_MINUS = 0x2D;
constexpr uint8_t HID_KEY_EQUAL = 0x2E;
constexpr uint8_t HID_KEY_F5 = 0x3E;
constexpr uint8_t HID_KEY_F6 = 0x3F;
constexpr uint8_t HID_KEY_F7 = 0x40;
constexpr uint8_t HID_KEY_ARROW_RIGHT = 0x4F;
constexpr uint8_t HID_KEY_ARROW_LEFT = 0x50;
constexpr uint8_t HID_KEY_ARROW_DOWN = 0x51;
constexpr uint8_t HID_KEY_ARROW_UP = 0x52;

// USB HID consumer-page usages.
constexpr uint16_t HID_USAGE_CONSUMER_BRIGHTNESS_INCREMENT = 0x006F;
constexpr uint16_t HID_USAGE_CONSUMER_BRIGHTNESS_DECREMENT = 0x0070;
constexpr uint16_t HID_USAGE_CONSUMER_SCAN_NEXT = 0x00B5;
constexpr uint16_t HID_USAGE_CONSUMER_SCAN_PREVIOUS = 0x00B6;
constexpr uint16_t HID_USAGE_CONSUMER_PLAY_PAUSE = 0x00CD;
constexpr uint16_t HID_USAGE_CONSUMER_MUTE = 0x00E2;
constexpr uint16_t HID_USAGE_CONSUMER_VOLUME_INCREMENT = 0x00E9;
constexpr uint16_t HID_USAGE_CONSUMER_VOLUME_DECREMENT = 0x00EA;
