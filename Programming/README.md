## Upload the Microcontroller Software

1. Install the current [Arduino IDE](https://www.arduino.cc/en/software).
2. Open **Tools > Board > Boards Manager**, search for `esp32`, and install **esp32 by Espressif Systems** version 3.3.8, the version the firmware is tested with. If no esp32 entry appears, add `https://espressif.github.io/arduino-esp32/package_esp32_index.json` under **File > Preferences > Additional boards manager URLs** first.
3. Open **Tools > Manage Libraries** and install **NimBLE-Arduino** by h2zero (2.5.0) and **U8g2** by olikraus (2.36.19). These two are not part of the board package; `Preferences` and `Wire` are.
4. Open [MotoButtons2.ino](../ArduinoCode/MotoButtons2/MotoButtons2.ino). The other files in the folder (`config.h`, `inputs.cpp` and so on) open as tabs alongside it.
5. Select **Tools > Board > esp32 > XIAO_ESP32C3**. The OLED Mini has no profile of its own; this one matches its chip and USB serial setup.
6. Connect the controller over USB-C, select its port, and click **Upload**.

Everything you might want to tune - pins, timings, key codes, the Bluetooth name, the debug switches - is in [config.h](../ArduinoCode/MotoButtons2/config.h). The key tables are in [keymap.cpp](../ArduinoCode/MotoButtons2/keymap.cpp).

## Command Line and CI

[sketch.yaml](../ArduinoCode/MotoButtons2/sketch.yaml) is an arduino-cli profile that pins the core and library versions, so the firmware builds the same way anywhere:

```
arduino-cli compile --profile xiao_esp32c3 ArduinoCode/MotoButtons2
```

Arduino IDE 2 ships its own arduino-cli at `<IDE folder>\resources\app\lib\backend\resources\arduino-cli.exe`. Pointed at the IDE's configuration it reuses the cores and libraries the IDE already has, which is the quickest way to compile-check a change without opening the IDE:

```
"C:\Program Files\Arduino IDE\resources\app\lib\backend\resources\arduino-cli.exe" compile --config-file "%USERPROFILE%\.arduinoIDE\arduino-cli.yaml" --fqbn esp32:esp32:XIAO_ESP32C3 ArduinoCode/MotoButtons2
```

A GitHub Actions workflow ([compile.yml](../.github/workflows/compile.yml)) compiles the sketch on every push that touches it.

## If Uploading Fails

The ESP32-C3 uses its ROM download mode rather than the nRF52 DFU procedure. Hold the board's **BOOT** button, tap **RESET**, release **BOOT**, select the newly detected serial port, and upload again.

Do not hold joystick **RIGHT** (GPIO2) or button **B** (GPIO9) while powering the controller on: both are ESP32-C3 strapping pins. RIGHT held low stops the chip from booting, B held low puts it into download mode. Set the joystick orientation by pressing the direction *after* power is on, while the boot screen is showing.
