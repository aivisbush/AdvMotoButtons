#include "settings.h"
#include "debug.h"
#include <Preferences.h>

static const char SETTINGS_NAMESPACE[] = "motobuttons";
static const char KEY_SCHEMA[] = "ver";
static const char KEY_MODE[] = "mode";
static const char KEY_ORIENTATION[] = "orient";
static const char KEY_OLED_ENABLED[] = "oled";
static const char KEY_OLED_CONTRAST[] = "contrast";

// Schema 1 had mode, orient and oled with no version key.
// Schema 2 added the version key and the OLED contrast.
static const uint8_t SETTINGS_SCHEMA_VERSION = 2;

Settings settings;

static bool isValidMode(uint8_t mode)
{
  return mode >= (uint8_t)Mode::DMD2 && mode <= (uint8_t)Mode::Media;
}

void settingsApplyDefaults()
{
  settings.mode = DEFAULT_MODE;
  settings.orientation = DEFAULT_ORIENTATION;
  settings.oledEnabled = true;
  settings.oledContrast = OLED_DEFAULT_CONTRAST;
}

bool settingsLoad()
{
  settingsApplyDefaults();

  Preferences preferences;
  // Read-only open fails when the namespace has never been written.
  if (!preferences.begin(SETTINGS_NAMESPACE, true))
  {
    debugPrintln("No saved settings; writing defaults.");
    settingsSave();
    return false;
  }

  bool hasCoreKeys = preferences.isKey(KEY_MODE) && preferences.isKey(KEY_ORIENTATION) &&
                     preferences.isKey(KEY_OLED_ENABLED);
  uint8_t schema = preferences.getUChar(KEY_SCHEMA, hasCoreKeys ? 1 : 0);
  uint8_t savedMode = preferences.getUChar(KEY_MODE, (uint8_t)DEFAULT_MODE);
  uint8_t savedOrientation = preferences.getUChar(KEY_ORIENTATION, DEFAULT_ORIENTATION);
  bool savedOledEnabled = preferences.getBool(KEY_OLED_ENABLED, true);
  uint8_t savedContrast = preferences.getUChar(KEY_OLED_CONTRAST, OLED_DEFAULT_CONTRAST);
  preferences.end();

  if (schema == 0 || schema > SETTINGS_SCHEMA_VERSION || !hasCoreKeys ||
      !isValidMode(savedMode) || savedOrientation >= ORIENTATION_COUNT)
  {
    debugPrintf("Settings missing or invalid (schema %u); restoring defaults.\n", schema);
    settingsSave();
    return false;
  }

  settings.mode = (Mode)savedMode;
  settings.orientation = savedOrientation;
  settings.oledEnabled = savedOledEnabled;
  settings.oledContrast = savedContrast;

  if (schema < SETTINGS_SCHEMA_VERSION)
  {
    debugPrintf("Migrating settings from schema %u to %u.\n", schema, SETTINGS_SCHEMA_VERSION);
    settingsSave();
  }
  return true;
}

bool settingsSave()
{
  Preferences preferences;
  if (!preferences.begin(SETTINGS_NAMESPACE, false))
  {
    debugPrintln("Failed to open settings for writing.");
    return false;
  }

  // NVS skips the flash write when a value is unchanged, so saving often is cheap.
  bool success = preferences.putUChar(KEY_SCHEMA, SETTINGS_SCHEMA_VERSION) == sizeof(uint8_t) &&
                 preferences.putUChar(KEY_MODE, (uint8_t)settings.mode) == sizeof(uint8_t) &&
                 preferences.putUChar(KEY_ORIENTATION, settings.orientation) == sizeof(uint8_t) &&
                 preferences.putBool(KEY_OLED_ENABLED, settings.oledEnabled) == sizeof(uint8_t) &&
                 preferences.putUChar(KEY_OLED_CONTRAST, settings.oledContrast) == sizeof(uint8_t);
  preferences.end();

  debugPrintln(success ? "Settings saved." : "Failed to save settings.");
  return success;
}

bool settingsClear()
{
  Preferences preferences;
  if (!preferences.begin(SETTINGS_NAMESPACE, false))
    return false;

  bool cleared = preferences.clear();
  preferences.end();
  return cleared;
}
