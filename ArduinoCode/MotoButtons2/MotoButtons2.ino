/*********************************************************************
License: GNU GENERAL PUBLIC LICENSE; Version 3, 29 June 2007
MotoButtons 2 - handlebar BLE HID controller for motorcycle navigation.
Device: ESP32-C3 OLED Mini. Modes: DMD2, OsmAnd, Media.

This file holds setup() and loop() plus the board-level odds and ends
(status LED, boot orientation window, watchdog). Everything else is in
the units listed in config.h.
*********************************************************************/
#include <Arduino.h>
#include <esp_system.h>
#include <esp_task_wdt.h>

#include "config.h"
#include "debug.h"
#include "settings.h"
#include "inputs.h"
#include "keymap.h"
#include "chords.h"
#include "oled.h"
#include "ble_hid.h"

// Prototypes, so setup() and loop() can come first.
static void statusLedBegin();
static void statusLedUpdate(bool connected);
static void runOrientationWindow();
static void watchdogBegin();
static void logBootSummary();

void setup()
{
  if (DEBUG)
    Serial.begin(115200);

  settingsLoad();
  statusLedBegin();
  inputsBegin(settings.orientation);
  oledBegin(settings.oledEnabled, settings.oledContrast);
  oledShowBootScreen();

  // Advertise first, so the phone reconnects while the boot screens run.
  bleBegin();
  runOrientationWindow();

  logBootSummary();
  if (DEBUG_INPUTS)
    inputsLogHeader();

  watchdogBegin();
  oledUpdate(bleConnected(), getModeName(settings.mode), true);
}

void loop()
{
  static bool wasConnected = false;

  esp_task_wdt_reset();

  bool connected = bleConnected();
  if (connected != wasConnected)
  {
    wasConnected = connected;
    if (connected)
    {
      debugPrintln("BLE connected to host.");
      oledOnConnected();
    }
    else
    {
      debugPrintln("BLE disconnected.");
    }
  }

  bleUpdate();
  statusLedUpdate(connected);
  inputsUpdate();
  if (DEBUG_INPUTS)
    inputsDiagnosticsTick();
  chordsUpdate(connected);
  keymapUpdate(connected, settings.mode);
  oledUpdate(connected, getModeName(settings.mode));

  delay(LOOP_TICK_MS);
}

/*---------------------------- status LED ----------------------------*/
static void setStatusLed(uint8_t brightness)
{
  analogWrite(PIN_STATUS_LED, STATUS_LED_ACTIVE_LOW ? 255 - brightness : brightness);
}

static void statusLedBegin()
{
  pinMode(PIN_STATUS_LED, OUTPUT);
  setStatusLed(0);
}

// Perceived brightness follows roughly the square of the duty cycle.
static uint8_t gammaCorrect(uint8_t linear)
{
  uint16_t squared = (uint16_t)linear * linear;
  return (uint8_t)((squared + 127) / 255);
}

// Breathing while waiting for a phone, dim and steady once connected.
static void statusLedUpdate(bool connected)
{
  static int16_t lastBrightness = -1;

  uint8_t brightness;
  if (connected)
  {
    brightness = STATUS_LED_CONNECTED_BRIGHTNESS;
  }
  else
  {
    uint16_t phase = millis() % STATUS_LED_PULSE_PERIOD_MS;
    uint16_t halfPeriod = STATUS_LED_PULSE_PERIOD_MS / 2;
    uint16_t rise = phase < halfPeriod ? phase : STATUS_LED_PULSE_PERIOD_MS - phase;
    brightness = gammaCorrect((uint8_t)((uint32_t)rise * 255 / halfPeriod));
  }

  if (brightness != lastBrightness)
  {
    setStatusLed(brightness);
    lastBrightness = brightness;
  }
}

/*------------------------ orientation window ------------------------*/
/* Whichever joystick direction is held steadily during the first seconds
 * after boot becomes UP. The direction is pressed after the chip has
 * booted, so the strapping pins on RIGHT (GPIO2) and B (GPIO9) are never
 * held through power-up. Directions held from before power-up still
 * count, since they are already down when the window opens.
 */
static void runOrientationWindow()
{
  unsigned long windowStartMs = millis();
  int8_t candidate = -1;
  unsigned long candidateSinceMs = 0;

  while (millis() - windowStartMs < ORIENTATION_WINDOW_MS)
  {
    inputsUpdate();

    int8_t held = inputsHeldDirection();
    if (held != candidate)
    {
      candidate = held;
      candidateSinceMs = millis();
    }

    if (candidate >= 0 && millis() - candidateSinceMs >= ORIENTATION_HOLD_MS)
    {
      uint8_t pin = buttons[candidate].pin;
      int8_t orientation = orientationForUpPin(pin);
      if (orientation >= 0)
      {
        bool changed = (uint8_t)orientation != settings.orientation;
        settings.orientation = (uint8_t)orientation;
        inputsSetOrientation(settings.orientation);
        if (changed)
          settingsSave();

        char message[40];
        snprintf(message, sizeof(message), "Orientation\nis set\nUP is %s", joystickPinName(pin));
        oledShowTransient(message, OLED_ORIENTATION_MESSAGE_MS, OLED_PRIORITY_HIGH);
        debugPrintf("Boot orientation set to map %d; UP is %s%s\n", orientation, joystickPinName(pin),
                    changed ? "" : " (unchanged)");
      }
      return;
    }

    delay(LOOP_TICK_MS);
  }
}

/*----------------------------- watchdog -----------------------------*/
/* The IDF starts the task watchdog itself; this widens its timeout and
 * subscribes the loop task, so a hung loop resets the controller instead
 * of leaving a dead handlebar. loop() feeds it on every pass.
 */
static void watchdogBegin()
{
  esp_task_wdt_config_t config = {};
  config.timeout_ms = WATCHDOG_TIMEOUT_MS;
  config.idle_core_mask = 0;
  config.trigger_panic = true;

  esp_err_t configured = esp_task_wdt_reconfigure(&config);
  if (configured == ESP_ERR_INVALID_STATE)
    configured = esp_task_wdt_init(&config);
  esp_err_t subscribed = esp_task_wdt_add(NULL);
  debugPrintf("Watchdog %lu ms: configure %d, subscribe %d\n", (unsigned long)WATCHDOG_TIMEOUT_MS,
              (int)configured, (int)subscribed);
}

/*---------------------------- boot summary --------------------------*/
static const char *resetReasonName(esp_reset_reason_t reason)
{
  switch (reason)
  {
  case ESP_RST_POWERON:
    return "power-on";
  case ESP_RST_EXT:
    return "external pin";
  case ESP_RST_SW:
    return "software restart";
  case ESP_RST_PANIC:
    return "panic";
  case ESP_RST_INT_WDT:
    return "interrupt watchdog";
  case ESP_RST_TASK_WDT:
    return "task watchdog";
  case ESP_RST_WDT:
    return "other watchdog";
  case ESP_RST_DEEPSLEEP:
    return "deep sleep wake";
  case ESP_RST_BROWNOUT:
    return "brownout";
  default:
    return "unknown";
  }
}

static void logBootSummary()
{
  debugPrintf("\nMotoButtons 2 v%s (ESP32-C3), BLE name \"%s\"\n", FIRMWARE_VERSION, BLE_DEVICE_NAME);
  debugPrintf("Reset reason: %s\n", resetReasonName(esp_reset_reason()));
  debugPrintf("Mode %s, orientation %u, OLED %s, contrast %u\n", getModeName(settings.mode),
              settings.orientation, settings.oledEnabled ? "on" : "off", settings.oledContrast);
}
