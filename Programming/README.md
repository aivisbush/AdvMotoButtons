## Upload the Microcontroller Software

1. Install the current [Arduino IDE](https://www.arduino.cc/en/software).
2. Open **Tools > Board > Boards Manager**, search for `esp32`, and install **esp32 by Espressif Systems**. The firmware is tested with version 3.3.8.
3. Open **Tools > Manage Libraries**, search for `NimBLE-Arduino`, and install **NimBLE-Arduino by h2zero**. The firmware is tested with version 2.5.0.
4. [Download the source code file](../ArduinoCode/MotoButtons2/MotoButtons2.ino) (keep it inside a folder named `MotoButtons2`, the Arduino IDE requires the folder name to match) and open it in the Arduino IDE.
5. Select **Tools > Board > esp32 > XIAO_ESP32C3**.
6. Connect the controller over USB-C, select its port, and click **Upload**.

Saved settings (mode, orientation, LED brightness) and Bluetooth bonds survive an upload, so the phone does not need to be paired again after updating the firmware.

## If Uploading Fails

The Arduino IDE normally resets the ESP32-C3 into its download mode by itself. If the upload still fails, hold the XIAO **BOOT** button, tap **RESET**, release **BOOT**, select the newly detected serial port, and upload again. The board is small and the buttons are next to the USB-C connector, so this requires opening the case.

Do not hold the controller's DOWN or CENTER inputs while powering it on or uploading: their GPIOs (D8, D9) are ESP32-C3 boot strapping pins.

## Updating the Software for Future Releases
To update the software, open the case, connect the USB-C port to your computer, open the new source code file in the Arduino IDE and click **Upload**. The COM port may change after a manual reset into download mode, so check the port selection in the toolbar before uploading.
