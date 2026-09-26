/* The board's LED. The OLED board has one status LED: it breathes while
 * waiting for a phone and glows dimly once connected. Boards without an
 * OLED have an RGB LED that also shows the mode and confirms actions.
 */
#pragma once

#include <Arduino.h>
#include "config.h"

void ledBegin();
// Call every loop.
void ledUpdate(bool connected, Mode mode);
// RGB only: one long blink in the new mode's colour.
void ledShowMode();
// RGB only: count orange flashes (orientation set, reset).
void ledFlash(uint8_t count);
// RGB only: false leaves the LED dark once connected.
void ledSetEnabled(bool on);
// Keeps the LED running for ms, for a blocking moment such as a reset.
void ledWait(uint16_t ms);
