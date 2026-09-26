/*********************************************************************
License: GNU GENERAL PUBLIC LICENSE; Version 3, 29 June 2007
MotoButtons 2 - handlebar BLE HID controller for motorcycle navigation.
Devices: ESP32-C3 OLED Mini, XIAO ESP32C3, XIAO ESP32C6 (board_*.h).
Modes: DMD2, OsmAnd, Locus, Media.

This file holds setup() and loop() plus the board-level odds and ends
(boot orientation window, watchdog). Everything else is in
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
#include "led.h"
#include "ble_hid.h"
#include "ota.h"

// Prototypes, so setup() and loop() can come first.
static void runOrientationWindow();
static void watchdogBegin();
static void logBootSummary();

void setup()
{
  if (DEBUG)
    Serial.begin(115200);

  settingsLoad();
  ledBegin();
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
  ledSetEnabled(settings.oledEnabled);
  ledShowMode();
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
  otaUpdate();
  ledUpdate(connected, settings.mode);
  inputsUpdate();
  if (DEBUG_INPUTS)
    inputsDiagnosticsTick();
  chordsUpdate(connected);
  keymapUpdate(connected, settings.mode);
  oledUpdate(connected, getModeName(settings.mode));

  delay(LOOP_TICK_MS);
}

/*------------------------ orientation window ------------------------*/
/* Whichever joystick direction is held steadily during the first seconds
 * after boot becomes UP. The direction is pressed after the chip has
 * booted, so strapping pins are never held through power-up. Directions held from before power-up still
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
        ledFlash((uint8_t)(orientation + 1));
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
  debugPrintf("\nMotoButtons 2 v%s (%s), BLE name \"%s\"\n", FIRMWARE_VERSION, BOARD_NAME, BLE_DEVICE_NAME);
  debugPrintf("Reset reason: %s\n", resetReasonName(esp_reset_reason()));
  debugPrintf("Mode %s, orientation %u, OLED %s, contrast %u\n", getModeName(settings.mode),
              settings.orientation, settings.oledEnabled ? "on" : "off", settings.oledContrast);
}
