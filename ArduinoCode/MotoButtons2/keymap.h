/* Per-mode key tables and the HID report engine.
 *
 * Every loop the engine works out which inputs are "active" (held, with
 * opposite directions cancelled, chords excluded and the A/B/C grace
 * period applied), builds the keyboard report those imply and sends it
 * only when it differs from the last one the phone received. Firmware
 * repeat and edge-triggered keys sit on top of that.
 */
#pragma once

#include <Arduino.h>
#include "config.h"

const char *getModeName(Mode mode);
Mode getNextMode(Mode mode);

// Builds and sends whatever reports the current inputs call for.
void keymapUpdate(bool connected, Mode mode);
// Sends an empty keyboard report if anything is down and forgets edges.
void keymapReleaseAll();
