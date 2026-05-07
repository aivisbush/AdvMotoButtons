/*********************************************************************
License: GNU GENERAL PUBLIC LICENSE; Version 3, 29 June 2007
Version: 2.0 with support for the following modes: DMD2, OsmAnd, media (music)
Device: Seeed XIAO ESP32-C6 (MotoButtons 2)
*********************************************************************/
#include <HijelHID_BLEKeyboard.h>
#include <Preferences.h>
#include <stdlib.h>

// Enable serial debugging (turn this off if not connected to PC)
#define DEBUG false

// How long to wait until the reset combo is activated
#define MODE_RESET_MS 5000

// Orientation of controller
#define DEFAULT_BUTTON_MAP 3
#define STARTUP_ORIENTATION_WINDOW_MS 500
uint8_t buttonOrientation = DEFAULT_BUTTON_MAP;

/*----- Persistent Storage -----*/
// This is used to store settings, such as the last mode.
Preferences preferences;
const char SETTINGS_NAMESPACE[] = "MotoButtons";

// BLE configuration
#define BLE_TX_POWER 8
const char BLE_DEVICE_NAME[] = "Bush Moto BT7";
const char BLE_DEVICE_MODEL[] = "Btns v2.0";
const char BLE_MANUFACTURER[] = "Bush";
bool BLE_connected = false;

// BLE classes
HijelHID_BLEKeyboard bleKeyboard(BLE_DEVICE_NAME, BLE_MANUFACTURER, 100);

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
bool brightnessSettingsDirty = false;

/* DMD2 Mode Configuration */
// https://www.drivemodedashboard.com/controller-implementation-guide/
// Key codes to send for button presses:
const uint8_t DMD_KEY_UP = KEY_UP;
const uint8_t DMD_KEY_DOWN = KEY_DOWN;
const uint8_t DMD_KEY_LEFT = KEY_LEFT;
const uint8_t DMD_KEY_RIGHT = KEY_RIGHT;
const uint8_t DMD_KEY_CENTER = KEY_F8;
const uint8_t DMD_KEY_A = KEY_F6;
const uint8_t DMD_KEY_B = KEY_F7;
const uint8_t DMD_KEY_C = KEY_RETURN;

/* OsmAnd Mode Configuration */
const uint8_t OSMAND_KEY_UP = KEY_UP;
const uint8_t OSMAND_KEY_DOWN = KEY_DOWN;
const uint8_t OSMAND_KEY_LEFT = KEY_LEFT;
const uint8_t OSMAND_KEY_RIGHT = KEY_RIGHT;
const uint8_t OSMAND_KEY_CENTER = KEY_NONE;           // unbound
const uint8_t OSMAND_KEY_A = KEY_EQUAL;               // zoom in (+ key)
const uint8_t OSMAND_KEY_B = KEY_MINUS;               // zoom out
const uint8_t OSMAND_KEY_C = KEY_C;                   // move to my location

/* Media Mode Configuration */
const uint16_t MEDIA_KEY_UP = MEDIA_VOLUME_UP;        // volume up
const uint16_t MEDIA_KEY_DOWN = MEDIA_VOLUME_DOWN;    // volume down
const uint16_t MEDIA_KEY_LEFT = MEDIA_PREV_TRACK;     // previous song
const uint16_t MEDIA_KEY_RIGHT = MEDIA_NEXT_TRACK;    // next song
const uint16_t MEDIA_KEY_CENTER = MEDIA_MUTE;         // Mute
const uint16_t MEDIA_KEY_A = MEDIA_PLAY_PAUSE;        // play - pause
const uint16_t MEDIA_KEY_B = MEDIA_BRIGHTNESS_UP;     // increase brightness
const uint16_t MEDIA_KEY_C = MEDIA_BRIGHTNESS_DOWN;   // decrease brightness
/*---------------------- END MODE CONFIGURATION ----------------------*/

/*----------------- BUTTON CONFIGURATION AND LOGIC -------------------*/
/* BLE key report */
#define N_KEY_REPORT 6
uint8_t keyReport[N_KEY_REPORT] = {KEY_NONE, KEY_NONE, KEY_NONE, KEY_NONE, KEY_NONE, KEY_NONE};
bool keyReportChanged = false;
bool forceKeyReport = false; // used to force another key report for key up activation events

// Digital IO pin mapping (default)
uint8_t BUTTON_UP = D3;
uint8_t BUTTON_DOWN = D8;
uint8_t BUTTON_LEFT = D4;
uint8_t BUTTON_RIGHT = D10;
uint8_t BUTTON_CENTER = D9;
uint8_t BUTTON_A = D5;
uint8_t BUTTON_B = D6;
uint8_t BUTTON_C = D7;
uint8_t RGB_LED_RED = D0;
uint8_t RGB_LED_BLUE = D1;
uint8_t RGB_LED_GREEN = D2;
#define USER_LED_PIN LED_BUILTIN
#define USER_LED_ACTIVE_LOW true

// Raw joystick GPIOs used for startup orientation selection.
const uint8_t JOYSTICK_PIN_UP = D4;
const uint8_t JOYSTICK_PIN_DOWN = D10;
const uint8_t JOYSTICK_PIN_LEFT = D8;
const uint8_t JOYSTICK_PIN_RIGHT = D3;

#define DEBOUNCE_TIME_MS 50
#define OSMAND_REPEAT_INTERVAL_MS 250
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
unsigned long lastOsmAndRepeatTime = 0;
/*------------------- END BUTTON CONFIG & LOGIC-----------------------*/

void setUserLED(bool on)
{
  digitalWrite(USER_LED_PIN, (USER_LED_ACTIVE_LOW ? !on : on) ? HIGH : LOW);
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

  setUserLED(LEDState != Off && LEDbrightness < 255);
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
    uint8_t up = digitalRead(JOYSTICK_PIN_UP);
    uint8_t down = digitalRead(JOYSTICK_PIN_DOWN);
    uint8_t left = digitalRead(JOYSTICK_PIN_LEFT);
    uint8_t right = digitalRead(JOYSTICK_PIN_RIGHT);

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

/* Change the mapping of buttons based on a map specifier, buttMap
 * butMapp:
 * 	0: UP is GPIO pin 2
 *  1: UP is GPIO pin 3
 *  2: UP is GPIO pin 4
 *  3: UP is GPIO pin 0
 */
bool setButtonMapping(uint8_t buttMap)
{
  switch (buttMap)
  {
  case 0: // three buttons on top
    BUTTON_UP = D10;
    BUTTON_DOWN = D4;
    BUTTON_LEFT = D3;
    BUTTON_RIGHT = D8;
    BUTTON_CENTER = D9;
    BUTTON_A = D5;
    BUTTON_B = D6;
    BUTTON_C = D7;
    break;
  case 1: // three buttons on left
    BUTTON_UP = D8;
    BUTTON_DOWN = D3;
    BUTTON_LEFT = D10;
    BUTTON_RIGHT = D4;
    BUTTON_CENTER = D9;
    BUTTON_A = D5;
    BUTTON_B = D6;
    BUTTON_C = D7;
    break;
  case 2: // three buttons on bottom
    BUTTON_UP = D4;
    BUTTON_DOWN = D10;
    BUTTON_LEFT = D8;
    BUTTON_RIGHT = D3;
    BUTTON_CENTER = D9;
    BUTTON_A = D5;
    BUTTON_B = D6;
    BUTTON_C = D7;
    break;
  case 3: // three buttons toward right
    BUTTON_UP = D3;
    BUTTON_DOWN = D8;
    BUTTON_LEFT = D4;
    BUTTON_RIGHT = D10;
    BUTTON_CENTER = D9;
    BUTTON_A = D5;
    BUTTON_B = D6;
    BUTTON_C = D7;
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
  if (button_center_state)
    return true;
  if (digitalRead(BUTTON_CENTER))
    return true;

  return false;
}

// if  This function should be called rapidly in a loop to update the debounce filter and key state
//  https://docs.arduino.cc/built-in-examples/digital/Debounce
bool debounceButton(unsigned int button, bool *state, bool *priorState, bool *buttonFlipped,
                    unsigned long *debounceTime, const char *buttonName)
{
  bool reading;
  unsigned long readTime;
  bool stateChanged = false;

  reading = digitalRead(button);
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

void releaseAllKeys()
{
  bleKeyboard.releaseAll();
}

void sendKeyboardReport()
{
  bleKeyboard.releaseAll();
  for (uint8_t i = 0; i < N_KEY_REPORT; i++)
  {
    if (keyReport[i] != KEY_NONE)
      bleKeyboard.press(keyReport[i]);
  }
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

bool parseUint8Token(const char *token, int minValue, int maxValue, uint8_t *outValue)
{
  if (token == NULL)
    return false;

  char *endPtr = NULL;
  long parsed = strtol(token, &endPtr, 10);
  if (endPtr == token || *endPtr != '\0' || parsed < minValue || parsed > maxValue)
    return false;

  *outValue = (uint8_t)parsed;
  return true;
}

bool handleFormatCombo(bool stateChanged)
{
  bool formatButtonsPressed = button_A_state && button_B_state && button_C_state;
  if (!formatButtonsPressed)
    return false;

  // The filesystem format combo takes priority over all smaller button combos.
  releaseAllKeys();
  clearABCButtonFlips();
  modeButtonsReleased = false;
  modeComboConsumed = false;
  brightnessAdjustTime = 0;
  brightnessComboConsumed = false;
  brightnessSettingsDirty = false;
  keyReportChanged = stateChanged;

  if (!formatComboConsumed &&
      (millis() - button_A_time > MODE_RESET_MS) &&
      (millis() - button_B_time > MODE_RESET_MS) &&
      (millis() - button_C_time > MODE_RESET_MS))
  {
    if (DEBUG)
      Serial.println("Clearing saved settings...");
    preferences.begin(SETTINGS_NAMESPACE, false);
    preferences.clear();
    preferences.end();
    applyDefaultSettings();
    writeSettings();
    flashLED(Red, 500, 2000);
    indicateMode(currentMode);
    formatComboConsumed = true;
  }
  return true;
}

bool handleConsumedFormatCombo(bool stateChanged)
{
  if (!formatComboConsumed)
    return false;

  clearABCButtonFlips();
  keyReportChanged = stateChanged;
  if (!button_A_state && !button_B_state && !button_C_state)
    formatComboConsumed = false;
  return true;
}

void handleModeCycleCombo()
{
  if (!button_B_state || !button_C_state)
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

bool hasOsmAndRepeatableHold()
{
  if (currentMode != OsmAnd)
    return false;

  return (button_A_state && !button_B_state && !button_C_state) ||
         (button_B_state && !button_A_state && !button_C_state) ||
         (button_C_state && !button_A_state && !button_B_state);
}

void updateButtons()
{
  bool stateChanged = false;

  // Read the state of all physical buttons
  stateChanged |= debounceButton(BUTTON_UP, &button_up_state, &button_up_state_prior, &button_up_flipped, &button_up_time, "UP");
  stateChanged |= debounceButton(BUTTON_DOWN, &button_down_state, &button_down_state_prior, &button_down_flipped, &button_down_time, "DOWN");
  stateChanged |= debounceButton(BUTTON_LEFT, &button_left_state, &button_left_state_prior, &button_left_flipped, &button_left_time, "LEFT");
  stateChanged |= debounceButton(BUTTON_RIGHT, &button_right_state, &button_right_state_prior, &button_right_flipped, &button_right_time, "RIGHT");
  stateChanged |= debounceButton(BUTTON_CENTER, &button_center_state, &button_center_state_prior, &button_center_flipped, &button_center_time, "CENTER");
  stateChanged |= debounceButton(BUTTON_A, &button_A_state, &button_A_state_prior, &button_A_flipped, &button_A_time, "A");
  stateChanged |= debounceButton(BUTTON_B, &button_B_state, &button_B_state_prior, &button_B_flipped, &button_B_time, "B");
  stateChanged |= debounceButton(BUTTON_C, &button_C_state, &button_C_state_prior, &button_C_flipped, &button_C_time, "C");

  if (handleFormatCombo(stateChanged))
    return;

  if (handleConsumedFormatCombo(stateChanged))
    return;

  // Indicate whether any buttons changed state
  keyReportChanged = stateChanged;

  /* Hard reset.
   * ESP32-C6 upload mode is handled by the ESP32 bootloader/Arduino IDE,
   * so this combo now performs a normal software restart.
   */
  if (button_A_state && button_C_state && !button_B_state && (millis() - button_A_time > MODE_RESET_MS) && (millis() - button_C_time > MODE_RESET_MS))
  {
    if (DEBUG)
      Serial.println("Restarting...");
    ESP.restart();
  }

  /*------------------- Handle mode cycling --------------------------*/
  handleModeCycleCombo();
  /*------------------------------------------------------------------*/

  /*------------------- Changing LED brightness --------------------------*/
  handleBrightnessCombo();
  /*------------------------------------------------------------------*/
}

void mapButtonsToKeyReport()
{
  unsigned int i = 0;

  bool centerActive = isCenterActive();
  switch (currentMode)
  {
  case DMD2:
    if (button_up_state && !centerActive)
    {
      keyReport[i] = DMD_KEY_UP;
      ++i;
    }
    if (button_down_state && !centerActive)
    {
      keyReport[i] = DMD_KEY_DOWN;
      ++i;
    }
    if (button_left_state && !centerActive)
    {
      keyReport[i] = DMD_KEY_LEFT;
      ++i;
    }
    if (button_right_state && !centerActive)
    {
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
      if (OSMAND_KEY_CENTER != KEY_NONE)
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

    if (button_C_state && !button_A_state && !button_B_state && (i < N_KEY_REPORT))
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
      bleKeyboard.tap(MEDIA_KEY_UP);
      ++i;
    }
    if (button_down_state && !centerActive)
    {
      if (DEBUG)
        Serial.println("Media key DOWN");
      bleKeyboard.tap(MEDIA_KEY_DOWN);
      ++i;
    }
    if (button_left_state && !centerActive)
    {
      if (DEBUG)
        Serial.println("Media key LEFT");
      bleKeyboard.tap(MEDIA_KEY_LEFT);
      ++i;
    }
    if (button_right_state && !centerActive)
    {
      if (DEBUG)
        Serial.println("Media key RIGHT");
      bleKeyboard.tap(MEDIA_KEY_RIGHT);
      ++i;
    }
    if (!button_center_state && button_center_flipped)
    {
      if (DEBUG)
        Serial.println("Media key CENTER");
      button_center_flipped = false;
      forceKeyReport = true;
      bleKeyboard.tap(MEDIA_KEY_CENTER);
      ++i;
    }
    if (button_A_state && !button_B_state && !button_C_state)
    {
      if (DEBUG)
        Serial.println("Media key A");
      button_A_flipped = false;
      bleKeyboard.tap(MEDIA_KEY_A);
      ++i;
    }
    else if (!button_A_state && button_A_flipped)
      button_A_flipped = false;

    if (button_B_state && !button_A_state && !button_C_state && (i < N_KEY_REPORT))
    {
      if (DEBUG)
        Serial.println("Media key B");
      button_B_flipped = false;
      bleKeyboard.tap(MEDIA_KEY_B);
      ++i;
    }
    else if (!button_B_state && button_B_flipped)
      button_B_flipped = false;

    if (button_C_state && !button_A_state && !button_B_state && (i < N_KEY_REPORT))
    {
      if (DEBUG)
        Serial.println("Media key C");
      button_C_flipped = false;
      bleKeyboard.tap(MEDIA_KEY_C);
      ++i;
    }
    else if (!button_C_state && button_C_flipped)
      button_C_flipped = false;
    break;

  default:
    break;
  }

  for (unsigned int j = i; j < N_KEY_REPORT; j++)
  {
    keyReport[j] = KEY_NONE;
  }
}

void setupDigitalIO()
{
  pinMode(BUTTON_UP, INPUT_PULLDOWN);
  pinMode(BUTTON_DOWN, INPUT_PULLDOWN);
  pinMode(BUTTON_LEFT, INPUT_PULLDOWN);
  pinMode(BUTTON_RIGHT, INPUT_PULLDOWN);
  pinMode(BUTTON_CENTER, INPUT_PULLDOWN);
  pinMode(BUTTON_A, INPUT_PULLDOWN);
  pinMode(BUTTON_B, INPUT_PULLDOWN);
  pinMode(BUTTON_C, INPUT_PULLDOWN);

  pinMode(RGB_LED_RED, OUTPUT);
  pinMode(RGB_LED_BLUE, OUTPUT);
  pinMode(RGB_LED_GREEN, OUTPUT);
  pinMode(USER_LED_PIN, OUTPUT);
  setUserLED(false);
}

bool writeSettings()
{
  if (DEBUG)
    Serial.println("Writing settings to NVS...");

  if (preferences.begin(SETTINGS_NAMESPACE, false))
  {
    preferences.putUChar("mode", (uint8_t)currentMode);
    preferences.putUChar("orient", buttonOrientation);
    preferences.putUChar("bright", (uint8_t)LEDbrightness);
    preferences.end();
    return true;
  }
  else
  {
    if (DEBUG)
      Serial.println("Error opening NVS for writing!");
    return false;
  }
}

// return true for error
bool readSettings()
{
  if (!preferences.begin(SETTINGS_NAMESPACE, true))
  {
    if (DEBUG)
      Serial.println("Error opening NVS for reading.");
    return true;
  }

  if (!preferences.isKey("mode") || !preferences.isKey("orient") || !preferences.isKey("bright"))
  {
    preferences.end();
    if (DEBUG)
      Serial.println("Settings not found.");
    return true;
  }

  uint8_t savedMode = preferences.getUChar("mode", (uint8_t)DEFAULT_MODE);
  uint8_t savedOrientation = preferences.getUChar("orient", DEFAULT_BUTTON_MAP);
  uint8_t savedBrightness = preferences.getUChar("bright", 0);
  preferences.end();

  if (savedOrientation > 3)
  {
    if (DEBUG)
      Serial.println("Invalid orientation value, restoring defaults.");
    applyDefaultSettings();
    return true;
  }

  switch (savedMode)
  {
  case 0:
  case 1:
    currentMode = DMD2;
    break;
  case 2:
    currentMode = OsmAnd;
    break;
  case 3:
    currentMode = MEDIA;
    break;
  default:
    applyDefaultSettings();
    return true;
  }

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

  return false; // no error
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

    Serial.println("MotoButtons 2 BLE Controller");
    Serial.println("-----------------------------\n");
    Serial.println();
  }

  bleKeyboard.setTxPower(BLE_TX_POWER);
  if (DEBUG)
    bleKeyboard.setLogLevel(HIDLogLevel::Normal);
  bleKeyboard.begin();

  if (DEBUG)
    Serial.println("Setup complete.");
  setRGBColor(SETUP_COMPLETE_COLOR);

  // let the user change the orientation of the device at startup
  int buttonMap = getButtonMapSelection();
  if (buttonMap >= 0)
  {
    buttonOrientation = (uint8_t)buttonMap;
    setButtonMapping(buttonOrientation);

    if (DEBUG)
    {
      Serial.print("Button orientation changed to: ");
      Serial.println(buttonOrientation);
    }

    // Indicate new button mode selection
    flashLED(BUTTON_ORIENTATION_COLOR, 250, 250 * (((uint8_t)buttonOrientation) + 1));
  }
  else
  {
    if (DEBUG)
      Serial.println("Button orientation not changed.");
  }

  // Restore settings
  bool success = false;
  success = readSettings();
  // new selection for buttonMap/orientation gets overwritten by readSettings
  if (buttonMap >= 0)
  {
    buttonOrientation = (uint8_t)buttonMap;
    setButtonMapping(buttonOrientation);
  }
  if (!success || buttonMap >= 0)
    writeSettings();

  indicateMode(currentMode);
}

void loop()
{
  // Indicate whether the device is connected and running
  if (!BLE_connected && bleKeyboard.isConnected())
  {
    if (DEBUG)
      Serial.println("BLE connected to host.");
    showMode(currentMode);
    BLE_connected = true;
  }
  else if (!bleKeyboard.isConnected())
  {
    RGBToggle(BLE_COLOR);
    delay(200);

    BLE_connected = false;
  }

  // Read current state of buttons
  updateButtons();

  if (bleKeyboard.isPaired())
  {
    bool osmandRepeatSend = false;
    if (hasOsmAndRepeatableHold() && !keyReportChanged && !forceKeyReport &&
        (millis() - lastOsmAndRepeatTime >= OSMAND_REPEAT_INTERVAL_MS))
    {
      osmandRepeatSend = true;
    }

    // Compile the BLE HID key report
    if (keyReportChanged || forceKeyReport || osmandRepeatSend)
    {
      if (osmandRepeatSend)
      {
        releaseAllKeys();
        delay(5);
      }
      if (forceKeyReport) {
        forceKeyReport = false;
        if (DEBUG)
          Serial.println("Key report forced.");
      }
      mapButtonsToKeyReport();
      sendKeyboardReport();
      lastOsmAndRepeatTime = millis();
      if (DEBUG)
        Serial.println("Key report sent.");
    }
    else if (!hasOsmAndRepeatableHold())
    {
      lastOsmAndRepeatTime = millis();
    }

  }
}
