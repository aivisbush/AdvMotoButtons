/*********************************************************************
License: GNU GENERAL PUBLIC LICENSE; Version 3, 29 June 2007
Version: 2.1 (DMD2, OsmAnd, Media modes)
Device: Seeed XIAO ESP32C3 (MotoButtons 2)
*********************************************************************/
#include <Arduino.h>
#include <NimBLEDevice.h>
#include <NimBLEHIDDevice.h>
#include <Preferences.h>
#include <string.h>

/*============================ USER CONFIGURATION ============================*/

/*---- 0. DEBUG ----*/
#define DEBUG false

/*---- 1. BLE ----*/
const char BLE_DEVICE_NAME[] = "Bush Moto BTx";
const char BLE_MANUFACTURER[] = "Bush";
#define BLE_TX_POWER 9           // dBm
#define BLE_CONN_INTERVAL_MIN 6  // x 1.25 ms
#define BLE_CONN_INTERVAL_MAX 18 // x 1.25 ms

/*---- 2. WIRING ----
  Inputs switch to 3V3 (internal pull-down); CENTER switches to GND (internal pull-up).
  D8 and D9 are ESP32-C3 boot strapping pins: do not hold DOWN or CENTER while powering on or uploading.
*/
#define PIN_RGB_LED_RED D0
#define PIN_BUTTON_C D1
#define PIN_BUTTON_B D2
#define PIN_JOYSTICK_UP D3
#define PIN_JOYSTICK_LEFT D4
#define PIN_BUTTON_A D5
#define PIN_RGB_LED_BLUE D6
#define PIN_RGB_LED_GREEN D7
#define PIN_JOYSTICK_DOWN D8
#define PIN_JOYSTICK_CENTER D9
#define PIN_JOYSTICK_RIGHT D10

/*---- 3. RGB COLORS ----*/
typedef enum { Red, Blue, Green, Magenta, White, Orange, Off, N_COLORS } Color;
const uint8_t COLOR_RGB[N_COLORS][3] = {
    {255, 0, 0},     // Red
    {0, 0, 255},     // Blue
    {0, 255, 0},     // Green
    {245, 0, 245},   // Magenta
    {245, 245, 245}, // White
    {255, 60, 0},    // Orange
    {0, 0, 0},       // Off
};
// states
#define POWER_ON_COLOR Orange           // steady while booting
#define SETUP_COMPLETE_COLOR Orange     // steady once BLE is up
#define BLE_COLOR Blue                  // blinking while not connected
#define BUTTON_ORIENTATION_COLOR Orange // orientation+1 flashes after orientation change at power-on
#define BOND_RESET_COLOR Orange         // 4 flashes after bond reset
// modes: steady while connected, one long blink on mode change
#define DMD2_MODE_COLOR Blue
#define OSMAND_MODE_COLOR Green
#define MEDIA_MODE_COLOR Magenta

/*---- 4. MODES AND KEY MAPS ----
  Mode change: hold B+C
*/
enum ButtonId { BTN_UP = 0, BTN_DOWN, BTN_LEFT, BTN_RIGHT, BTN_CENTER, BTN_A, BTN_B, BTN_C, N_BUTTONS };
#define BTN_BIT(id) (1u << (id))

#define MODE_CYCLE_COMBO (BTN_BIT(BTN_B) | BTN_BIT(BTN_C))
#define MODE_CYCLE_HOLD_MS 1000
#define DEFAULT_MODE DMD2
#define CONSUMER_KEY_HOLD_MS 50 // media key press length

// HID usages for the key maps
#define HID_KEY_NONE 0x00
#define HID_KEY_C 0x06
#define HID_KEY_ENTER 0x28
#define HID_KEY_MINUS 0x2D
#define HID_KEY_EQUAL 0x2E
#define HID_KEY_F6 0x3F
#define HID_KEY_F7 0x40
#define HID_KEY_F8 0x41
#define HID_KEY_ARROW_RIGHT 0x4F
#define HID_KEY_ARROW_LEFT 0x50
#define HID_KEY_ARROW_DOWN 0x51
#define HID_KEY_ARROW_UP 0x52
#define HID_USAGE_CONSUMER_BRIGHTNESS_INCREMENT 0x006F
#define HID_USAGE_CONSUMER_BRIGHTNESS_DECREMENT 0x0070
#define HID_USAGE_CONSUMER_SCAN_NEXT 0x00B5
#define HID_USAGE_CONSUMER_SCAN_PREVIOUS 0x00B6
#define HID_USAGE_CONSUMER_PLAY_PAUSE 0x00CD
#define HID_USAGE_CONSUMER_MUTE 0x00E2
#define HID_USAGE_CONSUMER_VOLUME_INCREMENT 0x00E9
#define HID_USAGE_CONSUMER_VOLUME_DECREMENT 0x00EA

typedef enum { DMD2 = 1, OsmAnd = 2, MEDIA = 3 } Mode; // stored in settings, keep values stable
// Media: volume/brightness keep stepping while held
// OsmAnd: fast map scroll by tapping the arrow (45/40 ms tuned on Android, not tested on iPhone); zoom A/B repeats while held
struct Repeat { uint8_t mask; uint16_t delayMs, intervalMs, releaseMs; }; // buttons that auto-repeat while held
#define N_REPEAT 2
struct KeyMap { uint16_t key[N_BUTTONS]; bool consumer; Repeat rep[N_REPEAT]; };
struct ModeDef { Mode id; Color color; KeyMap keys; };
#define DIRECTIONS (BTN_BIT(BTN_UP) | BTN_BIT(BTN_DOWN) | BTN_BIT(BTN_LEFT) | BTN_BIT(BTN_RIGHT))

const ModeDef MODES[] = {
    {DMD2, DMD2_MODE_COLOR,
     {{HID_KEY_ARROW_UP, HID_KEY_ARROW_DOWN, HID_KEY_ARROW_LEFT, HID_KEY_ARROW_RIGHT,
       HID_KEY_F8, HID_KEY_F6, HID_KEY_F7, HID_KEY_ENTER}, false, {{0, 0, 0, 0}, {0, 0, 0, 0}}}},
    {OsmAnd, OSMAND_MODE_COLOR,
     {{HID_KEY_ARROW_UP, HID_KEY_ARROW_DOWN, HID_KEY_ARROW_LEFT, HID_KEY_ARROW_RIGHT,
       HID_KEY_NONE, HID_KEY_EQUAL, HID_KEY_MINUS, HID_KEY_C}, false,
      {{DIRECTIONS, 0, 45, 40}, {BTN_BIT(BTN_A) | BTN_BIT(BTN_B), 150, 150, 40}}}},
    {MEDIA, MEDIA_MODE_COLOR,
     {{HID_USAGE_CONSUMER_VOLUME_INCREMENT, HID_USAGE_CONSUMER_VOLUME_DECREMENT,
       HID_USAGE_CONSUMER_SCAN_PREVIOUS, HID_USAGE_CONSUMER_SCAN_NEXT,
       HID_USAGE_CONSUMER_MUTE, HID_USAGE_CONSUMER_PLAY_PAUSE,
       HID_USAGE_CONSUMER_BRIGHTNESS_INCREMENT, HID_USAGE_CONSUMER_BRIGHTNESS_DECREMENT}, true,
      {{BTN_BIT(BTN_UP) | BTN_BIT(BTN_DOWN) | BTN_BIT(BTN_B) | BTN_BIT(BTN_C), 400, 150, 0}, {0, 0, 0, 0}}}},
};
#define N_MODES ((uint8_t)(sizeof(MODES) / sizeof(MODES[0])))

/*---- 5. SPECIAL FUNCTIONS ----*/
// LED brightness: Hold A+B
#define BRIGHTNESS_COMBO (BTN_BIT(BTN_A) | BTN_BIT(BTN_B))
#define BRIGHTNESS_HOLD_MS 1000
#define LED_BRIGHTNESS_STEP 20
#define LED_BRIGHTNESS_STEP_MS 200
#define LED_BRIGHTNESS_OFF_PAUSE_MS 1500 // pause at "off"
#define DEFAULT_LED_BRIGHTNESS 0

// Bond reset: Hold A+B+C for 5 seconds. This clears all bonds and resets the settings.
#define BOND_RESET_COMBO (BTN_BIT(BTN_A) | BTN_BIT(BTN_B) | BTN_BIT(BTN_C))
#define BOND_RESET_HOLD_MS 5000

// Joystick orientation: hold one joystick direction button while powering on, it becomes UP.
#define DEFAULT_BUTTON_MAP 3
#define STARTUP_ORIENTATION_WINDOW_MS 500
const uint8_t ORIENTATION_PINS[4][4] = {
    {PIN_JOYSTICK_RIGHT, PIN_JOYSTICK_LEFT, PIN_JOYSTICK_UP, PIN_JOYSTICK_DOWN}, // 0: buttons on top
    {PIN_JOYSTICK_DOWN, PIN_JOYSTICK_UP, PIN_JOYSTICK_RIGHT, PIN_JOYSTICK_LEFT}, // 1: buttons on left
    {PIN_JOYSTICK_LEFT, PIN_JOYSTICK_RIGHT, PIN_JOYSTICK_DOWN, PIN_JOYSTICK_UP}, // 2: buttons on bottom
    {PIN_JOYSTICK_UP, PIN_JOYSTICK_DOWN, PIN_JOYSTICK_LEFT, PIN_JOYSTICK_RIGHT}, // 3: buttons on right
};

/*---- 6. TIMING ----*/
#define DEBOUNCE_TIME_MS 50
#define LOOP_DELAY_MS 2 // lets the RTOS idle task sleep
#define BLE_BLINK_HALF_PERIOD_MS 200

/*========================== END USER CONFIGURATION ==========================*/

#define DEBUG_PRINT(x)   do { if (DEBUG) Serial.print(x); } while (0)
#define DEBUG_PRINTLN(x) do { if (DEBUG) Serial.println(x); } while (0)
#define SETTINGS_NAMESPACE "motobuttons"
#define N_KEY_REPORT 6
#define ABC_MASK (BTN_BIT(BTN_A) | BTN_BIT(BTN_B) | BTN_BIT(BTN_C))
#define KEYBOARD_REPORT_ID 1
#define CONSUMER_REPORT_ID 2

const uint8_t HID_REPORT_DESCRIPTOR[] = {
    0x05, 0x01,       // Usage Page (Generic Desktop)
    0x09, 0x06,       // Usage (Keyboard)
    0xA1, 0x01,       // Collection (Application)
    0x85, 0x01,       //   Report ID (1)
    0x05, 0x07,       //   Usage Page (Keyboard)
    0x19, 0xE0,       //   Usage Minimum (Left Control)
    0x29, 0xE7,       //   Usage Maximum (Right GUI)
    0x15, 0x00,       //   Logical Minimum (0)
    0x25, 0x01,       //   Logical Maximum (1)
    0x75, 0x01,       //   Report Size (1)
    0x95, 0x08,       //   Report Count (8)
    0x81, 0x02,       //   Input (Data, Variable, Absolute)
    0x95, 0x01,       //   Report Count (1)
    0x75, 0x08,       //   Report Size (8)
    0x81, 0x01,       //   Input (Constant)
    0x95, 0x06,       //   Report Count (6)
    0x75, 0x08,       //   Report Size (8)
    0x15, 0x00,       //   Logical Minimum (0)
    0x25, 0x65,       //   Logical Maximum (101)
    0x05, 0x07,       //   Usage Page (Keyboard)
    0x19, 0x00,       //   Usage Minimum (Reserved)
    0x29, 0x65,       //   Usage Maximum (Keyboard Application)
    0x81, 0x00,       //   Input (Data, Array, Absolute)
    0xC0,             // End Collection
    0x05, 0x0C,       // Usage Page (Consumer)
    0x09, 0x01,       // Usage (Consumer Control)
    0xA1, 0x01,       // Collection (Application)
    0x85, 0x02,       //   Report ID (2)
    0x15, 0x00,       //   Logical Minimum (0)
    0x26, 0xFF, 0x03, //   Logical Maximum (1023)
    0x19, 0x00,       //   Usage Minimum (Unassigned)
    0x2A, 0xFF, 0x03, //   Usage Maximum (1023)
    0x75, 0x10,       //   Report Size (16)
    0x95, 0x01,       //   Report Count (1)
    0x81, 0x00,       //   Input (Data, Array, Absolute)
    0xC0              // End Collection
};

NimBLEServer *bleServer = nullptr;
NimBLEHIDDevice *blehid = nullptr;
NimBLECharacteristic *keyboardInput = nullptr, *consumerInput = nullptr;
bool BLE_connected = false;
volatile bool bleConnectEvent = false, bleDisconnectEvent = false;

Color LEDState = Off;
int LEDbrightness = DEFAULT_LED_BRIGHTNESS; // 0 = full, 255 = off
struct LedFlash { bool active; Color color; uint16_t phasesLeft, halfPeriodMs; unsigned long lastToggleMs; bool ledOn; };
struct LedFlashRequest { bool pending; Color color; uint8_t count; uint16_t periodMs; }; // one queued flash
LedFlash ledFlash = {false, Off, 0, 0, 0, false};
LedFlashRequest ledFlashQueued = {false, Off, 0, 0};
unsigned long bleBlinkLastToggleMs = 0;

Mode currentMode;
uint8_t buttonOrientation = DEFAULT_BUTTON_MAP;

uint8_t keyReport[N_KEY_REPORT];
bool keyReportChanged = false;       // a debounced button changed
bool forceKeyReport = false;         // send again (key-up of a tap, or retry after failed send)
bool centerTapPending = false;       // center released, key not yet sent
bool consumerReleasePending = false; // consumer key is down, release after CONSUMER_KEY_HOLD_MS
unsigned long consumerPressTime = 0;
struct RepeatState { bool releaseSent; unsigned long lastTime, releaseTime; }; // releaseSent: key-up sent, key-down due
RepeatState repState[N_REPEAT];

struct Button { uint8_t pin; bool activeLow, state, prior, flipped; unsigned long time, edge; const char *name; }; // time = press, edge = last raw change
Button buttons[N_BUTTONS] = { // direction pins are set by setButtonMapping()
    {PIN_JOYSTICK_UP, false, false, false, false, 0, 0, "UP"}, {PIN_JOYSTICK_DOWN, false, false, false, false, 0, 0, "DOWN"},
    {PIN_JOYSTICK_LEFT, false, false, false, false, 0, 0, "LEFT"}, {PIN_JOYSTICK_RIGHT, false, false, false, false, 0, 0, "RIGHT"},
    {PIN_JOYSTICK_CENTER, true, false, false, false, 0, 0, "CENTER"},
    {PIN_BUTTON_A, false, false, false, false, 0, 0, "A"}, {PIN_BUTTON_B, false, false, false, false, 0, 0, "B"}, {PIN_BUTTON_C, false, false, false, false, 0, 0, "C"},
};

bool modeButtonsReleased = true, modeComboConsumed = false, bondResetComboConsumed = false;
unsigned long brightnessAdjustTime = 0;
bool brightnessComboConsumed = false, brightnessSettingsDirty = false;

bool writeSettings();
void applySteadyLED();
void resetRepeat();

#if DEBUG
// debug: serial "r <group> <intervalMs> <releaseMs>" overrides repeat timing of rep[group]
Repeat tune[N_REPEAT];
void handleSerialTuning() {
  static char buf[24];
  static uint8_t len = 0;
  while (Serial.available()) {
    char c = Serial.read();
    if (c == '\n' || c == '\r') {
      buf[len] = 0; len = 0;
      int g, i, r;
      if (sscanf(buf, "r %d %d %d", &g, &i, &r) == 3 && g >= 0 && g < N_REPEAT) {
        tune[g].intervalMs = i; tune[g].releaseMs = r;
        Serial.print("repeat "); Serial.print(g); Serial.print(" set: "); Serial.print(i); Serial.print("/"); Serial.println(r);
      }
    } else if (len < sizeof(buf) - 1) buf[len++] = c;
  }
}
#endif

/*------------------------------ Modes -------------------------------*/
int modeIndex(uint8_t mode) {
  for (uint8_t i = 0; i < N_MODES; i++) if (MODES[i].id == mode) return i;
  return -1;
}

const ModeDef *modeDef(Mode mode) {
  int i = modeIndex(mode);
  return &MODES[i < 0 ? 0 : i];
}

Mode getNextMode(Mode mode) {
  int i = modeIndex(mode);
  return MODES[i < 0 ? 0 : (i + 1) % N_MODES].id;
}

/*------------------------------ LED ---------------------------------*/
void setRGBColor(Color color) {
  // common anode: 255 = off; scaled by LEDbrightness
  auto rgbAnalog = [](uint8_t ch) -> uint8_t { return (uint8_t)(255 - (((uint16_t)ch * (255 - LEDbrightness) + 127) / 255)); };
  if (color >= N_COLORS) color = Off;
  LEDState = color;
  analogWrite(PIN_RGB_LED_RED, rgbAnalog(COLOR_RGB[color][0]));
  analogWrite(PIN_RGB_LED_GREEN, rgbAnalog(COLOR_RGB[color][1]));
  analogWrite(PIN_RGB_LED_BLUE, rgbAnalog(COLOR_RGB[color][2]));
}

void startFlash(Color color, uint8_t count, uint16_t periodMs) {
  if (count == 0) return;
  if (ledFlash.active) { ledFlashQueued = {true, color, count, periodMs}; return; }
  ledFlash = {true, color, (uint16_t)(count * 2), (uint16_t)(periodMs / 2), millis(), false};
  setRGBColor(Off);
}

void applySteadyLED() {
  if (BLE_connected) { setRGBColor(modeDef(currentMode)->color); return; }
  setRGBColor(Off);
  bleBlinkLastToggleMs = millis();
}

void updateLED() {
  unsigned long now = millis();
  if (ledFlash.active) {
    if (now - ledFlash.lastToggleMs < ledFlash.halfPeriodMs) return;
    ledFlash.lastToggleMs = now;
    if (--ledFlash.phasesLeft == 0) {
      ledFlash.active = false;
      if (ledFlashQueued.pending) {
        ledFlashQueued.pending = false;
        startFlash(ledFlashQueued.color, ledFlashQueued.count, ledFlashQueued.periodMs);
      } else applySteadyLED();
    } else {
      ledFlash.ledOn = !ledFlash.ledOn;
      setRGBColor(ledFlash.ledOn ? ledFlash.color : Off);
    }
    return;
  }
  if (!BLE_connected && now - bleBlinkLastToggleMs >= BLE_BLINK_HALF_PERIOD_MS) {
    bleBlinkLastToggleMs = now;
    setRGBColor(LEDState == Off ? BLE_COLOR : Off);
  }
}

void indicateMode(Mode mode) { startFlash(modeDef(mode)->color, 1, 1000); }

/*---------------------------- Buttons -------------------------------*/
// one joystick contact held at power-on -> orientation with that contact as UP, else -1
int getButtonMapSelection() {
  const uint8_t contacts[4] = {PIN_JOYSTICK_UP, PIN_JOYSTICK_DOWN, PIN_JOYSTICK_LEFT, PIN_JOYSTICK_RIGHT};
  unsigned long startMs = millis();
  while (millis() - startMs < STARTUP_ORIENTATION_WINDOW_MS) {
    uint8_t pressedCount = 0, heldPin = 0;
    for (uint8_t i = 0; i < 4; i++) if (digitalRead(contacts[i])) { pressedCount++; heldPin = contacts[i]; }
    if (pressedCount > 1) return -1;
    if (pressedCount == 1) {
      for (uint8_t o = 0; o < 4; o++) if (ORIENTATION_PINS[o][0] == heldPin) return o;
      return -1;
    }
    delay(10);
  }
  return -1;
}

bool setButtonMapping(uint8_t buttMap) {
  if (buttMap > 3) return true;
  for (uint8_t d = BTN_UP; d <= BTN_RIGHT; d++) buttons[d].pin = ORIENTATION_PINS[buttMap][d];
  return false;
}

// press: 2 consecutive samples (immediate); release: contact open for DEBOUNCE_TIME_MS (chatter while held is ignored)
bool debounceButton(Button &b) {
  bool reading = digitalRead(b.pin) != b.activeLow;
  unsigned long now = millis();
  bool stateChanged = false;
  if (reading != b.prior) b.edge = now;
  if (reading && b.prior && !b.state) { b.state = true; b.time = now; stateChanged = true; }
  else if (!reading && b.state && now - b.edge > DEBOUNCE_TIME_MS) { b.state = false; stateChanged = true; }
  if (stateChanged) {
    b.flipped = true;
    if (DEBUG) { Serial.print("Button "); Serial.print(b.name); Serial.println(b.state ? " pressed" : " released"); }
  }
  b.prior = reading;
  return stateChanged;
}

inline bool pressed(ButtonId id) { return buttons[id].state; }

uint8_t pressedMask() {
  uint8_t mask = 0;
  for (uint8_t i = 0; i < N_BUTTONS; i++) if (buttons[i].state) mask |= BTN_BIT(i);
  return mask;
}

bool comboActive(uint8_t combo) { return (pressedMask() & ABC_MASK) == combo; } // exactly this A/B/C set
bool comboReleased(uint8_t combo) { return (pressedMask() & combo) == 0; }

unsigned long comboHoldMs(uint8_t combo) { // since the last button of the combo was pressed
  unsigned long latest = 0;
  for (uint8_t i = 0; i < N_BUTTONS; i++) if ((combo & BTN_BIT(i)) && buttons[i].time > latest) latest = buttons[i].time;
  return millis() - latest;
}

void clearFlips(uint8_t mask) { for (uint8_t i = 0; i < N_BUTTONS; i++) if (mask & BTN_BIT(i)) buttons[i].flipped = false; }

void resetKeyReportState() {
  memset(keyReport, HID_KEY_NONE, N_KEY_REPORT);
  keyReportChanged = forceKeyReport = consumerReleasePending = centerTapPending = false;
  resetRepeat();
  clearFlips(0xFF);
}

/*--------------------------- HID transport --------------------------*/
bool sendKeyboardRaw(const uint8_t *keys) { // returns false if the notification could not be queued
  uint8_t report[8] = {0};
  memcpy(&report[2], keys, N_KEY_REPORT);
  keyboardInput->setValue(report, sizeof(report));
  return keyboardInput->notify(report, sizeof(report));
}

bool sendConsumerRaw(uint16_t usage) {
  uint8_t report[2] = {(uint8_t)(usage & 0xFF), (uint8_t)(usage >> 8)};
  consumerInput->setValue(report, sizeof(report));
  return consumerInput->notify(report, sizeof(report));
}

void releaseAllKeys() {
  uint8_t none[N_KEY_REPORT] = {0};
  sendKeyboardRaw(none);
}

void applyDefaultSettings() {
  currentMode = DEFAULT_MODE;
  buttonOrientation = DEFAULT_BUTTON_MAP;
  LEDbrightness = DEFAULT_LED_BRIGHTNESS;
  setButtonMapping(buttonOrientation);
}

/*----------------------------- Combos -------------------------------*/
bool handleBondResetCombo() {
  if (bondResetComboConsumed) {
    clearFlips(ABC_MASK);
    if (comboReleased(BOND_RESET_COMBO)) bondResetComboConsumed = false;
    return true;
  }
  if (!comboActive(BOND_RESET_COMBO)) return false;

  clearFlips(ABC_MASK);
  modeButtonsReleased = modeComboConsumed = brightnessComboConsumed = brightnessSettingsDirty = false;
  brightnessAdjustTime = 0;
  if (comboHoldMs(BOND_RESET_COMBO) > BOND_RESET_HOLD_MS) {
    DEBUG_PRINTLN("Clearing BLE bonds and resetting settings...");
    if (BLE_connected) releaseAllKeys();
    NimBLEDevice::deleteAllBonds();
    Preferences prefs;
    if (prefs.begin(SETTINGS_NAMESPACE, false)) { prefs.clear(); prefs.end(); }
    applyDefaultSettings();
    writeSettings();
    for (uint16_t h : bleServer->getPeerDevices()) bleServer->disconnect(h);
    startFlash(BOND_RESET_COLOR, 4, 500);
    indicateMode(currentMode);
    bondResetComboConsumed = true;
  }
  return true;
}

void handleModeCycleCombo() {
  if ((pressedMask() & MODE_CYCLE_COMBO) != MODE_CYCLE_COMBO) modeButtonsReleased = true;

  if (comboActive(MODE_CYCLE_COMBO) && modeButtonsReleased && comboHoldMs(MODE_CYCLE_COMBO) > MODE_CYCLE_HOLD_MS) {
    currentMode = getNextMode(currentMode);
    DEBUG_PRINT("Mode advanced to "); DEBUG_PRINTLN(currentMode);
    if (BLE_connected) releaseAllKeys();
    clearFlips(0xFF);
    centerTapPending = modeButtonsReleased = false;
    resetRepeat();
    modeComboConsumed = true;
    writeSettings();
    indicateMode(currentMode);
  } else if (modeComboConsumed) {
    clearFlips(MODE_CYCLE_COMBO);
    if (comboReleased(MODE_CYCLE_COMBO)) modeComboConsumed = false;
  }
}

void handleBrightnessCombo() {
  unsigned long now = millis();
  if (comboActive(BRIGHTNESS_COMBO)) {
    unsigned long stepMs = (LEDbrightness == 255) ? LED_BRIGHTNESS_OFF_PAUSE_MS : LED_BRIGHTNESS_STEP_MS;
    if (comboHoldMs(BRIGHTNESS_COMBO) > BRIGHTNESS_HOLD_MS && (brightnessAdjustTime == 0 || now - brightnessAdjustTime >= stepMs)) {
      LEDbrightness -= LED_BRIGHTNESS_STEP;
      if (LEDbrightness < 0) LEDbrightness = 255;
      brightnessAdjustTime = now;
      brightnessComboConsumed = brightnessSettingsDirty = true;
      clearFlips(BRIGHTNESS_COMBO);
      setRGBColor(LEDState);
      DEBUG_PRINT("LED brightness changed to "); DEBUG_PRINTLN(LEDbrightness);
    }
    return;
  }
  brightnessAdjustTime = 0;
  if (!brightnessComboConsumed) return;
  clearFlips(BRIGHTNESS_COMBO);
  if (comboReleased(BRIGHTNESS_COMBO)) {
    brightnessComboConsumed = false;
    if (brightnessSettingsDirty) { writeSettings(); brightnessSettingsDirty = false; }
  }
}

void updateButtons() {
  bool stateChanged = false;
  for (uint8_t i = 0; i < N_BUTTONS; i++) stateChanged |= debounceButton(buttons[i]);
  keyReportChanged = stateChanged;

  // center fires on release
  if (buttons[BTN_CENTER].flipped) {
    buttons[BTN_CENTER].flipped = false;
    if (!pressed(BTN_CENTER)) centerTapPending = true;
  }

  if (handleBondResetCombo()) return;
  handleModeCycleCombo();
  handleBrightnessCombo();
}

/*--------------------------- Key reports ----------------------------*/
// held and not blocked by center / combo rules
bool keyActive(uint8_t id) {
  if (id <= BTN_RIGHT) return buttons[id].state && !pressed(BTN_CENTER);
  if (id >= BTN_A) return comboActive(BTN_BIT(id));
  return false;
}

uint8_t repeatableMask(const Repeat *rp, unsigned long now) {
  uint8_t mask = 0;
  for (uint8_t id = 0; id < N_BUTTONS; id++)
    if ((rp->mask & BTN_BIT(id)) && keyActive(id) && now - buttons[id].time >= rp->delayMs) mask |= BTN_BIT(id);
  return mask;
}

void resetRepeat() {
  for (uint8_t s = 0; s < N_REPEAT; s++) repState[s] = {false, millis(), 0};
}

bool buildKeyReport(const KeyMap *map, uint8_t suppressMask) { // returns true if the center tap is included
  uint8_t n = 0;
  bool includesCenter = false;
  memset(keyReport, HID_KEY_NONE, N_KEY_REPORT);
  auto pushKey = [&](uint16_t key) { if (key != HID_KEY_NONE && n < N_KEY_REPORT) keyReport[n++] = (uint8_t)key; };

  for (uint8_t id = 0; id < N_BUTTONS; id++) {
    if (id == BTN_CENTER) continue;
    buttons[id].flipped = false;
    if (!(suppressMask & BTN_BIT(id)) && keyActive(id)) pushKey(map->key[id]);
  }
  if (centerTapPending) {
    if (map->key[BTN_CENTER] != HID_KEY_NONE) { pushKey(map->key[BTN_CENTER]); includesCenter = true; }
    else centerTapPending = false;
  }
  return includesCenter;
}

bool sendKeyboardReport(const KeyMap *map, uint8_t suppressMask) { // failed sends are retried next loop
  bool includesCenter = buildKeyReport(map, suppressMask);
  if (!sendKeyboardRaw(keyReport)) {
    DEBUG_PRINTLN("Key report send failed, will retry.");
    forceKeyReport = true;
    return false;
  }
  if (includesCenter) { centerTapPending = false; forceKeyReport = true; } // key-up of the tap
  return true;
}

// press only, released after CONSUMER_KEY_HOLD_MS
bool sendConsumerKey(uint16_t usage) {
  if (!sendConsumerRaw(usage)) return false;
  consumerReleasePending = true;
  consumerPressTime = millis();
  DEBUG_PRINT("Media key "); DEBUG_PRINTLN(usage);
  return true;
}

// one consumer key at a time
void handleConsumerKeys(const KeyMap *map) {
  unsigned long now = millis();
  for (uint8_t id = 0; id < N_BUTTONS; id++) {
    if (id == BTN_CENTER || !buttons[id].flipped) continue;
    if (keyActive(id)) {
      if (!sendConsumerKey(map->key[id])) return;
      for (uint8_t s = 0; s < N_REPEAT; s++) repState[s].lastTime = now;
      buttons[id].flipped = false;
      return;
    }
    buttons[id].flipped = false;
  }
  if (centerTapPending) {
    if (!sendConsumerKey(map->key[BTN_CENTER])) return;
    centerTapPending = false;
    return;
  }
  for (uint8_t s = 0; s < N_REPEAT; s++) {
    uint8_t rep = repeatableMask(&map->rep[s], now);
    if (!rep || now - repState[s].lastTime < map->rep[s].intervalMs) continue;
    for (uint8_t id = 0; id < N_BUTTONS; id++)
      if (rep & BTN_BIT(id)) { if (sendConsumerKey(map->key[id])) repState[s].lastTime = now; return; }
  }
}

void handleKeyReports() {
  const KeyMap *map = &modeDef(currentMode)->keys;
  unsigned long now = millis();
  // release held consumer key (any mode, retried)
  if (consumerReleasePending && now - consumerPressTime >= CONSUMER_KEY_HOLD_MS && sendConsumerRaw(0))
    consumerReleasePending = false;
  if (map->consumer) {
    forceKeyReport = false;
    if (!consumerReleasePending) handleConsumerKeys(map);
    return;
  }

  // each repeat group cycles key-down (interval - release) / key-up (release); suppress = keys currently in key-up
  bool phaseChanged = false;
  uint8_t suppress = 0;
  for (uint8_t s = 0; s < N_REPEAT; s++) {
    Repeat rp = map->rep[s];
#if DEBUG
    if (tune[s].intervalMs) { rp.intervalMs = tune[s].intervalMs; rp.releaseMs = tune[s].releaseMs; }
#endif
    RepeatState &st = repState[s];
    uint8_t rep = repeatableMask(&rp, now);
    if (!rep) { st.releaseSent = false; st.lastTime = now; continue; }
    if (st.releaseSent) {
      if (now - st.releaseTime >= rp.releaseMs) { st.releaseSent = false; st.lastTime = now; phaseChanged = true; }
      else suppress |= rep;
    } else if (now - st.lastTime >= (unsigned long)(rp.intervalMs > rp.releaseMs ? rp.intervalMs - rp.releaseMs : 0)) {
      st.releaseSent = true; st.releaseTime = now; suppress |= rep; phaseChanged = true;
    }
  }
  if (keyReportChanged || forceKeyReport || phaseChanged) {
    bool changed = keyReportChanged;
    forceKeyReport = false;
    if (sendKeyboardReport(map, suppress) && changed) DEBUG_PRINTLN("Report sent");
  }
}

/*----------------------------- Settings -----------------------------*/
bool writeSettings() {
  Preferences prefs;
  if (!prefs.begin(SETTINGS_NAMESPACE, false)) { DEBUG_PRINTLN("Error opening settings for writing!"); return false; }
  DEBUG_PRINT("Writing settings: "); DEBUG_PRINT(currentMode); DEBUG_PRINT(","); DEBUG_PRINT(buttonOrientation); DEBUG_PRINT(","); DEBUG_PRINTLN(LEDbrightness);
  bool ok = prefs.putUChar("mode", (uint8_t)currentMode) && prefs.putUChar("orientation", buttonOrientation) && prefs.putUChar("brightness", (uint8_t)LEDbrightness);
  prefs.end();
  if (!ok) DEBUG_PRINTLN("Error writing settings!");
  return ok;
}

bool readSettings() { // returns true on error (defaults applied)
  Preferences prefs;
  if (!prefs.begin(SETTINGS_NAMESPACE, true)) { DEBUG_PRINTLN("Settings not found."); return true; }
  bool complete = prefs.isKey("mode") && prefs.isKey("orientation") && prefs.isKey("brightness");
  uint8_t mode = prefs.getUChar("mode", 0), orient = prefs.getUChar("orientation", 0xFF), bright = prefs.getUChar("brightness", 0);
  prefs.end();
  DEBUG_PRINT("Read settings: "); DEBUG_PRINT(mode); DEBUG_PRINT(","); DEBUG_PRINT(orient); DEBUG_PRINT(","); DEBUG_PRINTLN(bright);

  if (!complete || modeIndex(mode) < 0 || orient > 3) {
    DEBUG_PRINTLN("Settings invalid, restoring defaults.");
    applyDefaultSettings();
    return true;
  }
  currentMode = (Mode)mode;
  buttonOrientation = orient;
  LEDbrightness = bright;
  setButtonMapping(buttonOrientation);
  setRGBColor(LEDState);
  return false;
}

/*-------------------------- BLE callbacks ---------------------------*/
// run in the BLE host task: only set flags, handled in loop()
class ServerCallbacks : public NimBLEServerCallbacks {
  void onConnect(NimBLEServer *, NimBLEConnInfo &) override { bleConnectEvent = true; }
  void onDisconnect(NimBLEServer *, NimBLEConnInfo &, int) override { bleDisconnectEvent = true; }
  // pairing failed with a stale bond: drop it so the phone pairs again
  void onAuthenticationComplete(NimBLEConnInfo &info) override {
    if (info.isEncrypted()) return;
    NimBLEDevice::deleteBond(info.getIdAddress());
    bleServer->disconnect(info.getConnHandle());
  }
};

void handleBLEEvents() {
  bool connectedNow = bleServer->getConnectedCount() > 0;
  if (bleDisconnectEvent || (BLE_connected && !connectedNow)) {
    bleDisconnectEvent = false;
    DEBUG_PRINTLN("BLE disconnected.");
    BLE_connected = false;
    resetKeyReportState();
    if (!ledFlash.active) applySteadyLED();
  }
  if (bleConnectEvent || (!BLE_connected && connectedNow)) {
    bleConnectEvent = false;
    DEBUG_PRINTLN("BLE connected.");
    BLE_connected = true;
    resetKeyReportState(); // no forced report: host enables HID notifications seconds later
    if (!ledFlash.active) applySteadyLED();
  }
}

void setupBLE() {
  NimBLEDevice::init(BLE_DEVICE_NAME);
  NimBLEDevice::setPower(BLE_TX_POWER);
  NimBLEDevice::setSecurityAuth(true, false, true); // bonding, no MITM, LE secure connections
  NimBLEDevice::setSecurityIOCap(BLE_HS_IO_NO_INPUT_OUTPUT);
  bleServer = NimBLEDevice::createServer();
  bleServer->setCallbacks(new ServerCallbacks());
  bleServer->advertiseOnDisconnect(true);

  blehid = new NimBLEHIDDevice(bleServer);
  blehid->setManufacturer(BLE_MANUFACTURER);
  blehid->setPnp(0x02, 0x303A, 0x4001, 0x0210);
  blehid->setHidInfo(0x00, 0x01);
  blehid->setReportMap((uint8_t *)HID_REPORT_DESCRIPTOR, sizeof(HID_REPORT_DESCRIPTOR));
  keyboardInput = blehid->getInputReport(KEYBOARD_REPORT_ID);
  consumerInput = blehid->getInputReport(CONSUMER_REPORT_ID);
  blehid->setBatteryLevel(100);

  NimBLEAdvertising *adv = NimBLEDevice::getAdvertising();
  adv->setAppearance(HID_KEYBOARD);
  adv->addServiceUUID(blehid->getHidService()->getUUID());
  adv->enableScanResponse(true);
  adv->setPreferredParams(BLE_CONN_INTERVAL_MIN, BLE_CONN_INTERVAL_MAX);
  NimBLEAdvertisementData scanResponse;
  scanResponse.setName(BLE_DEVICE_NAME);
  adv->setScanResponseData(scanResponse);
  bool started = NimBLEDevice::startAdvertising();
  DEBUG_PRINTLN(started ? "BLE advertising started." : "BLE advertising failed.");
}

/*------------------------------ Setup -------------------------------*/
void setup() {
  applyDefaultSettings();
  for (uint8_t i = 0; i < N_BUTTONS; i++) pinMode(buttons[i].pin, buttons[i].activeLow ? INPUT_PULLUP : INPUT_PULLDOWN);
  pinMode(PIN_RGB_LED_RED, OUTPUT);
  pinMode(PIN_RGB_LED_BLUE, OUTPUT);
  pinMode(PIN_RGB_LED_GREEN, OUTPUT);
  setRGBColor(POWER_ON_COLOR);

  if (DEBUG) {
    Serial.begin(115200);
    unsigned long t0 = millis();
    while (!Serial && millis() - t0 < 2000) delay(10);
    Serial.println("MotoButtons 2 BLE Controller");
  }

  setupBLE();
  setRGBColor(SETUP_COMPLETE_COLOR);

  int buttonMap = getButtonMapSelection();
  bool readError = readSettings();
  if (buttonMap >= 0) { // startup selection overrides the stored orientation
    buttonOrientation = (uint8_t)buttonMap;
    setButtonMapping(buttonOrientation);
    DEBUG_PRINT("Button orientation changed to: "); DEBUG_PRINTLN(buttonOrientation);
  }
  if (readError || buttonMap >= 0) writeSettings();

  if (buttonMap >= 0) startFlash(BUTTON_ORIENTATION_COLOR, buttonOrientation + 1, 250);
  indicateMode(currentMode);
}

void loop() {
#if DEBUG
  handleSerialTuning();
#endif
  handleBLEEvents();
  updateButtons();
  if (BLE_connected) handleKeyReports();
  updateLED();
  delay(LOOP_DELAY_MS);
}
