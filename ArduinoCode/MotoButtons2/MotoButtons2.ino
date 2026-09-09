/*********************************************************************
License: GNU GENERAL PUBLIC LICENSE; Version 3, 29 June 2007
Version: 2.0 with support for the following modes: DMD2, OsmAnd, media (music)
Device: ESP32-C3 OLED Mini (MotoButtons 2)
*********************************************************************/
#include <Arduino.h>
#include <Wire.h>
#include <NimBLEDevice.h>
#include <NimBLEHIDDevice.h>
#include <Preferences.h>
#include <esp_system.h>
#include <U8g2lib.h>
#include "driver/gpio.h"

// Enable serial debugging (turn this off if not connected to PC)
#define DEBUG true

/*
 * --------------------- HARDWARE PIN MAPPING -------------------------
 * ESP32-C3 OLED Mini board labels match raw GPIO numbers.
 * Use GPIO numbers here instead of Arduino D aliases because D aliases
 * change depending on the selected Arduino board profile.
 *
 * GPIO2, GPIO8, and GPIO9 are ESP32-C3 boot strapping pins. Avoid holding
 * inputs wired to those pins while powering on, resetting, or entering
 * upload mode.
 *
 * GPIO20 = RX and GPIO21 = TX are left free.
 */
const uint8_t PIN_JOYSTICK_UP = 0;
const uint8_t PIN_BUTTON_CENTER = 1;
const uint8_t PIN_JOYSTICK_RIGHT = 2;
const uint8_t PIN_JOYSTICK_DOWN = 3;
const uint8_t PIN_JOYSTICK_LEFT = 4;
const uint8_t OLED_SDA_PIN = 5;     // GPIO5 reserved by onboard OLED
const uint8_t OLED_SCL_PIN = 6;     // GPIO6 reserved by onboard OLED
const uint8_t PIN_BUTTON_A = 7;
const uint8_t STATUS_LED_PIN = 8;   // GPIO8 reserved by onboard/status LED
const uint8_t PIN_BUTTON_B = 9;
const uint8_t PIN_BUTTON_C = 10;
// GPIO20 = RX, free
// GPIO21 = TX, free

const bool BUTTON_UP_ACTIVE_LOW = true;
const bool BUTTON_DOWN_ACTIVE_LOW = true;
const bool BUTTON_LEFT_ACTIVE_LOW = true;
const bool BUTTON_RIGHT_ACTIVE_LOW = true;
const bool BUTTON_CENTER_ACTIVE_LOW = true;
const bool BUTTON_A_ACTIVE_LOW = true;
const bool BUTTON_B_ACTIVE_LOW = true;
const bool BUTTON_C_ACTIVE_LOW = true;

const bool STATUS_LED_ACTIVE_LOW = true;
const uint16_t STATUS_LED_PULSE_PERIOD_MS = 1200;
const uint16_t OLED_BOOT_MESSAGE_MS = 1000;
const uint16_t OLED_TRANSIENT_MS = 2000;
const uint16_t OLED_ORIENTATION_MESSAGE_MS = 3000;
const uint16_t OLED_SPLASH_MESSAGE_MS = 3000;
const uint16_t JOYSTICK_ANALOG_ACTIVE_LOW_PRESS_MIN = 1;
const uint16_t JOYSTICK_ANALOG_ACTIVE_LOW_PRESS_MAX = 10;
const uint16_t JOYSTICK_ANALOG_ACTIVE_LOW_RELEASE_MIN = 30;
const uint8_t JOYSTICK_ANALOG_SAMPLE_COUNT = 7;
const uint8_t JOYSTICK_ANALOG_CONFIRM_COUNT = 3;

// How long factory-reset and software-restart chords must be held
#define MODE_RESET_MS 5000

// Orientation of controller
#define DEFAULT_BUTTON_MAP 3
uint8_t buttonOrientation = DEFAULT_BUTTON_MAP;

/*----- Persistent Settings -----*/
const char SETTINGS_NAMESPACE[] = "motobuttons";
const char SETTINGS_MODE_KEY[] = "mode";
const char SETTINGS_OLED_KEY[] = "oled";
const char SETTINGS_ORIENT_KEY[] = "orient";

// BLE configuration
#define BLE_TX_POWER 9
/* DMD2 identifies a controller by matching this Bluetooth name against its own
 * list of known devices. Candidates to test, in order of preference:
 *   "DMD2 CTL 8K" - the name DMD published for DIY 8 button controllers
 *   "CICTRL"      - Carpe Iter Adventure Control; reported as detected by DMD2
 *   "BarButtons"  - JaxeADV BarButtons, a DMD2 certified controller
 * Any name change needs the phone to forget the pairing before re-pairing.
 */
const char BLE_DEVICE_NAME[] = "Bush Moto OLED";
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

/* DMD2 Mode Configuration */
// USB HID usage IDs used in keyboard reports.
const uint8_t HID_KEY_NONE = 0x00;
const uint8_t HID_KEY_C = 0x06;
const uint8_t HID_KEY_ENTER = 0x28;
const uint8_t HID_KEY_MINUS = 0x2D;
const uint8_t HID_KEY_EQUAL = 0x2E;
const uint8_t HID_KEY_F5 = 0x3E;
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
const uint8_t DMD_KEY_CENTER = HID_KEY_F5;
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

uint8_t BUTTON_UP = PIN_JOYSTICK_UP;
uint8_t BUTTON_DOWN = PIN_JOYSTICK_DOWN;
uint8_t BUTTON_LEFT = PIN_JOYSTICK_LEFT;
uint8_t BUTTON_RIGHT = PIN_JOYSTICK_RIGHT;
uint8_t BUTTON_CENTER = PIN_BUTTON_CENTER;
uint8_t BUTTON_A = PIN_BUTTON_A;
uint8_t BUTTON_B = PIN_BUTTON_B;
uint8_t BUTTON_C = PIN_BUTTON_C;

U8G2_SSD1306_72X40_ER_F_HW_I2C oled(U8G2_R0, U8X8_PIN_NONE, OLED_SCL_PIN, OLED_SDA_PIN);
const uint8_t OLED_WIDTH = 72;
const uint8_t OLED_HEIGHT = 40;
const uint8_t OLED_MAX_LINES = 3;
// Button overlay bar across the top, with the message area below it.
const uint8_t OLED_BAR_SLOTS = 8;
const uint8_t OLED_BAR_SLOT_WIDTH = OLED_WIDTH / OLED_BAR_SLOTS;
const uint8_t OLED_BAR_CENTER_Y = 4;
const uint8_t OLED_TEXT_TOP = 12;
// Boot splash: three fixed left-aligned lines, drawn as large as will fit.
const uint8_t OLED_SPLASH_LINES = 3;
const char *const OLED_SPLASH_TEXT[OLED_SPLASH_LINES] = {"READY", "TO >>", "RACE"};
// Connection screens share one size, so both strings must fit the chosen font.
const char OLED_TEXT_CONNECTING[] = "Connecting";
const char OLED_TEXT_CONNECTED[] = "Connected";
// Largest first: used where a screen must keep one stable font size.
const uint8_t *const OLED_FONT_LADDER[] = {
  u8g2_font_10x20_tr,
  u8g2_font_9x15B_tr,
  u8g2_font_9x15_tr,
  u8g2_font_8x13B_tr,
  u8g2_font_7x14B_tr,
  u8g2_font_7x13B_tr,
  u8g2_font_6x12_tr,
  u8g2_font_5x8_tr
};
const uint8_t OLED_FONT_LADDER_COUNT = sizeof(OLED_FONT_LADDER) / sizeof(OLED_FONT_LADDER[0]);
bool oledEnabled = true;
bool oledAwake = false;
bool displayComboConsumed = false;
// Transient priority: a lower-priority message cannot cut short a higher one.
const uint8_t OLED_PRIORITY_NORMAL = 0;
const uint8_t OLED_PRIORITY_HIGH = 1;
const char *oledTransientText = nullptr;
unsigned long oledTransientUntil = 0;
uint8_t oledTransientPriority = OLED_PRIORITY_NORMAL;
const uint8_t *oledTransientFont = nullptr;
// Which kind of screen is currently on the panel, so each render can tell
// whether it still owns the display.
typedef enum
{
  SCREEN_NONE = 0,
  SCREEN_TEXT,
  SCREEN_SPLASH
} OledScreen;
OledScreen oledScreen = SCREEN_NONE;
// The splash runs after the "Connected" message, for as long as we stay connected.
bool oledSplashPending = false;
unsigned long oledSplashUntil = 0;
char lastOledText[40] = "";
// -1 forces the first bar draw; otherwise the bar is redrawn on any state change.
int16_t lastButtonMask = -1;
// Transients are built at runtime, so keep our own copy of the text.
char oledTransientBuffer[sizeof(lastOledText)] = "";

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
unsigned long lastRepeatTime = 0;
/*------------------- END BUTTON CONFIG & LOGIC-----------------------*/

const char *getModeName(Mode mode)
{
  switch (mode)
  {
  case DMD2:
    return "DMD";
  case OsmAnd:
    return "OsmAnd";
  case MEDIA:
    return "Media";
  default:
    return "?";
  }
}

void setStatusLED(uint8_t brightness)
{
  analogWrite(STATUS_LED_PIN, STATUS_LED_ACTIVE_LOW ? 255 - brightness : brightness);
}

bool isJoystickAnalogPin(uint8_t pin)
{
  return pin == PIN_JOYSTICK_UP ||
         pin == PIN_JOYSTICK_DOWN ||
         pin == PIN_JOYSTICK_LEFT ||
         pin == PIN_JOYSTICK_RIGHT ||
         pin == PIN_BUTTON_CENTER;
}

struct JoystickAnalogFilter
{
  uint8_t pin;
  bool state;
  uint8_t pressCount;
  uint8_t releaseCount;
  int lastAdc;
};

JoystickAnalogFilter joystickAnalogFilters[] = {
  {PIN_JOYSTICK_UP, false, 0, 0, -1},
  {PIN_JOYSTICK_DOWN, false, 0, 0, -1},
  {PIN_JOYSTICK_LEFT, false, 0, 0, -1},
  {PIN_JOYSTICK_RIGHT, false, 0, 0, -1},
  {PIN_BUTTON_CENTER, false, 0, 0, -1}
};

int8_t getJoystickAnalogFilterIndex(uint8_t pin)
{
  for (size_t i = 0; i < sizeof(joystickAnalogFilters) / sizeof(joystickAnalogFilters[0]); i++)
  {
    if (joystickAnalogFilters[i].pin == pin)
      return i;
  }
  return -1;
}

int readFilteredJoystickAnalog(uint8_t pin)
{
  uint16_t samples[JOYSTICK_ANALOG_SAMPLE_COUNT];
  for (uint8_t i = 0; i < JOYSTICK_ANALOG_SAMPLE_COUNT; i++)
  {
    samples[i] = analogRead(pin);
    delayMicroseconds(80);
  }

  for (uint8_t i = 1; i < JOYSTICK_ANALOG_SAMPLE_COUNT; i++)
  {
    uint16_t value = samples[i];
    int8_t j = i - 1;
    while (j >= 0 && samples[j] > value)
    {
      samples[j + 1] = samples[j];
      j--;
    }
    samples[j + 1] = value;
  }

  return samples[JOYSTICK_ANALOG_SAMPLE_COUNT / 2];
}

bool readButtonPin(uint8_t pin, bool activeLow)
{
  if (isJoystickAnalogPin(pin))
  {
    int8_t filterIndex = getJoystickAnalogFilterIndex(pin);
    if (filterIndex < 0)
      return false;

    JoystickAnalogFilter *filter = &joystickAnalogFilters[filterIndex];
    int analogValue = readFilteredJoystickAnalog(pin);
    filter->lastAdc = analogValue;

    if (activeLow)
    {
      if (analogValue >= JOYSTICK_ANALOG_ACTIVE_LOW_PRESS_MIN &&
          analogValue <= JOYSTICK_ANALOG_ACTIVE_LOW_PRESS_MAX)
      {
        filter->pressCount++;
        filter->releaseCount = 0;
        if (filter->pressCount >= JOYSTICK_ANALOG_CONFIRM_COUNT)
          filter->state = true;
      }
      else if (analogValue == 0 || analogValue >= JOYSTICK_ANALOG_ACTIVE_LOW_RELEASE_MIN)
      {
        filter->releaseCount++;
        filter->pressCount = 0;
        if (filter->releaseCount >= JOYSTICK_ANALOG_CONFIRM_COUNT)
          filter->state = false;
      }
      else
      {
        filter->pressCount = 0;
        filter->releaseCount = 0;
      }
      return filter->state;
    }
  }

  bool reading = digitalRead(pin);
  return activeLow ? !reading : reading;
}

void setupOLED()
{
  Wire.begin(OLED_SDA_PIN, OLED_SCL_PIN);
  oled.begin();
  oled.setContrast(255);
  oledAwake = true;
}

void setOLEDEnabled(bool enabled)
{
  oledEnabled = enabled;
  lastOledText[0] = '\0';
  lastButtonMask = -1;
  oledScreen = SCREEN_NONE;
  if (oledEnabled)
  {
    oled.setPowerSave(0);
    oledAwake = true;
    return;
  }

  oled.clearBuffer();
  oled.sendBuffer();
  oled.setPowerSave(1);
  oledAwake = false;
}

/* One bit per button, so the overlay is only redrawn when something changes. */
uint8_t getButtonMask()
{
  return (uint8_t)(button_A_state << 0) |
         (uint8_t)(button_B_state << 1) |
         (uint8_t)(button_C_state << 2) |
         (uint8_t)(button_up_state << 3) |
         (uint8_t)(button_down_state << 4) |
         (uint8_t)(button_left_state << 5) |
         (uint8_t)(button_right_state << 6) |
         (uint8_t)(button_center_state << 7);
}

int16_t getBarSlotCenterX(uint8_t slot)
{
  return slot * OLED_BAR_SLOT_WIDTH + OLED_BAR_SLOT_WIDTH / 2;
}

/* Each slot keeps its fixed position and stays blank until its button is pressed. */
void drawBarLabel(uint8_t slot, const char *label, bool pressed)
{
  if (!pressed)
    return;

  oled.setFont(u8g2_font_5x8_tr);
  int16_t x = getBarSlotCenterX(slot) - oled.getStrWidth(label) / 2;
  oled.drawStr(x, OLED_BAR_CENTER_Y + 3, label);
}

/* Arrows appear only while their direction is pressed. */
void drawBarArrow(uint8_t slot, uint8_t direction, bool pressed)
{
  if (!pressed)
    return;

  int16_t cx = getBarSlotCenterX(slot);
  int16_t cy = OLED_BAR_CENTER_Y;
  int16_t tipX = cx;
  int16_t tipY = cy;
  int16_t baseAX = cx;
  int16_t baseAY = cy;
  int16_t baseBX = cx;
  int16_t baseBY = cy;

  switch (direction)
  {
  case 0: // up
    tipY = cy - 3;
    baseAX = cx - 3;
    baseAY = cy + 2;
    baseBX = cx + 3;
    baseBY = cy + 2;
    break;
  case 1: // down
    tipY = cy + 3;
    baseAX = cx - 3;
    baseAY = cy - 2;
    baseBX = cx + 3;
    baseBY = cy - 2;
    break;
  case 2: // left
    tipX = cx - 3;
    baseAX = cx + 2;
    baseAY = cy - 3;
    baseBX = cx + 2;
    baseBY = cy + 3;
    break;
  default: // right
    tipX = cx + 3;
    baseAX = cx - 2;
    baseAY = cy - 3;
    baseBX = cx - 2;
    baseBY = cy + 3;
    break;
  }

  oled.drawTriangle(tipX, tipY, baseAX, baseAY, baseBX, baseBY);
}

/* Debug overlay: A B C, the four directions as arrows, center as a dot.
 * A slot is drawn only while its button is held, and slots never move, so a
 * combination of presses is read off the fixed positions.
 * States are the logical (orientation-mapped) ones, so the bar also shows
 * the effect of the current orientation.
 */
void drawButtonBar()
{
  drawBarLabel(0, "A", button_A_state);
  drawBarLabel(1, "B", button_B_state);
  drawBarLabel(2, "C", button_C_state);
  drawBarArrow(3, 0, button_up_state);
  drawBarArrow(4, 1, button_down_state);
  drawBarArrow(5, 2, button_left_state);
  drawBarArrow(6, 3, button_right_state);

  if (button_center_state)
    oled.drawDisc(getBarSlotCenterX(7), OLED_BAR_CENTER_Y, 3);
}

/* Selects and applies the largest ladder font in which every one of texts
 * fits maxWidth and lineCount stacked lines fit maxHeight. Screens that must
 * not change size between messages measure all their strings at once.
 */
const uint8_t *pickFittingFont(const char *const *texts, uint8_t textCount, uint8_t lineCount,
                               int16_t maxWidth, int16_t maxHeight, int16_t *ascentOut, int16_t *pitchOut)
{
  const uint8_t *chosenFont = OLED_FONT_LADDER[OLED_FONT_LADDER_COUNT - 1];

  for (uint8_t f = 0; f < OLED_FONT_LADDER_COUNT; f++)
  {
    oled.setFont(OLED_FONT_LADDER[f]);
    int16_t pitch = oled.getAscent() + 1;
    bool fits = pitch * lineCount <= maxHeight;
    for (uint8_t i = 0; i < textCount && fits; i++)
    {
      if (oled.getStrWidth(texts[i]) > maxWidth)
        fits = false;
    }

    if (fits)
    {
      chosenFont = OLED_FONT_LADDER[f];
      break;
    }
  }

  oled.setFont(chosenFont);
  if (ascentOut != nullptr)
    *ascentOut = oled.getAscent();
  if (pitchOut != nullptr)
    *pitchOut = oled.getAscent() + 1;
  return chosenFont;
}

/* All mode names share one size, so the longest one decides it. */
const uint8_t *getModeScreenFont()
{
  static const uint8_t *font = nullptr;
  if (font == nullptr)
  {
    const char *const modeNames[] = {getModeName(DMD2), getModeName(OsmAnd), getModeName(MEDIA)};
    // The mode screen carries the button bar, so the text area is shorter.
    font = pickFittingFont(modeNames, 3, 1, OLED_WIDTH, OLED_HEIGHT - OLED_TEXT_TOP, nullptr, nullptr);
  }
  return font;
}

/* "Connecting" and "Connected" share one size for the same reason. */
const uint8_t *getConnectionScreenFont()
{
  static const uint8_t *font = nullptr;
  if (font == nullptr)
  {
    const char *const texts[] = {OLED_TEXT_CONNECTING, OLED_TEXT_CONNECTED};
    font = pickFittingFont(texts, 2, 1, OLED_WIDTH, OLED_HEIGHT, nullptr, nullptr);
  }
  return font;
}

/* Boot splash. Left aligned, no button bar, and the largest ladder font
 * whose three lines still fit the 72x40 panel.
 */
void renderSplashScreen()
{
  if (!oledEnabled)
    return;

  if (oledScreen == SCREEN_SPLASH)
    return;

  if (!oledAwake)
  {
    oled.setPowerSave(0);
    oledAwake = true;
  }

  int16_t ascent = 0;
  int16_t pitch = 0;
  pickFittingFont(OLED_SPLASH_TEXT, OLED_SPLASH_LINES, OLED_SPLASH_LINES,
                  OLED_WIDTH, OLED_HEIGHT, &ascent, &pitch);

  int16_t totalHeight = pitch * (OLED_SPLASH_LINES - 1) + ascent;
  int16_t top = (OLED_HEIGHT - totalHeight) / 2;
  if (top < 0)
    top = 0;

  oled.clearBuffer();
  for (uint8_t i = 0; i < OLED_SPLASH_LINES; i++)
    oled.drawStr(0, top + ascent + i * pitch, OLED_SPLASH_TEXT[i]);
  oled.sendBuffer();

  oledScreen = SCREEN_SPLASH;
}

void renderOLEDText(const char *text, bool showButtonBar = false, const uint8_t *forcedFont = nullptr)
{
  if (!oledEnabled)
    return;

  if (!oledAwake)
  {
    oled.setPowerSave(0);
    oledAwake = true;
  }

  uint8_t buttonMask = showButtonBar ? getButtonMask() : 0;
  if (oledScreen == SCREEN_TEXT && buttonMask == lastButtonMask &&
      strncmp(lastOledText, text, sizeof(lastOledText)) == 0)
    return;
  lastButtonMask = buttonMask;
  oledScreen = SCREEN_TEXT;

  strncpy(lastOledText, text, sizeof(lastOledText) - 1);
  lastOledText[sizeof(lastOledText) - 1] = '\0';

  oled.clearBuffer();
  if (showButtonBar)
    drawButtonBar();
  if (forcedFont != nullptr)
  {
    // Screens that must not change size between messages pass their own font.
    oled.setFont(forcedFont);
  }
  else
  {
    oled.setFont(u8g2_font_7x14B_tr);
    // Fall back to a smaller font so longer messages can be wrapped instead of clipped.
    if (strchr(text, '\n') != nullptr || oled.getStrWidth(text) > OLED_WIDTH)
      oled.setFont(u8g2_font_5x8_tr);
  }

  // Break on '\n', then greedily wrap each part on spaces to fit the panel.
  char lines[OLED_MAX_LINES][sizeof(lastOledText)];
  char candidate[sizeof(lastOledText) * 2];
  char word[sizeof(lastOledText)];
  memset(lines, 0, sizeof(lines));

  uint8_t lineIndex = 0;
  const char *cursor = text;
  while (*cursor != '\0' && lineIndex < OLED_MAX_LINES)
  {
    if (*cursor == '\n')
    {
      // Only advance for a hard break once the current line has content.
      if (lines[lineIndex][0] != '\0')
        lineIndex++;
      cursor++;
      continue;
    }

    if (*cursor == ' ')
    {
      cursor++;
      continue;
    }

    const char *wordEnd = cursor;
    while (*wordEnd != '\0' && *wordEnd != ' ' && *wordEnd != '\n')
      wordEnd++;

    size_t wordLength = (size_t)(wordEnd - cursor);
    if (wordLength > sizeof(word) - 1)
      wordLength = sizeof(word) - 1;
    memcpy(word, cursor, wordLength);
    word[wordLength] = '\0';
    cursor = wordEnd;

    if (lines[lineIndex][0] == '\0')
    {
      strncpy(lines[lineIndex], word, sizeof(lines[lineIndex]) - 1);
      continue;
    }

    snprintf(candidate, sizeof(candidate), "%s %s", lines[lineIndex], word);
    if (oled.getStrWidth(candidate) <= OLED_WIDTH)
    {
      strncpy(lines[lineIndex], candidate, sizeof(lines[lineIndex]) - 1);
      lines[lineIndex][sizeof(lines[lineIndex]) - 1] = '\0';
      continue;
    }

    if (lineIndex + 1 >= OLED_MAX_LINES)
      break;
    lineIndex++;
    strncpy(lines[lineIndex], word, sizeof(lines[lineIndex]) - 1);
  }

  uint8_t lineCount = OLED_MAX_LINES;
  if (lineIndex < OLED_MAX_LINES)
    lineCount = lines[lineIndex][0] == '\0' ? lineIndex : lineIndex + 1;
  int16_t lineHeight = oled.getMaxCharHeight();
  int16_t textTop = showButtonBar ? OLED_TEXT_TOP : 0;
  int16_t textAreaHeight = OLED_HEIGHT - textTop;
  int16_t top = textTop + (textAreaHeight - lineCount * lineHeight) / 2;
  if (top < textTop)
    top = textTop;

  for (uint8_t i = 0; i < lineCount; i++)
  {
    uint16_t lineWidth = oled.getStrWidth(lines[i]);
    int16_t x = lineWidth < OLED_WIDTH ? (OLED_WIDTH - lineWidth) / 2 : 0;
    oled.drawStr(x, top + lineHeight * (i + 1) - 2, lines[i]);
  }
  oled.sendBuffer();
}

void showOLEDTransient(const char *text, uint16_t durationMs, uint8_t priority, const uint8_t *font)
{
  // Keep the message already on screen if it outranks this one.
  bool transientActive = oledTransientText != nullptr && millis() < oledTransientUntil;
  if (transientActive && priority < oledTransientPriority)
    return;

  oledTransientPriority = priority;
  oledTransientFont = font;
  strncpy(oledTransientBuffer, text, sizeof(oledTransientBuffer) - 1);
  oledTransientBuffer[sizeof(oledTransientBuffer) - 1] = '\0';
  oledTransientText = oledTransientBuffer;
  oledTransientUntil = millis() + durationMs;
  renderOLEDText(oledTransientBuffer, false, oledTransientFont);
}

void updateOLEDStatus(bool force = false)
{
  if (!oledEnabled)
  {
    if (force)
      setOLEDEnabled(false);
    return;
  }

  if (force)
  {
    lastOledText[0] = '\0';
    oledScreen = SCREEN_NONE;
  }

  // A transient message wins over the idle status, connected or not.
  if (oledTransientText != nullptr)
  {
    if (millis() < oledTransientUntil)
    {
      renderOLEDText(oledTransientText, false, oledTransientFont);
      return;
    }
    oledTransientText = nullptr;
    oledTransientPriority = OLED_PRIORITY_NORMAL;
    oledTransientFont = nullptr;
  }

  // Then the splash, which sits between "Connected" and the mode screen and
  // is abandoned if the link drops.
  if (oledSplashPending)
  {
    if (!BLE_connected)
    {
      oledSplashPending = false;
      oledSplashUntil = 0;
    }
    else
    {
      if (oledSplashUntil == 0)
        oledSplashUntil = millis() + OLED_SPLASH_MESSAGE_MS;

      if (millis() < oledSplashUntil)
      {
        renderSplashScreen();
        return;
      }

      oledSplashPending = false;
      oledSplashUntil = 0;
    }
  }

  const char *text = BLE_connected ? getModeName(currentMode) : OLED_TEXT_CONNECTING;

  // The button bar is part of the mode screen only.
  renderOLEDText(text, BLE_connected,
                 BLE_connected ? getModeScreenFont() : getConnectionScreenFont());
}

void updateStatusLED()
{
  if (BLE_connected)
  {
    setStatusLED(255);
    return;
  }

  uint16_t phase = millis() % STATUS_LED_PULSE_PERIOD_MS;
  uint16_t halfPeriod = STATUS_LED_PULSE_PERIOD_MS / 2;
  uint8_t brightness = phase < halfPeriod
                         ? map(phase, 0, halfPeriod, 0, 255)
                         : map(phase, halfPeriod, STATUS_LED_PULSE_PERIOD_MS, 255, 0);
  setStatusLED(brightness);
}

void indicateMode(Mode mode)
{
  if (BLE_connected)
    renderOLEDText(getModeName(mode), true, getModeScreenFont());
  else
    updateOLEDStatus(true);
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

/* Label of the physical joystick direction a pin belongs to. */
const char *getJoystickPinName(uint8_t pin)
{
  if (pin == PIN_JOYSTICK_UP)
    return "UP";
  if (pin == PIN_JOYSTICK_DOWN)
    return "DOWN";
  if (pin == PIN_JOYSTICK_LEFT)
    return "LEFT";
  if (pin == PIN_JOYSTICK_RIGHT)
    return "RIGHT";
  return "?";
}

/* Map the physical joystick pin that should act as UP to a button map index. */
int8_t getOrientationForUpPin(uint8_t pin)
{
  switch (pin)
  {
  case PIN_JOYSTICK_RIGHT:
    return 0;
  case PIN_JOYSTICK_DOWN:
    return 1;
  case PIN_JOYSTICK_LEFT:
    return 2;
  case PIN_JOYSTICK_UP:
    return 3;
  default:
    return -1;
  }
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

void configureInputPull(uint8_t pin, bool activeLow)
{
  if (activeLow)
  {
    gpio_pullup_en((gpio_num_t)pin);
    gpio_pulldown_dis((gpio_num_t)pin);
  }
  else
  {
    gpio_pullup_dis((gpio_num_t)pin);
    gpio_pulldown_en((gpio_num_t)pin);
  }
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
  oledEnabled = true;
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
  displayComboConsumed = false;
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
    showOLEDTransient("Reset completed", OLED_TRANSIENT_MS, OLED_PRIORITY_HIGH, nullptr);
    delay(OLED_TRANSIENT_MS);
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

void applyOrientation(uint8_t orientation)
{
  buttonOrientation = orientation;
  setButtonMapping(orientation);
  // Re-read the inputs so logical state matches the new mapping.
  setupDigitalIO();
}

/* Returns the physical joystick pin being held, or -1 when none or more than
 * one is. Called at boot only, so the analog filter is primed first.
 */
int8_t readHeldJoystickDirectionPin()
{
  const uint8_t directionPins[] = {PIN_JOYSTICK_UP, PIN_JOYSTICK_DOWN, PIN_JOYSTICK_LEFT, PIN_JOYSTICK_RIGHT};
  const uint8_t pinCount = sizeof(directionPins) / sizeof(directionPins[0]);

  // The joystick pins are read through an ADC filter that needs several
  // consistent samples before it reports a press.
  for (uint8_t pass = 0; pass <= JOYSTICK_ANALOG_CONFIRM_COUNT; pass++)
    for (uint8_t i = 0; i < pinCount; i++)
      readButtonPin(directionPins[i], true);

  int8_t heldPin = -1;
  for (uint8_t i = 0; i < pinCount; i++)
  {
    if (!readButtonPin(directionPins[i], true))
      continue;
    if (heldPin >= 0)
      return -1; // ambiguous: more than one direction held
    heldPin = (int8_t)directionPins[i];
  }
  return heldPin;
}

/* Orientation is selected at boot only: whichever joystick direction is held
 * while the controller starts up becomes UP. Returns true when it was set.
 */
bool applyBootOrientation()
{
  int8_t heldPin = readHeldJoystickDirectionPin();
  if (heldPin < 0)
    return false;

  int8_t orientation = getOrientationForUpPin((uint8_t)heldPin);
  if (orientation < 0)
    return false;

  applyOrientation((uint8_t)orientation);
  writeSettings();

  // Part of the boot sequence, so hold it on screen instead of racing the
  // transient timer against BLE reconnecting.
  char message[sizeof(lastOledText)];
  snprintf(message, sizeof(message), "Orientation\nis set\nUP is %s", getJoystickPinName((uint8_t)heldPin));
  renderOLEDText(message);
  delay(OLED_ORIENTATION_MESSAGE_MS);
  if (DEBUG)
  {
    Serial.print("Boot orientation set to map ");
    Serial.print(orientation);
    Serial.print("; UP is ");
    Serial.println(getJoystickPinName((uint8_t)heldPin));
  }
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

void handleDisplayToggleCombo()
{
  if (button_A_state && button_B_state && !button_C_state)
  {
    unsigned long displayHoldMs = millis() - max(button_A_time, button_B_time);
    if (displayHoldMs > MODE_TOGGLE_MS && !displayComboConsumed)
    {
      releaseAllKeys();
      displayComboConsumed = true;
      button_A_flipped = false;
      button_B_flipped = false;
      setOLEDEnabled(!oledEnabled);
      updateOLEDStatus(true);
      writeSettings();
      if (DEBUG)
      {
        Serial.print("OLED ");
        Serial.println(oledEnabled ? "enabled" : "disabled");
      }
    }
  }
  else
  {
    if (displayComboConsumed)
    {
      button_A_flipped = false;
      button_B_flipped = false;
      if (!button_A_state && !button_B_state)
      {
        displayComboConsumed = false;
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
    // DMD2 handles repeat, repeat speed and long press itself, and can only do
    // that if it sees one clean key down and key up per physical press.
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

  /*------------------- Toggle OLED display -----------------------------*/
  handleDisplayToggleCombo();
  /*------------------------------------------------------------------*/

  // Once a chord action has fired, suppress its component buttons until
  // every button in the chord has been released.
  if (modeComboConsumed || displayComboConsumed)
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
    // Every key is held for exactly as long as its button is: the report
    // simply mirrors the current button states. The A/B/C exclusions keep the
    // chords (mode change, display toggle, restart, reset) from leaking keys.
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
    if (button_center_state)
    {
      if (DEBUG)
        Serial.println("DMD2 CENTER");
      keyReport[i] = DMD_KEY_CENTER;
      ++i;
    }
    else if (button_center_flipped)
      button_center_flipped = false;

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

    if (button_C_state && !button_A_state && !button_B_state && (i < N_KEY_REPORT))
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

  configureInputPull(BUTTON_UP, BUTTON_UP_ACTIVE_LOW);
  configureInputPull(BUTTON_DOWN, BUTTON_DOWN_ACTIVE_LOW);
  configureInputPull(BUTTON_LEFT, BUTTON_LEFT_ACTIVE_LOW);
  configureInputPull(BUTTON_RIGHT, BUTTON_RIGHT_ACTIVE_LOW);
  configureInputPull(BUTTON_CENTER, BUTTON_CENTER_ACTIVE_LOW);
  configureInputPull(BUTTON_A, BUTTON_A_ACTIVE_LOW);
  configureInputPull(BUTTON_B, BUTTON_B_ACTIVE_LOW);
  configureInputPull(BUTTON_C, BUTTON_C_ACTIVE_LOW);

  pinMode(STATUS_LED_PIN, OUTPUT);
  setStatusLED(0);

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
                 preferences.putUChar(SETTINGS_ORIENT_KEY, buttonOrientation) == sizeof(uint8_t) &&
                 preferences.putBool(SETTINGS_OLED_KEY, oledEnabled);
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
                  preferences.isKey(SETTINGS_ORIENT_KEY) &&
                  preferences.isKey(SETTINGS_OLED_KEY);
  uint8_t savedMode = preferences.getUChar(SETTINGS_MODE_KEY, 0);
  uint8_t savedOrientation = preferences.getUChar(SETTINGS_ORIENT_KEY, DEFAULT_BUTTON_MAP);
  bool savedOLEDEnabled = preferences.getBool(SETTINGS_OLED_KEY, true);
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
  setButtonMapping(buttonOrientation);
  oledEnabled = savedOLEDEnabled;
  setOLEDEnabled(oledEnabled);

  if (DEBUG)
  {
    Serial.print("Saved mode: ");
    Serial.println(savedMode);
    Serial.print("Saved orientation: ");
    Serial.println(savedOrientation);
    Serial.print("Saved OLED state: ");
    Serial.println(savedOLEDEnabled ? "enabled" : "disabled");
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

  NimBLEAdvertisementData scanResponse;
  scanResponse.setName(BLE_DEVICE_NAME);
  advertising->setScanResponseData(scanResponse);

  bool advertisingStarted = NimBLEDevice::startAdvertising();
  if (DEBUG)
    Serial.println(advertisingStarted ? "BLE advertising started." : "BLE advertising failed.");
}

void setup()
{
  applyDefaultSettings();
  setupDigitalIO();
  setupOLED();
  renderOLEDText("Booting...");
  delay(OLED_BOOT_MESSAGE_MS);

  if (DEBUG)
  {
    Serial.begin(115200);
    unsigned long serialWaitStart = millis();
    while (!Serial && (millis() - serialWaitStart < 2000))
      delay(10);
    Serial.println("MotoButtons 2 ESP32-C3 BLE Controller");
  }

  bool settingsLoaded = readSettings();
  setButtonMapping(buttonOrientation);

  // A joystick direction held while booting selects the orientation.
  bool orientationSet = applyBootOrientation();

  // Create or repair settings.
  if (!settingsLoaded && !orientationSet)
    writeSettings();

  // Refresh logical state after the final orientation mapping is known.
  setupDigitalIO();
  setupBLE();

  if (DEBUG)
    Serial.println("Setup complete; advertising BLE HID device.");
  updateOLEDStatus(true);
}

void loop()
{
  static bool connectionIndicated = false;

  updateStatusLED();
  updateOLEDStatus();

  if (BLE_connected)
  {
    if (!connectionIndicated)
    {
      if (DEBUG)
        Serial.println("BLE connected to host.");
      showOLEDTransient(OLED_TEXT_CONNECTED, OLED_TRANSIENT_MS, OLED_PRIORITY_NORMAL,
                        getConnectionScreenFont());
      // The splash follows the "Connected" message.
      oledSplashPending = true;
      oledSplashUntil = 0;
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
