/* Firmware update over Bluetooth.
 *
 * A bonded phone writes the new firmware image to the inactive app
 * partition through a custom GATT service, then the controller verifies it
 * (size and MD5) and restarts into it. Protocol, all little endian:
 *   control write  0x01 START  size(u32) md5(16 bytes)
 *                  0x02 FINISH
 *                  0x03 ABORT
 *   data write     the next image bytes, in order, written with response
 *   control notify event(u8) status(u8) value(u32), see OtaEvent/OtaStatus
 */
#pragma once

#include <Arduino.h>

class NimBLEServer;

// Adds the update service; call before advertising starts.
void otaCreateService(NimBLEServer *server);
// Abandons an unfinished update when the phone disconnects.
void otaOnDisconnect();
// Progress on the OLED and the restart after a good image; call every loop.
void otaUpdate();
bool otaActive();
