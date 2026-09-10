#include "chords.h"
#include "config.h"
#include "inputs.h"
#include "settings.h"
#include "keymap.h"
#include "oled.h"
#include "ble_hid.h"
#include "debug.h"

// Set when a chord fires; cleared once A, B and C are all released, so
// the chord's buttons never leak as keys on the way up.
static bool chordLatched = false;
// Seconds value last shown by a countdown, -1 when none is on screen.
static int8_t countdownShown = -1;

static unsigned long minHeld(unsigned long first, unsigned long second)
{
  return first < second ? first : second;
}

// Shows "<label> Ns" while a 5 s chord is being held.
static void showCountdown(const char *label, unsigned long heldMs)
{
  unsigned long remainingMs = heldMs >= MODE_RESET_MS ? 0 : MODE_RESET_MS - heldMs;
  int8_t seconds = (int8_t)((remainingMs + 999) / 1000);
  if (seconds == countdownShown)
    return;

  countdownShown = seconds;
  char text[24];
  snprintf(text, sizeof(text), "%s %ds", label, seconds);
  oledShowTransient(text, OLED_CHORD_FEEDBACK_MS, OLED_PRIORITY_HIGH);
}

static void clearCountdown()
{
  if (countdownShown < 0)
    return;
  countdownShown = -1;
  oledClearTransient();
}

static void factoryReset()
{
  debugPrintln("Clearing settings and BLE bonds...");
  bool settingsCleared = settingsClear();
  bool bondsCleared = bleClearBonds();
  debugPrintf("Settings cleared: %d, BLE bonds cleared: %d\n", (int)settingsCleared, (int)bondsCleared);

  oledShowTransient("Reset done", OLED_TRANSIENT_MS, OLED_PRIORITY_HIGH);
  delay(OLED_TRANSIENT_MS);
  ESP.restart();
}

static void restartController()
{
  debugPrintln("Restarting controller...");
  oledShowTransient("Restarting", OLED_TRANSIENT_MS, OLED_PRIORITY_HIGH);
  delay(100);
  ESP.restart();
}

static void cycleMode(bool connected)
{
  settings.mode = getNextMode(settings.mode);
  debugPrintf("Mode advanced to %s\n", getModeName(settings.mode));
  keymapReleaseAll();
  settingsSave();
  // The mode screen shows the name when connected; say it anyway otherwise.
  if (!connected)
    oledShowTransient(getModeName(settings.mode), OLED_TRANSIENT_MS, OLED_PRIORITY_NORMAL);
}

static void toggleDisplay()
{
  settings.oledEnabled = !settings.oledEnabled;
  keymapReleaseAll();
  oledSetEnabled(settings.oledEnabled);
  settingsSave();
  debugPrintf("OLED %s\n", settings.oledEnabled ? "enabled" : "disabled");
}

void chordsUpdate(bool connected)
{
  bool a = buttonHeld(BUTTON_A);
  bool b = buttonHeld(BUTTON_B);
  bool c = buttonHeld(BUTTON_C);

  if (chordLatched)
  {
    if (!a && !b && !c)
      chordLatched = false;
    return;
  }

  // Factory reset outranks every smaller chord.
  if (a && b && c)
  {
    unsigned long heldMs = minHeld(minHeld(buttonHeldMs(BUTTON_A), buttonHeldMs(BUTTON_B)), buttonHeldMs(BUTTON_C));
    showCountdown("Reset", heldMs);
    if (heldMs >= MODE_RESET_MS)
      factoryReset();
    return;
  }

  if (a && c)
  {
    unsigned long heldMs = minHeld(buttonHeldMs(BUTTON_A), buttonHeldMs(BUTTON_C));
    showCountdown("Restart", heldMs);
    if (heldMs >= MODE_RESET_MS)
      restartController();
    return;
  }

  clearCountdown();

  if (b && c)
  {
    if (minHeld(buttonHeldMs(BUTTON_B), buttonHeldMs(BUTTON_C)) >= MODE_TOGGLE_MS)
    {
      chordLatched = true;
      cycleMode(connected);
    }
    return;
  }

  if (a && b)
  {
    if (minHeld(buttonHeldMs(BUTTON_A), buttonHeldMs(BUTTON_B)) >= MODE_TOGGLE_MS)
    {
      chordLatched = true;
      toggleDisplay();
    }
    return;
  }
}

bool chordsSuppressButtons()
{
  return chordLatched;
}
