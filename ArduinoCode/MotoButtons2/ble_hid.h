/* Bluetooth HID: a keyboard report (six keys, no modifiers) plus a
 * consumer-control report for media keys. Bonding with Just Works
 * pairing; once bonded, advertising is restricted to bonded phones, see
 * BLE_WHITELIST_BONDED in config.h.
 */
#pragma once

#include <Arduino.h>
#include "config.h"

void bleBegin();
// Housekeeping for the whitelist fallback; call every loop.
void bleUpdate();
bool bleConnected();
void bleSendKeyboardReport(const uint8_t keys[KEY_REPORT_SIZE]);
// Press and release of one consumer usage.
void bleSendConsumerPulse(uint16_t usage);
bool bleClearBonds();
