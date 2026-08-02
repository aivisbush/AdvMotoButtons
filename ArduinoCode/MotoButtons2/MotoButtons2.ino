/*********************************************************************
License: GNU GENERAL PUBLIC LICENSE; Version 3, 29 June 2007
Version: 2.0 with support for the following modes: DMD2, OsmAnd, media (music)
Device: Seeed XIAO ESP32C3 (MotoButtons 2)
*********************************************************************/
#include <Arduino.h>
#include <NimBLEDevice.h>
#include <NimBLEHIDDevice.h>
#include <Preferences.h>
#include <esp_system.h>

// Enable serial debugging (turn this off if not connected to PC)
#define DEBUG true

// How long factory-reset and software-restart chords must be held
#define MODE_RESET_MS 5000

// Orientation of controller
#define DEFAULT_BUTTON_MAP 3
#define STARTUP_ORIENTATION_WINDOW_MS 500
uint8_t buttonOrientation = DEFAULT_BUTTON_MAP;

/*----- Persistent Settings -----*/
const char SETTINGS_NAMESPACE[] = "motobuttons";
const char SETTINGS_MODE_KEY[] = "mode";
const char SETTINGS_ORIENTATION_KEY[] = "orientation";
const char SETTINGS_BRIGHTNESS_KEY[] = "brightness";

// BLE configuration
#define BLE_TX_POWER 9
const char BLE_DEVICE_NAME[] = "Bush Moto BT14";
const char BLE_DEVICE_MODEL[] = "Btns v2.0";
const char BLE_MANUFACTURER[] = "Bush";
volatile bool BLE_connected = false;

NimBLEHIDDevice *blehid = nullptr;
NimBLECharacteristic *keyboardInput = nullptr;
NimBLECharacteristic *consumerInput = nullptr;
NimBLEServer *bleServer = nullptr;

const uint8_t KEYBOARD_REPORT_ID = 1;
const uint8_t CONSUMER_REPORT_ID = 2;

// Separate keyboard and consumer-control input reports.
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

// RGB LED colors plus off
typedef enum
{
  Red,
  Blue,    // BLE connected (flashing, BLE not connected)
  Green,   // OsmAnd mode
  Magenta, // media mode
  White,   // regular key press
  Off,
} Color;

Color priorLEDState = Off;
Color LEDState = Off;

#define BLE_COLOR Blue
#define DMD2_MODE_COLOR Blue
#define OSMAND_MODE_COLOR Green
#define MEDIA_MODE_COLOR Magenta
#define KEY_PRESS_COLOR White
#define POWER_ON_COLOR Red
#define SETUP_COMPLETE_COLOR White
#define BUTTON_ORIENTATION_COLOR Red

/*
 * --------------------- MODE CONFIGURATION ----------------------------
 * 	DMD2: up/down/left/right arrows, enter, F6 and F7
 * 	OsmAnd: up/down/left/right arrows, unbound center, '+', '-', 'c'
 *  Media (music): vol up, vol down, previous track, next track, mute, play/pause, stop
 */
// Selected Mode: indicates the currently selected operating mode
#define MODE_TOGGLE_MS 1000
#define N_MODES 3
typedef enum
{
  DMD2 = 1,
  OsmAnd = 2,
  MEDIA = 3
} Mode;
#define DEFAULT_MODE DMD2
Mode currentMode;

bool modeButtonsReleased = true;
bool modeComboConsumed = false;
bool formatComboConsumed = false;
bool restartComboConsumed = false;
bool brightnessSettingsDirty = false;

/* DMD2 Mode Configuration */
// USB HID usage IDs used in keyboard reports.
const uint8_t HID_KEY_NONE = 0x00;
const uint8_t HID_KEY_C = 0x06;
const uint8_t HID_KEY_ENTER = 0x28;
const uint8_t HID_KEY_MINUS = 0x2D;
const uint8_t HID_KEY_EQUAL = 0x2E;
const uint8_t HID_KEY_F6 = 0x3F;
const uint8_t HID_KEY_F7 = 0x40;
const uint8_t HID_KEY_F8 = 0x41;
const uint8_t HID_KEY_ARROW_RIGHT = 0x4F;
const uint8_t HID_KEY_ARROW_LEFT = 0x50;
const uint8_t HID_KEY_ARROW_DOWN = 0x51;
const uint8_t HID_KEY_ARROW_UP = 0x52;

// USB HID consumer-page usages used in consumer-control reports.
const uint16_t HID_USAGE_CONSUMER_BRIGHTNESS_INCREMENT = 0x006F;
const uint16_t HID_USAGE_CONSUMER_BRIGHTNESS_DECREMENT = 0x0070;
const uint16_t HID_USAGE_CONSUMER_SCAN_NEXT = 0x00B5;
const uint16_t HID_USAGE_CONSUMER_SCAN_PREVIOUS = 0x00B6;
const uint16_t HID_USAGE_CONSUMER_PLAY_PAUSE = 0x00CD;
const uint16_t HID_USAGE_CONSUMER_MUTE = 0x00E2;
const uint16_t HID_USAGE_CONSUMER_VOLUME_INCREMENT = 0x00E9;
const uint16_t HID_USAGE_CONSUMER_VOLUME_DECREMENT = 0x00EA;

// Key codes to send for button presses:
const uint8_t DMD_KEY_UP = HID_KEY_ARROW_UP;
const uint8_t DMD_KEY_DOWN = HID_KEY_ARROW_DOWN;
const uint8_t DMD_KEY_LEFT = HID_KEY_ARROW_LEFT;
const uint8_t DMD_KEY_RIGHT = HID_KEY_ARROW_RIGHT;
const uint8_t DMD_KEY_CENTER = HID_KEY_F8;
const uint8_t DMD_KEY_A = HID_KEY_F6;
const uint8_t DMD_KEY_B = HID_KEY_F7;
const uint8_t DMD_KEY_C = HID_KEY_ENTER;

/* OsmAnd Mode Configuration */
const uint8_t OSMAND_KEY_UP = HID_KEY_ARROW_UP;
const uint8_t OSMAND_KEY_DOWN = HID_KEY_ARROW_DOWN;
const uint8_t OSMAND_KEY_LEFT = HID_KEY_ARROW_LEFT;
const uint8_t OSMAND_KEY_RIGHT = HID_KEY_ARROW_RIGHT;
const uint8_t OSMAND_KEY_CENTER = HID_KEY_NONE;       // unbound
const uint8_t OSMAND_KEY_A = HID_KEY_EQUAL;           // zoom in (+ key)
const uint8_t OSMAND_KEY_B = HID_KEY_MINUS;           // zoom out
const uint8_t OSMAND_KEY_C = HID_KEY_C;               // move to my location

/* Media Mode Configuration */
const uint16_t MEDIA_KEY_UP = HID_USAGE_CONSUMER_VOLUME_INCREMENT;    // volume up
const uint16_t MEDIA_KEY_DOWN = HID_USAGE_CONSUMER_VOLUME_DECREMENT;  // volume down
const uint16_t MEDIA_KEY_LEFT = HID_USAGE_CONSUMER_SCAN_PREVIOUS;     // previous song
const uint16_t MEDIA_KEY_RIGHT = HID_USAGE_CONSUMER_SCAN_NEXT;        // next song
const uint16_t MEDIA_KEY_CENTER = HID_USAGE_CONSUMER_MUTE;            // mute
const uint16_t MEDIA_KEY_A = HID_USAGE_CONSUMER_PLAY_PAUSE;           // play / pause
const uint16_t MEDIA_KEY_B = HID_USAGE_CONSUMER_BRIGHTNESS_INCREMENT; // increase brightness
const uint16_t MEDIA_KEY_C = HID_USAGE_CONSUMER_BRIGHTNESS_DECREMENT; // decrease brightness
/*---------------------- END MODE CONFIGURATION ----------------------*/

/*----------------- BUTTON CONFIGURATION AND LOGIC -------------------*/
/* BLE key report */
#define N_KEY_REPORT 6
uint8_t keyReport[N_KEY_REPORT] = {HID_KEY_NONE, HID_KEY_NONE, HID_KEY_NONE, HID_KEY_NONE, HID_KEY_NONE, HID_KEY_NONE};
bool keyReportChanged = false;
bool forceKeyReport = false; // used to force another key report for key up activation events

// Seeed XIAO ESP32C3 pin mapping, matching the existing MotoButtons wiring.
// Note: GPIO2/D0, GPIO8/D8, and GPIO9/D9 are ESP32-C3 boot strapping pins.
// This wiring uses D8/D9 for buttons, so avoid holding DOWN or CENTER while
// powering on or entering upload mode.
const uint8_t PIN_JOYSTICK_UP = D3;
const uint8_t PIN_JOYSTICK_DOWN = D8;
const uint8_t PIN_JOYSTICK_LEFT = D4;
const uint8_t PIN_JOYSTICK_RIGHT = D10;
const uint8_t PIN_BUTTON_CENTER = D9;
const uint8_t PIN_BUTTON_A = D5;
const uint8_t PIN_BUTTON_B = D2;
const uint8_t PIN_BUTTON_C = D1;

uint8_t BUTTON_UP = PIN_JOYSTICK_UP;
uint8_t BUTTON_DOWN = PIN_JOYSTICK_DOWN;
uint8_t BUTTON_LEFT = PIN_JOYSTICK_LEFT;
uint8_t BUTTON_RIGHT = PIN_JOYSTICK_RIGHT;
uint8_t BUTTON_CENTER = PIN_BUTTON_CENTER;
uint8_t BUTTON_A = PIN_BUTTON_A;
uint8_t BUTTON_B = PIN_BUTTON_B;
uint8_t BUTTON_C = PIN_BUTTON_C;
const bool BUTTON_UP_ACTIVE_LOW = false;
const bool BUTTON_DOWN_ACTIVE_LOW = false;
const bool BUTTON_LEFT_ACTIVE_LOW = false;
const bool BUTTON_RIGHT_ACTIVE_LOW = false;
const bool BUTTON_CENTER_ACTIVE_LOW = true;
const bool BUTTON_A_ACTIVE_LOW = false;
const bool BUTTON_B_ACTIVE_LOW = false;
const bool BUTTON_C_ACTIVE_LOW = false;
uint8_t RGB_LED_RED = D0;
uint8_t RGB_LED_GREEN = D7;
uint8_t RGB_LED_BLUE = D6;
#define USER_LED_ENABLED false
#define USER_LED_PIN 255
#define USER_LED_ACTIVE_LOW true

// Raw joystick GPIOs used for startup orientation selection.
const uint8_t JOYSTICK_PIN_UP = PIN_JOYSTICK_UP;
const uint8_t JOYSTICK_PIN_DOWN = PIN_JOYSTICK_DOWN;
const uint8_t JOYSTICK_PIN_LEFT = PIN_JOYSTICK_LEFT;
const uint8_t JOYSTICK_PIN_RIGHT = PIN_JOYSTICK_RIGHT;

#define DEBOUNCE_TIME_MS 120
#define DIRECTION_REPEAT_INTERVAL_MS 100
#define ABC_REPEAT_INTERVAL_MS 250
#define DMD_ABC_REPEAT_INTERVAL_MS 100
// state of buttons
bool button_up_state = false;
bool button_down_state = false;
bool button_left_state = false;
bool button_right_state = false;
bool button_center_state = false;
bool button_A_state = false;
bool button_B_state = false;
bool button_C_state = false;
// prior state of button reading for debouncing purposes
bool button_up_state_prior = false;
bool button_down_state_prior = false;
bool button_left_state_prior = false;
bool button_right_state_prior = false;
bool button_center_state_prior = false;
bool button_A_state_prior = false;
bool button_B_state_prior = false;
bool button_C_state_prior = false;

// Record state change of buttons to be used to monitor when event is transmitted
bool button_up_flipped = false;
bool button_down_flipped = false;
bool button_left_flipped = false;
bool button_right_flipped = false;
bool button_center_flipped = false;
bool button_A_flipped = false;
bool button_B_flipped = false;
bool button_C_flipped = false;
// last time that button transitioned from low to high
unsigned long button_up_time = 0;
unsigned long button_down_time = 0;
unsigned long button_left_time = 0;
unsigned long button_right_time = 0;
unsigned long button_center_time = 0;
unsigned long button_A_time = 0;
unsigned long button_B_time = 0;
unsigned long button_C_time = 0;
// LED brightness 0 - 255 (100% - 0%)
int LEDbrightness = 0;
unsigned long brightnessAdjustTime = 0;
bool brightnessComboConsumed = false;
unsigned long lastRepeatTime = 0;
/*------------------- END BUTTON CONFIG & LOGIC-----------------------*/
void setUserLED(bool on)
{
  if (!USER_LED_ENABLED)
    return;

  digitalWrite(USER_LED_PIN, (USER_LED_ACTIVE_LOW ? !on : on) ? HIGH : LOW);
}

bool readButtonPin(uint8_t pin, bool activeLow)
{
  bool reading = digitalRead(pin);
  return activeLow ? !reading : reading;
}

/*
  Set the color of the RGB LED to one of the 7 possibilities, plus off
  For a common anode(+) LED, LOW is ON and HIGH is OFF.
*/
void setRGBColor(Color color)
{
  priorLEDState = LEDState;

  // convert a "normal" 0..255 channel intensity into the common-anode
  // analogWrite value, taking LEDbrightness (0..255, 0==full on, 255==off) into account.
  auto rgbAnalog = [](uint8_t channel)->uint8_t {
    // brightnessPercent = (255 - LEDbrightness)/255
    // analog = 255 - channel * brightnessPercent
    return (uint8_t)(255 - (((uint16_t)channel * (255 - LEDbrightness) + 127) / 255));
  };

  switch (color)
  {
  case Red:
    LEDState = Red;
    analogWrite(RGB_LED_RED, rgbAnalog(255));
    analogWrite(RGB_LED_BLUE, rgbAnalog(0));
    analogWrite(RGB_LED_GREEN, rgbAnalog(0));
    break;
  case Blue:
    LEDState = Blue;
    analogWrite(RGB_LED_RED, rgbAnalog(0));
    analogWrite(RGB_LED_BLUE, rgbAnalog(255));
    analogWrite(RGB_LED_GREEN, rgbAnalog(0));
    break;
  case Green:
    LEDState = Green;
    analogWrite(RGB_LED_RED, rgbAnalog(0));
    analogWrite(RGB_LED_BLUE, rgbAnalog(0));
    analogWrite(RGB_LED_GREEN, rgbAnalog(255));
    break;
  case Magenta:
    LEDState = Magenta;
    // keep existing slight-dim behavior (~245) for magenta
    analogWrite(RGB_LED_RED, rgbAnalog(245));
    analogWrite(RGB_LED_BLUE, rgbAnalog(245));
    analogWrite(RGB_LED_GREEN, rgbAnalog(0));
    break;
  case White:
    LEDState = White;
    // keep existing slight-dim behavior (~245) for white
    analogWrite(RGB_LED_RED, rgbAnalog(245));
    analogWrite(RGB_LED_BLUE, rgbAnalog(245));
    analogWrite(RGB_LED_GREEN, rgbAnalog(245));
    break;
  case Off:
  default:
    LEDState = Off;
    analogWrite(RGB_LED_RED, rgbAnalog(0));
    analogWrite(RGB_LED_BLUE, rgbAnalog(0));
    analogWrite(RGB_LED_GREEN, rgbAnalog(0));
  }
}

void flashLED(Color color, uint16_t delayMs, uint16_t durationMs)
{
  uint8_t N = durationMs / delayMs;

  priorLEDState = LEDState;
  LEDState = color;

  for (uint8_t i = 0; i < 2 * (N + 1); i++)
  {
    if (i % 2 == 0)
      setRGBColor(Off);
    else
      setRGBColor(color);
    delay(delayMs / 2);
  }
}

void restoreLEDState()
{
  setRGBColor(priorLEDState);
  LEDState = priorLEDState;
}

void indicateMode(Mode mode)
{
  // Indicate the new mode
  switch (mode)
  {
  case DMD2:
    flashLED(DMD2_MODE_COLOR, 1000, 200);
    setRGBColor(DMD2_MODE_COLOR);
    break;
  case OsmAnd:
    flashLED(OSMAND_MODE_COLOR, 1000, 200);
    setRGBColor(OSMAND_MODE_COLOR);
    break;
  case MEDIA:
    flashLED(MEDIA_MODE_COLOR, 1000, 200);
    setRGBColor(MEDIA_MODE_COLOR);
    break;
  default:
    setRGBColor(Red);
  }
}

void showMode(Mode mode)
{
  // Indicate the new mode
  switch (mode)
  {
  case DMD2:
    setRGBColor(DMD2_MODE_COLOR);
    break;
  case OsmAnd:
    setRGBColor(OSMAND_MODE_COLOR);
    break;
  case MEDIA:
    setRGBColor(MEDIA_MODE_COLOR);
    break;
  default:
    setRGBColor(Red);
  }
}

void RGBToggle(Color color)
{
  // Treat LED as "on" when a color (not Off) is active.
  if (LEDState == Off)
    setRGBColor(color);
  else
    setRGBColor(Off);
}

// cycle through all colors of the LED for demo purposes
void colorCycle(uint16_t N)
{
  const uint8_t COLOR_COUNT = 6;
  for (uint32_t i = 0; i < N * COLOR_COUNT; i++)
  {
    setRGBColor((Color)(i % COLOR_COUNT));
    delay(500);
  }
}

// At startup, the user can hold down a joystick direction to select an orientation
// -1 indicates no valid selection was made
int getButtonMapSelection()
{
  unsigned long startMs = millis();
  int detectedSelection = -1;

  while (millis() - startMs < STARTUP_ORIENTATION_WINDOW_MS)
  {
    // Read all four directions because we can only allow a mode switch if
    // one direction is pressed
    uint8_t up = readButtonPin(JOYSTICK_PIN_UP, BUTTON_UP_ACTIVE_LOW);
    uint8_t down = readButtonPin(JOYSTICK_PIN_DOWN, BUTTON_DOWN_ACTIVE_LOW);
    uint8_t left = readButtonPin(JOYSTICK_PIN_LEFT, BUTTON_LEFT_ACTIVE_LOW);
    uint8_t right = readButtonPin(JOYSTICK_PIN_RIGHT, BUTTON_RIGHT_ACTIVE_LOW);

    if (up + down + left + right > 1)
      return -1;

    if (up)
      detectedSelection = 2;
    else if (down)
      detectedSelection = 0;
    else if (left)
      detectedSelection = 1;
    else if (right)
      detectedSelection = 3;

    if (detectedSelection >= 0)
      return detectedSelection;

    delay(10);
  }

  // no selection was made during the startup window
  return -1;
}

/* Change the logical joystick mapping based on a map specifier, buttMap.
 * This lets the controller be mounted in four orientations without rewiring.
 */
bool setButtonMapping(uint8_t buttMap)
{
  switch (buttMap)
  {
  case 0: // three buttons on top
    BUTTON_UP = PIN_JOYSTICK_RIGHT;
    BUTTON_DOWN = PIN_JOYSTICK_LEFT;
    BUTTON_LEFT = PIN_JOYSTICK_UP;
    BUTTON_RIGHT = PIN_JOYSTICK_DOWN;
    BUTTON_CENTER = PIN_BUTTON_CENTER;
    BUTTON_A = PIN_BUTTON_A;
    BUTTON_B = PIN_BUTTON_B;
    BUTTON_C = PIN_BUTTON_C;
    break;
  case 1: // three buttons on left
    BUTTON_UP = PIN_JOYSTICK_DOWN;
    BUTTON_DOWN = PIN_JOYSTICK_UP;
    BUTTON_LEFT = PIN_JOYSTICK_RIGHT;
    BUTTON_RIGHT = PIN_JOYSTICK_LEFT;
    BUTTON_CENTER = PIN_BUTTON_CENTER;
    BUTTON_A = PIN_BUTTON_A;
    BUTTON_B = PIN_BUTTON_B;
    BUTTON_C = PIN_BUTTON_C;
    break;
  case 2: // three buttons on bottom
    BUTTON_UP = PIN_JOYSTICK_LEFT;
    BUTTON_DOWN = PIN_JOYSTICK_RIGHT;
    BUTTON_LEFT = PIN_JOYSTICK_DOWN;
    BUTTON_RIGHT = PIN_JOYSTICK_UP;
    BUTTON_CENTER = PIN_BUTTON_CENTER;
    BUTTON_A = PIN_BUTTON_A;
    BUTTON_B = PIN_BUTTON_B;
    BUTTON_C = PIN_BUTTON_C;
    break;
  case 3: // three buttons toward right
    BUTTON_UP = PIN_JOYSTICK_UP;
    BUTTON_DOWN = PIN_JOYSTICK_DOWN;
    BUTTON_LEFT = PIN_JOYSTICK_LEFT;
    BUTTON_RIGHT = PIN_JOYSTICK_RIGHT;
    BUTTON_CENTER = PIN_BUTTON_CENTER;
    BUTTON_A = PIN_BUTTON_A;
    BUTTON_B = PIN_BUTTON_B;
    BUTTON_C = PIN_BUTTON_C;
    break;
  default:
    return true;
  }

  return false;
}

// This function returns true if the center button is in an active state
// if so, the program should ignore up/down/left/right on the joystick
bool isCenterActive()
{
  return button_center_state;
}

// if  This function should be called rapidly in a loop to update the debounce filter and key state
//  https://docs.arduino.cc/built-in-examples/digital/Debounce
bool debounceButton(unsigned int button, bool activeLow, bool *state, bool *priorState, bool *buttonFlipped,
                    unsigned long *debounceTime, const char *buttonName)
{
  bool reading;
  unsigned long readTime;
  bool stateChanged = false;

  reading = readButtonPin(button, activeLow);
  readTime = millis();

  // If reading has changed, switch has not settled yet
  if (reading != *priorState)
    *debounceTime = readTime;
  // State has been stable for time exceeding debounce filter delay, so update state
  else if ((readTime - *debounceTime) > DEBOUNCE_TIME_MS)
    // If state changed after debounce, new state has not yet been transmitted
    if (reading != *state)
    {
      stateChanged = true;
      *state = reading;
      *buttonFlipped = true;
      if (DEBUG)
      {
        Serial.print("Button ");
        Serial.print(buttonName);
        Serial.println(reading ? " pressed" : " released");
      }
    }
  *priorState = reading;

  return stateChanged;
}

void initializeButtonState(uint8_t button, bool activeLow, bool *state, bool *priorState, bool *buttonFlipped,
                           unsigned long *debounceTime)
{
  bool reading = readButtonPin(button, activeLow);
  *state = reading;
  *priorState = reading;
  *buttonFlipped = false;
  *debounceTime = millis();
}

void clearKeyReport()
{
  memset(keyReport, HID_KEY_NONE, sizeof(keyReport));
}

void sendKeyboardReport()
{
  if (!BLE_connected || keyboardInput == nullptr)
    return;

  uint8_t report[8] = {0, 0, HID_KEY_NONE, HID_KEY_NONE, HID_KEY_NONE, HID_KEY_NONE, HID_KEY_NONE, HID_KEY_NONE};
  memcpy(&report[2], keyReport, sizeof(keyReport));
  keyboardInput->setValue(report, sizeof(report));
  keyboardInput->notify();
}

void sendConsumerKey(uint16_t usage)
{
  if (!BLE_connected || consumerInput == nullptr)
    return;

  uint8_t report[2] = {(uint8_t)(usage & 0xFF), (uint8_t)(usage >> 8)};
  consumerInput->setValue(report, sizeof(report));
  consumerInput->notify();
  delay(5);

  const uint8_t releaseReport[2] = {0, 0};
  consumerInput->setValue(releaseReport, sizeof(releaseReport));
  consumerInput->notify();
}

void releaseAllKeys()
{
  clearKeyReport();
  sendKeyboardReport();
}

void applyDefaultSettings()
{
  currentMode = DEFAULT_MODE;
  buttonOrientation = DEFAULT_BUTTON_MAP;
  LEDbrightness = 0;
  setButtonMapping(buttonOrientation);
}

void clearABCButtonFlips()
{
  button_A_flipped = false;
  button_B_flipped = false;
  button_C_flipped = false;
}

Mode getNextMode(Mode mode)
{
  switch (mode)
  {
  case DMD2:
    return OsmAnd;
  case OsmAnd:
    return MEDIA;
  case MEDIA:
  default:
    return DMD2;
  }
}

bool clearSettings()
{
  Preferences preferences;
  if (!preferences.begin(SETTINGS_NAMESPACE, false))
    return false;

  bool cleared = preferences.clear();
  preferences.end();
  return cleared;
}

bool clearBLEBonds()
{
  return NimBLEDevice::deleteAllBonds();
}

bool handleFormatCombo()
{
  bool formatButtonsPressed = button_A_state && button_B_state && button_C_state;
  if (!formatButtonsPressed)
    return false;

  // Factory reset takes priority over all smaller button combos.
  if (!formatComboConsumed)
    releaseAllKeys();
  formatComboConsumed = true;
  clearABCButtonFlips();
  modeButtonsReleased = false;
  modeComboConsumed = false;
  brightnessAdjustTime = 0;
  brightnessComboConsumed = false;
  brightnessSettingsDirty = false;
  keyReportChanged = false;
  forceKeyReport = false;

  if ((millis() - button_A_time > MODE_RESET_MS) &&
      (millis() - button_B_time > MODE_RESET_MS) &&
      (millis() - button_C_time > MODE_RESET_MS))
  {
    if (DEBUG)
      Serial.println("Clearing settings and BLE bonds...");
    bool settingsCleared = clearSettings();
    bool bondsCleared = clearBLEBonds();
    if (DEBUG)
    {
      Serial.print("Settings cleared: ");
      Serial.println(settingsCleared);
      Serial.print("BLE bonds cleared: ");
      Serial.println(bondsCleared);
    }
    flashLED(Red, 500, 2000);
    delay(100);
    ESP.restart();
  }
  return true;
}

bool handleConsumedFormatCombo()
{
  if (!formatComboConsumed)
    return false;

  clearABCButtonFlips();
  keyReportChanged = false;
  forceKeyReport = false;
  if (!button_A_state && !button_B_state && !button_C_state)
    formatComboConsumed = false;
  return true;
}

bool handleRestartCombo()
{
  bool restartButtonsPressed = button_A_state && button_C_state && !button_B_state;
  if (restartButtonsPressed)
  {
    if (!restartComboConsumed)
      releaseAllKeys();
    restartComboConsumed = true;
    clearABCButtonFlips();
    keyReportChanged = false;
    forceKeyReport = false;

    if ((millis() - button_A_time > MODE_RESET_MS) &&
        (millis() - button_C_time > MODE_RESET_MS))
    {
      if (DEBUG)
        Serial.println("Restarting controller...");
      delay(20);
      ESP.restart();
    }
    return true;
  }

  if (!restartComboConsumed)
    return false;

  clearABCButtonFlips();
  keyReportChanged = false;
  forceKeyReport = false;
  if (!button_A_state && !button_C_state)
    restartComboConsumed = false;
  return true;
}

void handleModeCycleCombo()
{
  if (!button_B_state && !button_C_state)
    modeButtonsReleased = true;

  if (button_B_state && button_C_state && !button_A_state &&
      (millis() - max(button_B_time, button_C_time) > MODE_TOGGLE_MS) &&
      modeButtonsReleased)
  {
    currentMode = getNextMode(currentMode);
    if (DEBUG)
    {
      Serial.print("Mode advanced to ");
      Serial.println(currentMode);
    }

    releaseAllKeys();
    modeButtonsReleased = false;
    modeComboConsumed = true;
    button_B_flipped = false;
    button_C_flipped = false;
    writeSettings();
    indicateMode(currentMode);
  }
  else if (modeComboConsumed)
  {
    button_B_flipped = false;
    button_C_flipped = false;
    if (!button_B_state && !button_C_state)
      modeComboConsumed = false;
  }
}

void handleBrightnessCombo()
{
  if (button_A_state && button_B_state)
  {
    unsigned long brightnessHoldMs = millis() - max(button_A_time, button_B_time);
    if (brightnessHoldMs > MODE_TOGGLE_MS && (brightnessAdjustTime == 0 || millis() - brightnessAdjustTime >= 200))
    {
      LEDbrightness = LEDbrightness - 20;
      if (LEDbrightness < 0)
        LEDbrightness = 255;
      brightnessAdjustTime = millis();
      if (!brightnessComboConsumed)
        releaseAllKeys();
      brightnessComboConsumed = true;
      brightnessSettingsDirty = true;
      button_A_flipped = false;
      button_B_flipped = false;

      setRGBColor(LEDState);
      if (DEBUG)
      {
        Serial.print("LED brightness changed to ");
        Serial.println(LEDbrightness);
      }
    }
  }
  else
  {
    brightnessAdjustTime = 0;
    if (brightnessComboConsumed)
    {
      button_A_flipped = false;
      button_B_flipped = false;
      if (!button_A_state && !button_B_state)
      {
        brightnessComboConsumed = false;
        if (brightnessSettingsDirty)
        {
          writeSettings();
          brightnessSettingsDirty = false;
        }
      }
    }
  }
}

uint16_t getRepeatInterval()
{
  bool centerActive = isCenterActive();

  switch (currentMode)
  {
  case DMD2:
    if (button_up_state || button_down_state || button_left_state || button_right_state)
      return DIRECTION_REPEAT_INTERVAL_MS;

    if ((button_A_state && !button_B_state && !button_C_state) ||
        (button_B_state && !button_A_state && !button_C_state))
      return DMD_ABC_REPEAT_INTERVAL_MS;
    return 0;

  case OsmAnd:
    if (!centerActive &&
        (button_up_state || button_down_state || button_left_state || button_right_state))
      return DIRECTION_REPEAT_INTERVAL_MS;

    if ((button_A_state && !button_B_state && !button_C_state) ||
        (button_B_state && !button_A_state && !button_C_state))
      return ABC_REPEAT_INTERVAL_MS;
    return 0;

  default:
    return 0;
  }
}

void updateButtons()
{
  bool stateChanged = false;

  // Read the state of all physical buttons
  stateChanged |= debounceButton(BUTTON_UP, BUTTON_UP_ACTIVE_LOW, &button_up_state, &button_up_state_prior, &button_up_flipped, &button_up_time, "UP");
  stateChanged |= debounceButton(BUTTON_DOWN, BUTTON_DOWN_ACTIVE_LOW, &button_down_state, &button_down_state_prior, &button_down_flipped, &button_down_time, "DOWN");
  stateChanged |= debounceButton(BUTTON_LEFT, BUTTON_LEFT_ACTIVE_LOW, &button_left_state, &button_left_state_prior, &button_left_flipped, &button_left_time, "LEFT");
  stateChanged |= debounceButton(BUTTON_RIGHT, BUTTON_RIGHT_ACTIVE_LOW, &button_right_state, &button_right_state_prior, &button_right_flipped, &button_right_time, "RIGHT");
  stateChanged |= debounceButton(BUTTON_CENTER, BUTTON_CENTER_ACTIVE_LOW, &button_center_state, &button_center_state_prior, &button_center_flipped, &button_center_time, "CENTER");
  stateChanged |= debounceButton(BUTTON_A, BUTTON_A_ACTIVE_LOW, &button_A_state, &button_A_state_prior, &button_A_flipped, &button_A_time, "A");
  stateChanged |= debounceButton(BUTTON_B, BUTTON_B_ACTIVE_LOW, &button_B_state, &button_B_state_prior, &button_B_flipped, &button_B_time, "B");
  stateChanged |= debounceButton(BUTTON_C, BUTTON_C_ACTIVE_LOW, &button_C_state, &button_C_state_prior, &button_C_flipped, &button_C_time, "C");

  if (handleFormatCombo())
    return;

  if (handleConsumedFormatCombo())
    return;

  if (handleRestartCombo())
    return;

  // Indicate whether any buttons changed state
  keyReportChanged = stateChanged;

  /*------------------- Handle mode cycling --------------------------*/
  handleModeCycleCombo();
  /*------------------------------------------------------------------*/

  /*------------------- Changing LED brightness --------------------------*/
  handleBrightnessCombo();
  /*------------------------------------------------------------------*/

  // Once a chord action has fired, suppress its component buttons until
  // every button in the chord has been released.
  if (modeComboConsumed || brightnessComboConsumed)
  {
    keyReportChanged = false;
    forceKeyReport = false;
    clearABCButtonFlips();
  }
}

void mapButtonsToKeyReport()
{
  unsigned int i = 0;
  clearKeyReport();

  bool centerActive = isCenterActive();
  switch (currentMode)
  {
  case DMD2:
    if (button_up_state)
    {
      if (DEBUG)
        Serial.println("DMD2 UP");
      keyReport[i] = DMD_KEY_UP;
      ++i;
    }
    if (button_down_state)
    {
      if (DEBUG)
        Serial.println("DMD2 DOWN");
      keyReport[i] = DMD_KEY_DOWN;
      ++i;
    }
    if (button_left_state)
    {
      if (DEBUG)
        Serial.println("DMD2 LEFT");
      keyReport[i] = DMD_KEY_LEFT;
      ++i;
    }
    if (button_right_state)
    {
      if (DEBUG)
        Serial.println("DMD2 RIGHT");
      keyReport[i] = DMD_KEY_RIGHT;
      ++i;
    }
    if (!button_center_state && button_center_flipped)
    {
      button_center_flipped = false;
      forceKeyReport = true;
      keyReport[i] = DMD_KEY_CENTER;
      ++i;
    }
    if (button_A_state && !button_B_state && !button_C_state)
    {
      button_A_flipped = false;
      keyReport[i] = DMD_KEY_A;
      ++i;
    }
    else if (!button_A_state && button_A_flipped)
      button_A_flipped = false;

    if (button_B_state && !button_A_state && !button_C_state && (i < N_KEY_REPORT))
    {
      button_B_flipped = false;
      keyReport[i] = DMD_KEY_B;
      ++i;
    }
    else if (!button_B_state && button_B_flipped)
      button_B_flipped = false;

    if (button_C_state && button_C_flipped && !button_A_state && !button_B_state && (i < N_KEY_REPORT))
    {
      button_C_flipped = false;
      keyReport[i] = DMD_KEY_C;
      ++i;
    }
    else if (!button_C_state && button_C_flipped)
      button_C_flipped = false;
    break;

  case OsmAnd:
    if (button_up_state && !centerActive)
    {
      if (DEBUG)
        Serial.println("OsmAnd UP");
      keyReport[i] = OSMAND_KEY_UP;
      ++i;
    }
    if (button_down_state && !centerActive)
    {
      if (DEBUG)
        Serial.println("OsmAnd DOWN");
      keyReport[i] = OSMAND_KEY_DOWN;
      ++i;
    }
    if (button_left_state && !centerActive)
    {
      if (DEBUG)
        Serial.println("OsmAnd LEFT");
      keyReport[i] = OSMAND_KEY_LEFT;
      ++i;
    }
    if (button_right_state && !centerActive)
    {
      if (DEBUG)
        Serial.println("OsmAnd RIGHT");
      keyReport[i] = OSMAND_KEY_RIGHT;
      ++i;
    }
    if (!button_center_state && button_center_flipped)
    {
      button_center_flipped = false;
      if (OSMAND_KEY_CENTER != HID_KEY_NONE)
      {
        if (DEBUG)
          Serial.println("OsmAnd CENTER");
        forceKeyReport = true;
        keyReport[i] = OSMAND_KEY_CENTER;
        ++i;
      }
    }
    if (button_A_state && !button_B_state && !button_C_state)
    {
      if (DEBUG)
        Serial.println("OsmAnd A");
      button_A_flipped = false;
      keyReport[i] = OSMAND_KEY_A;
      ++i;
    }
    else if (!button_A_state && button_A_flipped)
      button_A_flipped = false;

    if (button_B_state && !button_A_state && !button_C_state && (i < N_KEY_REPORT))
    {
      if (DEBUG)
        Serial.println("OsmAnd B");
      button_B_flipped = false;
      keyReport[i] = OSMAND_KEY_B;
      ++i;
    }
    else if (!button_B_state && button_B_flipped)
      button_B_flipped = false;

    if (button_C_state && button_C_flipped && !button_A_state && !button_B_state && (i < N_KEY_REPORT))
    {
      if (DEBUG)
        Serial.println("OsmAnd C");
      button_C_flipped = false;
      keyReport[i] = OSMAND_KEY_C;
      ++i;
    }
    else if (!button_C_state && button_C_flipped)
      button_C_flipped = false;
    break;

  case MEDIA:
    // the media keys must be reported via a different ("consumer") function to work on iOS
    if (button_up_state && !centerActive)
    {
      if (DEBUG)
        Serial.println("Media key UP");
      sendConsumerKey(MEDIA_KEY_UP);
    }
    if (button_down_state && !centerActive)
    {
      if (DEBUG)
        Serial.println("Media key DOWN");
      sendConsumerKey(MEDIA_KEY_DOWN);
    }
    if (button_left_state && !centerActive)
    {
      if (DEBUG)
        Serial.println("Media key LEFT");
      sendConsumerKey(MEDIA_KEY_LEFT);
    }
    if (button_right_state && !centerActive)
    {
      if (DEBUG)
        Serial.println("Media key RIGHT");
      sendConsumerKey(MEDIA_KEY_RIGHT);
    }
    if (!button_center_state && button_center_flipped)
    {
      if (DEBUG)
        Serial.println("Media key CENTER");
      button_center_flipped = false;
      sendConsumerKey(MEDIA_KEY_CENTER);
    }
    if (button_A_state && !button_B_state && !button_C_state)
    {
      if (DEBUG)
        Serial.println("Media key A");
      button_A_flipped = false;
      sendConsumerKey(MEDIA_KEY_A);
    }
    else if (!button_A_state && button_A_flipped)
      button_A_flipped = false;

    if (button_B_state && !button_A_state && !button_C_state)
    {
      if (DEBUG)
        Serial.println("Media key B");
      button_B_flipped = false;
      sendConsumerKey(MEDIA_KEY_B);
    }
    else if (!button_B_state && button_B_flipped)
      button_B_flipped = false;

    if (button_C_state && button_C_flipped && !button_A_state && !button_B_state)
    {
      if (DEBUG)
        Serial.println("Media key C");
      button_C_flipped = false;
      sendConsumerKey(MEDIA_KEY_C);
    }
    else if (!button_C_state && button_C_flipped)
      button_C_flipped = false;
    break;

  default:
    break;
  }

}

void setupDigitalIO()
{
  pinMode(BUTTON_UP, BUTTON_UP_ACTIVE_LOW ? INPUT_PULLUP : INPUT_PULLDOWN);
  pinMode(BUTTON_DOWN, BUTTON_DOWN_ACTIVE_LOW ? INPUT_PULLUP : INPUT_PULLDOWN);
  pinMode(BUTTON_LEFT, BUTTON_LEFT_ACTIVE_LOW ? INPUT_PULLUP : INPUT_PULLDOWN);
  pinMode(BUTTON_RIGHT, BUTTON_RIGHT_ACTIVE_LOW ? INPUT_PULLUP : INPUT_PULLDOWN);
  pinMode(BUTTON_CENTER, BUTTON_CENTER_ACTIVE_LOW ? INPUT_PULLUP : INPUT_PULLDOWN);
  pinMode(BUTTON_A, BUTTON_A_ACTIVE_LOW ? INPUT_PULLUP : INPUT_PULLDOWN);
  pinMode(BUTTON_B, BUTTON_B_ACTIVE_LOW ? INPUT_PULLUP : INPUT_PULLDOWN);
  pinMode(BUTTON_C, BUTTON_C_ACTIVE_LOW ? INPUT_PULLUP : INPUT_PULLDOWN);

  pinMode(RGB_LED_RED, OUTPUT);
  pinMode(RGB_LED_BLUE, OUTPUT);
  pinMode(RGB_LED_GREEN, OUTPUT);

  if (USER_LED_ENABLED)
    pinMode(USER_LED_PIN, OUTPUT);
  setUserLED(false);

  initializeButtonState(BUTTON_UP, BUTTON_UP_ACTIVE_LOW, &button_up_state, &button_up_state_prior, &button_up_flipped, &button_up_time);
  initializeButtonState(BUTTON_DOWN, BUTTON_DOWN_ACTIVE_LOW, &button_down_state, &button_down_state_prior, &button_down_flipped, &button_down_time);
  initializeButtonState(BUTTON_LEFT, BUTTON_LEFT_ACTIVE_LOW, &button_left_state, &button_left_state_prior, &button_left_flipped, &button_left_time);
  initializeButtonState(BUTTON_RIGHT, BUTTON_RIGHT_ACTIVE_LOW, &button_right_state, &button_right_state_prior, &button_right_flipped, &button_right_time);
  initializeButtonState(BUTTON_CENTER, BUTTON_CENTER_ACTIVE_LOW, &button_center_state, &button_center_state_prior, &button_center_flipped, &button_center_time);
  initializeButtonState(BUTTON_A, BUTTON_A_ACTIVE_LOW, &button_A_state, &button_A_state_prior, &button_A_flipped, &button_A_time);
  initializeButtonState(BUTTON_B, BUTTON_B_ACTIVE_LOW, &button_B_state, &button_B_state_prior, &button_B_flipped, &button_B_time);
  initializeButtonState(BUTTON_C, BUTTON_C_ACTIVE_LOW, &button_C_state, &button_C_state_prior, &button_C_flipped, &button_C_time);
}

bool writeSettings()
{
  Preferences preferences;
  if (!preferences.begin(SETTINGS_NAMESPACE, false))
    return false;

  bool success = preferences.putUChar(SETTINGS_MODE_KEY, (uint8_t)currentMode) == sizeof(uint8_t) &&
                 preferences.putUChar(SETTINGS_ORIENTATION_KEY, buttonOrientation) == sizeof(uint8_t) &&
                 preferences.putUChar(SETTINGS_BRIGHTNESS_KEY, (uint8_t)LEDbrightness) == sizeof(uint8_t);
  preferences.end();

  if (DEBUG)
    Serial.println(success ? "Settings saved." : "Failed to save settings.");
  return success;
}

// Returns true only when a complete, valid settings record was loaded.
bool readSettings()
{
  Preferences preferences;
  if (!preferences.begin(SETTINGS_NAMESPACE, true))
    return false;

  bool complete = preferences.isKey(SETTINGS_MODE_KEY) &&
                  preferences.isKey(SETTINGS_ORIENTATION_KEY) &&
                  preferences.isKey(SETTINGS_BRIGHTNESS_KEY);
  uint8_t savedMode = preferences.getUChar(SETTINGS_MODE_KEY, 0);
  uint8_t savedOrientation = preferences.getUChar(SETTINGS_ORIENTATION_KEY, 0xFF);
  uint8_t savedBrightness = preferences.getUChar(SETTINGS_BRIGHTNESS_KEY, 0);
  preferences.end();

  if (!complete || savedMode < DMD2 || savedMode > MEDIA || savedOrientation > 3)
  {
    if (DEBUG)
      Serial.println("Settings missing or invalid; restoring defaults.");
    applyDefaultSettings();
    return false;
  }

  currentMode = (Mode)savedMode;
  buttonOrientation = savedOrientation;
  LEDbrightness = savedBrightness;
  setButtonMapping(buttonOrientation);
  setRGBColor(LEDState);

  if (DEBUG)
  {
    Serial.print("Saved mode: ");
    Serial.println(savedMode);
    Serial.print("Saved device orientation: ");
    Serial.println(savedOrientation);
    Serial.print("Saved LED brightness: ");
    Serial.println(savedBrightness);
  }
  return true;
}

class MotoButtonsServerCallbacks : public NimBLEServerCallbacks
{
  void onConnect(NimBLEServer *server, NimBLEConnInfo &connInfo) override
  {
    BLE_connected = true;
  }

  void onDisconnect(NimBLEServer *server, NimBLEConnInfo &connInfo, int reason) override
  {
    BLE_connected = false;
    NimBLEDevice::startAdvertising();
  }
};

void setupBLE()
{
  NimBLEDevice::init(BLE_DEVICE_NAME);
  NimBLEDevice::setPower(BLE_TX_POWER);
  NimBLEDevice::setSecurityAuth(true, false, true);
  NimBLEDevice::setSecurityIOCap(BLE_HS_IO_NO_INPUT_OUTPUT);

  bleServer = NimBLEDevice::createServer();
  bleServer->setCallbacks(new MotoButtonsServerCallbacks());

  blehid = new NimBLEHIDDevice(bleServer);
  blehid->setManufacturer(BLE_MANUFACTURER);
  blehid->setPnp(0x02, 0x303A, 0x4001, 0x0200);
  blehid->setHidInfo(0x00, 0x01);
  blehid->setReportMap((uint8_t *)HID_REPORT_DESCRIPTOR, sizeof(HID_REPORT_DESCRIPTOR));
  keyboardInput = blehid->getInputReport(KEYBOARD_REPORT_ID);
  consumerInput = blehid->getInputReport(CONSUMER_REPORT_ID);
  blehid->setBatteryLevel(100);

  NimBLEAdvertising *advertising = NimBLEDevice::getAdvertising();
  advertising->setAppearance(HID_KEYBOARD);
  advertising->addServiceUUID(blehid->getHidService()->getUUID());
  advertising->enableScanResponse(true);
  advertising->setPreferredParams(0x06, 0x12);
  NimBLEDevice::startAdvertising();
}

void setup()
{
  applyDefaultSettings();
  setupDigitalIO();
  setRGBColor(POWER_ON_COLOR);

  if (DEBUG)
  {
    Serial.begin(115200);
    unsigned long serialWaitStart = millis();
    while (!Serial && (millis() - serialWaitStart < 2000))
      delay(10);
    Serial.println("MotoButtons 2 ESP32-C3 BLE Controller");
  }

  bool settingsLoaded = readSettings();

  // Let the user change the orientation by holding one joystick direction
  // during the first part of startup.
  int buttonMap = getButtonMapSelection();
  if (buttonMap >= 0)
  {
    buttonOrientation = (uint8_t)buttonMap;
    setButtonMapping(buttonOrientation);
    flashLED(BUTTON_ORIENTATION_COLOR, 250, 250 * (buttonOrientation + 1));
    if (DEBUG)
    {
      Serial.print("Button orientation changed to: ");
      Serial.println(buttonOrientation);
    }
  }

  // Create or repair settings, and persist a startup orientation override.
  if (!settingsLoaded || buttonMap >= 0)
    writeSettings();

  // Refresh logical state after the final orientation mapping is known.
  setupDigitalIO();
  setupBLE();

  if (DEBUG)
    Serial.println("Setup complete; advertising BLE HID device.");
  setRGBColor(SETUP_COMPLETE_COLOR);
  indicateMode(currentMode);
}

void loop()
{
  static bool connectionIndicated = false;

  if (BLE_connected)
  {
    if (!connectionIndicated)
    {
      if (DEBUG)
        Serial.println("BLE connected to host.");
      showMode(currentMode);
      connectionIndicated = true;
      keyReportChanged = true;
    }
  }
  else
  {
    if (connectionIndicated)
    {
      clearKeyReport();
      keyReportChanged = false;
      forceKeyReport = false;
      connectionIndicated = false;
    }
    RGBToggle(BLE_COLOR);
    delay(200);
  }

  updateButtons();

  if (!BLE_connected)
    return;

  uint16_t repeatInterval = getRepeatInterval();
  bool repeatSend = repeatInterval > 0 && !keyReportChanged && !forceKeyReport &&
                    (millis() - lastRepeatTime >= repeatInterval);

  if (keyReportChanged || forceKeyReport || repeatSend)
  {
    if (repeatSend)
    {
      releaseAllKeys();
      delay(5);
    }

    if (forceKeyReport)
    {
      forceKeyReport = false;
      if (DEBUG)
        Serial.println("Key report forced.");
    }

    mapButtonsToKeyReport();
    sendKeyboardReport();
    lastRepeatTime = millis();
    if (DEBUG)
      Serial.println("Key report sent.");
  }
  else if (repeatInterval == 0)
  {
    lastRepeatTime = millis();
  }
}
