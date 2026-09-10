/* The 72x40 status display.
 *
 * Screens run in a fixed order: boot screen -> orientation message if a
 * direction was held -> "Connecting" until a phone connects -> "Connected"
 * -> "READY / TO >> / RACE" splash, once per boot -> mode screen with the
 * button overlay. Transient messages carry a priority so a low-priority
 * one cannot cut short a high-priority one. The panel dims after a quiet
 * spell and blanks when disconnected and idle, to spare the OLED.
 */
#pragma once

#include <Arduino.h>

constexpr uint8_t OLED_PRIORITY_NORMAL = 0;
constexpr uint8_t OLED_PRIORITY_HIGH = 1;

void oledBegin(bool enabled, uint8_t contrast);
void oledSetEnabled(bool enabled);
void oledShowBootScreen();
// Shows text for durationMs unless a higher-priority transient is up.
// font = nullptr picks a size automatically.
void oledShowTransient(const char *text, uint16_t durationMs, uint8_t priority, const uint8_t *font = nullptr);
void oledClearTransient();
// Call on the connect edge: queues the "Connected" message and the splash.
void oledOnConnected();
// Renders whatever the current state calls for; call every loop.
void oledUpdate(bool connected, const char *modeName, bool force = false);
