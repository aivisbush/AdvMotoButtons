# MotoButtons 2 - project context

Handlebar BLE HID controller for motorcycle navigation. An **ESP32-C3 OLED Mini** board with a
5-way joystick and three buttons presents itself as a Bluetooth keyboard and drives DMD2, OsmAnd or
a media player, showing status on a 72x40 OLED.

Fork of [joncox123/MotoButtons2](https://github.com/joncox123/MotoButtons2), rewritten for the
ESP32-C3 (the original targets an nRF52840). Work happens on the `esp32c3OLED` branch; `dev` is the
main branch.

## Layout

The sketch lives in `ArduinoCode/MotoButtons2/` as one `.ino` plus units the Arduino IDE compiles
as tabs:

| File | What |
|---|---|
| `MotoButtons2.ino` | `setup()`/`loop()`, status LED, boot orientation window, watchdog, boot log |
| `config.h` | every tunable: pins, timings, thresholds, HID codes, BLE name, `DEBUG` switches, `FIRMWARE_VERSION` |
| `inputs.h/.cpp` | `Button` struct array, ADC joystick reading, debounce, orientation table, diagnostics |
| `keymap.h/.cpp` | per-mode key tables and the HID report engine |
| `chords.h/.cpp` | button combinations: mode, display, restart, factory reset |
| `oled.h/.cpp` | display state machine, rendering, dim/blank |
| `ble_hid.h/.cpp` | NimBLE HID device, bonded-phone whitelist |
| `settings.h/.cpp` | Preferences record with a schema version |
| `debug.h` | `debugPrintf()` / `debugPrintln()`, compiled out when `DEBUG` is false |
| `sketch.yaml` | arduino-cli profile pinning core and library versions |

Elsewhere: `.github/workflows/compile.yml` compiles on every push, `Docs/` holds reference notes
(read them before touching input handling or DMD2 behaviour), `Programming/README.md` is the
flashing guide, `Wiring/` still shows the original nRF52840 drawing hand-annotated for C3 pins,
`3D/` and `Design/` are case and photos.

## Building

Arduino IDE, board **XIAO_ESP32C3**, esp32 core **3.3.8**, plus two Library Manager libraries that
are *not* part of the core: **NimBLE-Arduino 2.5.0** and **U8g2 2.36.19**. `Preferences` and
`Wire` ship with the core.

**Compile-check locally with the IDE's bundled arduino-cli.** There is no standalone `arduino-cli`
on the machine, but Arduino IDE 2 ships one, and with the IDE's config file it uses the cores and
libraries already installed. This is verified to work and takes about 80 s:

```
"C:\Program Files\Arduino IDE\resources\app\lib\backend\resources\arduino-cli.exe" compile --config-file "%USERPROFILE%\.arduinoIDE\arduino-cli.yaml" --fqbn esp32:esp32:XIAO_ESP32C3 --warnings all --build-path <some temp dir> ArduinoCode/MotoButtons2
```

Compile every change before reporting it done. Flashing still needs the IDE or `arduino-cli upload`.

Sources use **CRLF** line endings (`.gitattributes` and `.editorconfig` enforce it). Style: 2-space
indent, Allman braces, `constexpr` constants in `config.h`, camelCase names, `debugPrintf()` for
logging. `.clang-format` matches; the Arduino guide's "avoid `#define`" is followed.

Arduino's sketch preprocessor generates no prototype for a function with default arguments. Keep
such functions in `.h/.cpp` units (where real prototypes exist), never in the `.ino`.

## Hardware notes

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
Firmware repeat exists for OsmAnd and Media only.

**DMD2 mode sends raw key down / key up.** DMD2 owns repeat, repeat speed and long press, and needs
one clean press and one clean release. The only deviation is `CHORD_GRACE_MS` (50 ms) on lone A/B/C
presses, so chords pressed as one movement do not leak a key.
See [Docs/dmd2-controller-implementation-guide.md](Docs/dmd2-controller-implementation-guide.md).

**The BLE device name is functional, not cosmetic.** DMD2 picks a button scheme by matching the
Bluetooth name against its own list. Current name: `Bush Moto OLED` (not recognised, by choice for
now). See [Docs/dmd2-recognition-notes.md](Docs/dmd2-recognition-notes.md).

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

- The 2.1.0 refactor is compile-checked but **not yet flashed and tested on the hardware**.
- DMD2 still lists the controller as *Generic Remote Controller*; the Generic Remote Controller
  entry needs a paid license in current DMD2 versions.
- The joystick leakage is worked around, not repaired - the pod should be cleaned and dried, or
  given 10 kOhm pull-ups per line.
- `Wiring/Wiring_Diagram_MotoButtons2_analog_mod.png` still shows the original nRF52840 drawing.
