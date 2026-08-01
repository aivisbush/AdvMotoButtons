## Upload the Microcontroller Software

1. Install the current [Arduino IDE](https://www.arduino.cc/en/software).
2. Open **Tools > Board > Boards Manager**, search for `esp32`, and install **esp32 by Espressif Systems**. The firmware is tested with version 3.3.8.
3. Open [MotoButtons2.ino](../ArduinoCode/MotoButtons2/MotoButtons2.ino).
4. Select **Tools > Board > esp32 > XIAO_ESP32C3**.
5. Connect the controller over USB-C, select its port, and click **Upload**.

The firmware uses only libraries included with the Espressif ESP32 board package; no separate BLE keyboard library is needed.

## If Uploading Fails

The ESP32-C3 uses its ROM download mode rather than the nRF52 DFU procedure. Hold the XIAO **BOOT** button, tap **RESET**, release **BOOT**, select the newly detected serial port, and upload again. Avoid holding the controller's DOWN or CENTER inputs while powering it on because their GPIOs are ESP32-C3 strapping pins.
