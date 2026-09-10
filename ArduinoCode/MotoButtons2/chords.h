/* Button combinations. All of them are among A, B and C:
 *   B+C  held 1 s   next mode (DMD2 -> OsmAnd -> Media)
 *   A+B  held 1 s   OLED on/off
 *   A+C  held 5 s   restart the controller
 *   A+B+C held 5 s  factory reset: settings and Bluetooth bonds
 * The 5 s chords count down on the OLED while held.
 */
#pragma once

#include <Arduino.h>

void chordsUpdate(bool connected);
// True from the moment a chord fires until all of A, B and C are released.
bool chordsSuppressButtons();
