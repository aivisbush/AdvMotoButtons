# MotoButtons 2 - project context

Handlebar BLE HID controller for motorcycle navigation. A board with a 5-way joystick and three
buttons presents itself as a Bluetooth keyboard and drives DMD2, OsmAnd, Locus Map or a media player.

Fork of [joncox123/MotoButtons2](https://github.com/joncox123/MotoButtons2). One branch holds every
board: work happens on `ota` (to be merged to `main`); the old per-chip branches (`esp32`, `esp32c3`,
`esp32c3OLED`, `nRF52840`) are to be archived. Boards and sketches:

| Board id | Board | Sketch | Profile | Product ID |
|---|---|---|---|---|
| `esp32c3-oled` | ESP32-C3 OLED Mini (72x40 OLED) | `ArduinoCode/MotoButtons2` | `xiao_esp32c3` | 0x4001 |
| `esp32c3` | Seeed XIAO ESP32C3 (RGB LED) | `ArduinoCode/MotoButtons2` + `-DMB_BOARD_ESP32C3` | `xiao_esp32c3` | 0x4002 |
| `esp32c6` | Seeed XIAO ESP32C6 (RGB LED) | `ArduinoCode/MotoButtons2` | `xiao_esp32c6` | 0x4003 |
| `nrf52840` | Seeed XIAO nRF52840 (RGB LED) | `ArduinoCode/MotoButtonsNRF` | `nrf52840` | 0x4004 |

`release.json` lists the boards for CI and releases (sketch, profile, optional compiler `flags`, `ota`).

**ESP32 board selection.** `config.h` includes one `board_*.h`: `board_esp32c6.h` when the board
setting is XIAO_ESP32C6 (`ARDUINO_XIAO_ESP32C6`), `board_esp32c3.h` when `MB_BOARD_ESP32C3` is
defined (a commented `#define` in `config.h` for the IDE, `compiler.cpp.extra_flags` in CI; both C3
boards share the XIAO_ESP32C3 setting, so nothing else tells them apart), else
`board_esp32c3_oled.h`. Each header holds board name, `BOARD_ID_TAG`, `BOARD_PRODUCT_ID`, pins,
input polarity (`BUTTONS_ACTIVE_LOW`, `CENTER_ACTIVE_LOW`), `JOYSTICK_ADC`, `BOARD_HAS_OLED`,
`BOARD_HAS_RGB_LED`; unused pins are `PIN_NONE`, so every constant exists on every board. A
`CONFIG_IDF_TARGET_*` check stops a header being built for the wrong chip. arduino-cli profiles cannot
carry build flags, hence the `flags` field. The C3 and C6 joystick names are the directions in the
default orientation, taken from the old branches' tables.

## Layout

The ESP32 sketch lives in `ArduinoCode/MotoButtons2/` as one `.ino` plus units the Arduino IDE
compiles as tabs:

| File | What |
|---|---|
| `MotoButtons2.ino` | `setup()`/`loop()`, boot orientation window, watchdog, boot log |
| `config.h` | board selection and every tunable: timings, thresholds, HID codes, BLE name, `DEBUG` switches, `FIRMWARE_VERSION` |
| `board_*.h` | per-board identity, pins, input polarity and indicators |
| `inputs.h/.cpp` | `Button` struct array, ADC or digital reading, debounce, orientation table, diagnostics |
| `keymap.h/.cpp` | per-mode key tables and the HID report engine |
| `chords.h/.cpp` | button combinations: mode, display, restart, factory reset |
| `oled.h/.cpp` | display state machine, rendering, dim/blank; no-op without `BOARD_HAS_OLED` |
| `led.h/.cpp` | single status LED (breathing/dim) or RGB LED (blue blink waiting, mode colour, flashes) |
| `ble_hid.h/.cpp` | NimBLE HID device, bonded-phone whitelist |
| `ota.h/.cpp` | firmware update over BLE: custom GATT service, `Update` library, restart |
| `settings.h/.cpp` | Preferences record with a schema version |
| `debug.h` | `debugPrintf()` / `debugPrintln()`, compiled out when `DEBUG` is false |
| `sketch.yaml` | arduino-cli profiles (one per chip) pinning core and library versions |

The nRF52840 sketch `ArduinoCode/MotoButtonsNRF/MotoButtonsNRF.ino` is a single file (Bluefruit, the
2.1 code from the `nRF52840` branch): RGB LED instead of an OLED, digital inputs with pull-downs
(active high), settings in InternalFS, custom `BLEHidGeneric` descriptor with only the modes' keys,
generic HID appearance. It reports `FIRMWARE_VERSION` (0x2A26) and `BOARD_PRODUCT_ID` (PnP ID) like
the ESP32 build but has **no Bluetooth update service yet**, so the phone app does not list it.

Elsewhere: `.github/workflows/compile.yml` compiles every board in `release.json` on every push, `Docs/` holds reference notes
(read them before touching input handling or DMD2 behaviour), `Programming/README.md` is the
flashing guide, `Wiring/` still shows the original nRF52840 drawing hand-annotated for C3 pins,
`3D/` and `Design/` are case and photos. `Apps/Android` is the phone updater app (Java, no AndroidX,
Gradle 8.9 / AGP 8.6.1, build with `JAVA_HOME` = Android Studio's `jbr` and `gradlew assembleDebug`).

## Building

ESP32 boards: Arduino IDE, board **XIAO_ESP32C3** (C3 and C3 OLED) or **XIAO_ESP32C6**, esp32 core **3.3.8**, plus two Library Manager
libraries that are *not* part of the core: **NimBLE-Arduino 2.5.0** and **U8g2 2.36.19**.
`Preferences` and `Wire` ship with the core. nRF52840: board **Seeed XIAO nRF52840 Sense**, Seeed
nrf52 core **1.1.12** (Bluefruit and TinyUSB ship with it). Each sketch's `sketch.yaml` pins these.

**Compile-check locally with the IDE's bundled arduino-cli.** There is no standalone `arduino-cli`
on the machine, but Arduino IDE 2 ships one, and with the IDE's config file it uses the cores and
libraries already installed. This is verified to work and takes about 80 s:

```
"C:\Program Files\Arduino IDE\resources\app\lib\backend\resources\arduino-cli.exe" compile --config-file "%USERPROFILE%\.arduinoIDE\arduino-cli.yaml" --profile xiao_esp32c3 --output-dir <some temp dir> ArduinoCode/MotoButtons2
"C:\Program Files\Arduino IDE\resources\app\lib\backend\resources\arduino-cli.exe" compile --config-file "%USERPROFILE%\.arduinoIDE\arduino-cli.yaml" --profile nrf52840 --output-dir <some temp dir> ArduinoCode/MotoButtonsNRF
```

For the XIAO ESP32C3 add `--build-property "compiler.cpp.extra_flags=-DMB_BOARD_ESP32C3"`; for the
C6 use `--profile xiao_esp32c6`. The ESP32 core also copies its output to
`ArduinoCode/MotoButtons2/build/` (git-ignored). The nRF
build's `.zip` is the DFU package. Compile every change before reporting it done. Flashing still needs the IDE or `arduino-cli upload`.

Sources use **CRLF** line endings (`.gitattributes` and `.editorconfig` enforce it). Style: 2-space
indent, Allman braces, `constexpr` constants in `config.h`, camelCase names, `debugPrintf()` for
logging. `.clang-format` matches; the Arduino guide's "avoid `#define`" is followed.

Arduino's sketch preprocessor generates no prototype for a function with default arguments. Keep
such functions in `.h/.cpp` units (where real prototypes exist), never in the `.ino`.

## Hardware notes (ESP32-C3 OLED; C3/C6 pins are in their `board_*.h`)

GPIO numbers, not Arduino `D` aliases (aliases move between board profiles):

```
GPIO0  joystick UP        GPIO5  OLED SDA        GPIO9  button B   (strapping)
GPIO1  joystick CENTER    GPIO6  OLED SCL        GPIO10 button C
GPIO2  joystick RIGHT     GPIO7  button A        GPIO20 RX, free
GPIO3  joystick DOWN      GPIO8  status LED      GPIO21 TX, free
GPIO4  joystick LEFT             (strapping)
```

- All button commons are wired to **GND**; everything is active low.
- GPIO2, GPIO8 and GPIO9 are **strapping pins**. GPIO2 (RIGHT) must be high for the chip to boot;
  GPIO9 (B) low at reset enters download mode. That is why orientation is chosen in a window after
  boot rather than during power-up.
- Only GPIO0..GPIO4 reach ADC1, which happens to be exactly the joystick.

## Decisions worth knowing before editing

**Joystick inputs are measured, not read digitally.** Leakage in the pod drags idle lines to
~0.7-2.2 V, inside the input's undefined band, so `digitalRead()` returns coin flips. `inputs.cpp`
keeps those pads in analog mode with the pull-up asserted and thresholds on millivolts. Do not
"simplify" this back to `digitalRead()`. See [Docs/joystick-input-notes.md](Docs/joystick-input-notes.md).

**The report engine is diff-based.** Every loop `keymap.cpp` computes which inputs are *active*
(held, opposite directions cancelled, A/B/C only when alone, past the chord grace period and not
suppressed by a fired chord), builds the keyboard report those imply, and sends it only when it
differs from the last one sent. Bindings have a kind: `Held` (down while held), `Tap` (once per
press, held `KEY_TAP_MS`), `Consumer` (consumer-page pulse, repeating if `repeatMs` is set).
A keyboard mode may also hold `Consumer` bindings (Locus zoom in); the keyboard report never carries
modifiers. Firmware repeat exists for OsmAnd, Locus zoom and Media only, and never blocks: a `Held` binding with `repeatMs`
is reported down for `repeatMs` and up for `releaseMs`, per key, by omitting it from the report during
the up phase. The OsmAnd arrows use `OSMAND_DIRECTION_KEY_DOWN_MS` / `_UP_MS` (30/30): OsmAnd scrolls
1 px per 3 ms while a key is down and adds a 200 px nudge when a press under 250 ms is released, so
each tap cycle moves the map one nudge. Both phases must be at least one BLE connection interval,
which `ble_hid.cpp` logs at connect, or OsmAnd misses the release and the motion stutters.

**DMD2 mode sends raw key down / key up.** DMD2 owns repeat, repeat speed and long press, and needs
one clean press and one clean release. The only deviation is `CHORD_GRACE_MS` (50 ms) on lone A/B/C
presses, so chords pressed as one movement do not leak a key.
See [Docs/dmd2-controller-implementation-guide.md](Docs/dmd2-controller-implementation-guide.md).

**Locus mode** needs *Use hardware buttons* enabled in Locus. Its default keys are arrows (pan),
`+`/`-` (zoom), `C` centre, `R` rotate, `D` display, `G` GPS. Measured on a Galaxy S21 (adb
injection, screen recording against Samsung's `interceptKeyTq` log times): Shift+`=` and bare `=`
do not zoom; `KEYCODE_PLUS` or volume up do, and no HID keyboard usage produces `KEYCODE_PLUS`, so A
sends consumer volume up. `C` does not jump to the position: it *toggles* follow-GPS mode, and the
map moves on the next location fix (seconds indoors, ~1 s with a good fix), so a second press cancels
it. The on-screen button centres at once; no key does. C is `Held` for the press, confirmed on the
bike phone to centre at once with a real GPS fix. Arrows are raw down/up like DMD2; Locus pans by its
*Map movement step* setting.

**The controller must not look like a typing keyboard**, or Android hides the on-screen keyboard.
Two independent checks, both verified on a Galaxy S21 (nRF52840 branch, same fix here):
1. Android marks a device `ALPHAKEY` when its declared keys include `Q`. So the keyboard collection
   is an array over only the usages the modes send, built at start-up from `keymapKeyboardKeys()`;
   the report carries each key's index + 1, translated in `bleSendKeyboardReport()`. No modifiers.
2. Samsung's Bluetooth stack flags `isKeyboard` from the peripheral minor class 0x540, which it
   derives from the BLE appearance. `BLE_APPEARANCE` is generic HID (0x03C0), set in advertising
   and in the GAP service.
Both are cached at pairing: any change to the key set or appearance needs forget + re-pair.

**The BLE device name is functional, not cosmetic.** DMD2 takes plain keys only as its paid Generic
Remote Controller; its DMD Remote slots are fed by THORK's DMD Manage app, which accepts a device whose
name contains `DMD-Remote3`. Both sketches use `DMD-Remote3`. Docs/dmd2-recognition-notes.md is
older than this finding.

**Orientation is chosen in the boot window.** For `ORIENTATION_WINDOW_MS` after start-up, a single
direction held for `ORIENTATION_HOLD_MS` becomes UP. The mapping is a table of physical pins per
orientation in `inputs.cpp`; `settings.orientation` is saved only when it changed.

**OLED screens** run in a fixed order: boot screen with version -> orientation message if set ->
`Connecting` -> `Connected` -> `READY / TO >> / RACE` splash (once per boot) -> mode screen with
the button overlay. Transients carry a priority. The panel dims after `OLED_DIM_AFTER_MS` of no
input or connection change and blanks after `OLED_BLANK_DISCONNECTED_MS` without a phone. Fonts
come from `pickFittingFont()`'s largest-first ladder. All timers are start + duration, never an
absolute `millis()` deadline.

**Bonded-only advertising.** With `BLE_WHITELIST_BONDED` true and at least one bond, advertising
uses the accept list for `BLE_WHITELIST_OPEN_AFTER_MS` after boot and after each disconnect, then
opens. Untested on hardware: if reconnects always take 90 s, the controller is not resolving the
phone's private address and the switch should go to false.

**Phone-only update path.** GitHub Pages serves the `gh-pages` branch: `index.html` (M3 setup wizard,
template in `site/`), `moto-buttons.apk`, `firmware/<board>/<prod|beta>/MotoButtons2-<ver>.bin` and
`firmware/latest.json` = `{"boards":{"<board>":{"prod":{version,file,size,md5},"beta":{...}}}}`.
Tags drive `.github/workflows/release.yml`: `<board>/v<ver>` (prod), `<board>/v<ver>-beta.N` (beta),
`app/v<ver>`; it builds with the board's entry in `release.json` (`sketch`, `profile`, `ota`), checks
the tag against the `MBFWVER=` tag in the build, and for `"ota": true` boards commits the `.bin` to
`gh-pages` via `site/tools/publish.py` (which also checks `MBBOARD=`). Every build goes to a GitHub
Release; the nRF52840 one as `.zip` (DFU package) and `.hex`. The board is
reported to the app as the Bluetooth PnP ID product (`BOARD_PRODUCT_ID`, no GATT change) and the
`BOARD_ID_TAG` string is kept in the image by a run-time reference in `ota.cpp` (the linker drops an
unused constant). Version order: 2.4.1-beta.2 < 2.4.1. The app (`RemoteFirmware.java`) reads
`BuildConfig.SITE_URL + firmware/latest.json`; debug builds use `http://localhost:8000/` (serve
`site/` and `adb reverse tcp:8000 tcp:8000`), release builds the Pages URL. Release signing key and
`keystore.properties` live in `%USERPROFILE%\.motobuttons\` (never in the repo). The app checks DMD
Manage (`DmdSupport.java`, needs the `<queries>` entry), opens from the page via `motobuttons://open`
(intent URL), shows "All set" after an update and can uninstall itself (`ACTION_DELETE`). It declares
`configChanges` so the controller's keyboard reconnecting does not recreate the screen.

**Firmware update over BLE** (`ota.cpp`, hardware-tested 2.2.0 <-> 2.2.1 on a Galaxy S21). Service
`6a2a0000-7a4e-4b5c-9d3f-2f6d6f746f62`, control `...0001` (write + notify: START size+MD5, FINISH,
ABORT; notify event/status/value) and data `...0002` (writes with response), both encrypted-only.
The image goes to the idle slot of the default OTA partition table; `Update.end()` checks the MD5 and
the controller restarts after `OTA_RESTART_DELAY_MS`. The Firmware Revision characteristic (0x2A26) in
the Device Information service carries `FIRMWARE_VERSION`; the app uses the update service to tell our
controllers apart. The phone app mirrors the UUIDs in `Protocol.java`. Adding or changing a GATT
service means every phone must forget and re-pair (Android caches the service list).

**Watchdog.** The IDF task watchdog is reconfigured to `WATCHDOG_TIMEOUT_MS` (10 s) and the loop
task subscribed. The longest blocking call is the 2 s factory-reset message; keep it that way.

**Settings** are schema-versioned (`ver` key, currently 2). Adding a field means bumping the
version and handling the migration in `settingsLoad()`.

## Debug switches

Both in `config.h`:

- `DEBUG` - general serial logging at 115200. Currently **true**. Nothing blocks on the USB host.
- `DEBUG_INPUTS` - millivolt diagnostics for every input, once a second plus on every transition.
  Currently **false**; flip to true when input trouble comes back.

## Open items

- nRF52840: no Bluetooth update yet (plan: Adafruit `BLEDfu` + Nordic DFU library in the app;
  adding the service means every phone re-pairs).
- `esp32c3` and `esp32c6` profiles (2.5.0) are compile-checked only, **not flashed**. Against their
  old branches they gain Locus, the non-keyboard HID, `DMD-Remote3`, OTA, A+C restart and the
  post-boot orientation window; A+B now switches the steady LED colour off/on instead of stepping
  brightness; the C3's DMD2 centre key is now `F5` (was `F8`). Settings start from defaults.
- The joystick leakage is worked around, not repaired - the pod should be cleaned and dried, or
  given 10 kOhm pull-ups per line.
- `Wiring/Wiring_Diagram_MotoButtons2_analog_mod.png` still shows the original nRF52840 drawing.
