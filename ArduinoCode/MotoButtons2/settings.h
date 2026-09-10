/* Persistent settings, kept in NVS through the Preferences library under
 * the "motobuttons" namespace. The record carries a schema version so a
 * future layout change can migrate instead of resetting.
 */
#pragma once

#include <Arduino.h>
#include "config.h"

struct Settings
{
  Mode mode;
  uint8_t orientation;
  bool oledEnabled;
  uint8_t oledContrast;
};

extern Settings settings;

void settingsApplyDefaults();
// Loads the saved record, migrating or falling back to defaults as needed.
// Returns true only when a valid record was found.
bool settingsLoad();
bool settingsSave();
// Erases the namespace. Bonds are cleared separately by the BLE unit.
bool settingsClear();
