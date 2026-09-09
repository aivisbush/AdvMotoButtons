# MotoButtons 2 - project context

Handlebar BLE HID controller for motorcycle navigation. An **ESP32-C3 OLED Mini** board with a
5-way joystick and three buttons presents itself as a Bluetooth keyboard and drives DMD2, OsmAnd or
a media player, showing status on a 72x40 OLED.

Fork of [joncox123/MotoButtons2](https://github.com/joncox123/MotoButtons2), rewritten for the
ESP32-C3 (the original targets an nRF52840). Work happens on the `esp32c3OLED` branch; `dev` is the
main branch.

## Layout

| Path | What |
|---|---|
| `ArduinoCode/MotoButtons2/MotoButtons2.ino` | The entire firmware, one file |
| `Docs/` | Reference notes - read these before changing input handling or DMD2 behaviour |
| `Programming/README.md` | Flashing instructions |
| `Wiring/` | Wiring diagram (still the original nRF52840 drawing, hand-annotated for C3 pins) |
| `3D/`, `Design/` | Case and photos |

## Building

Arduino IDE, board **XIAO_ESP32C3**, esp32 core 3.3.8. Libraries all ship with the board package
(NimBLE, U8g2, Preferences).

**There is no `arduino-cli` on the development machine**, so changes cannot be compile-checked
locally - review edits carefully and say plainly that they are unverified.

Files use **CRLF** line endings. Style is 2-space indent, Allman braces, constants grouped at the
top of the file, `if (DEBUG)` guarded serial output.

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
- GPIO2, GPIO8 and GPIO9 are **strapping pins**. Holding GPIO2 (joystick RIGHT) low through a
  power-up can block boot, which matters for the boot-time orientation feature.
- Only GPIO0..GPIO4 reach ADC1, which happens to be exactly the joystick.

## Decisions worth knowing before editing

**Joystick inputs are measured, not read digitally.** Leakage in the pod drags idle lines to
~0.7-2.2 V, inside the input's undefined band, so `digitalRead()` returns coin flips - phantom
presses. The firmware keeps those pads in analog mode with the pull-up asserted and thresholds on
millivolts instead. Do not "simplify" this back to `digitalRead()`.
See [Docs/joystick-input-notes.md](Docs/joystick-input-notes.md).

**DMD2 mode sends raw key down / key up.** No firmware auto-repeat, nothing fired on release only:
DMD2 owns repeat, repeat speed and long press, and needs one clean press and one clean release to
do it. OsmAnd and Media keep firmware repeat, since those apps do not implement it.
See [Docs/dmd2-controller-implementation-guide.md](Docs/dmd2-controller-implementation-guide.md).

**The BLE device name is functional, not cosmetic.** DMD2 picks a button scheme by matching the
Bluetooth name against its own list. Currently under test - see
[Docs/dmd2-recognition-notes.md](Docs/dmd2-recognition-notes.md).

**Joystick orientation is chosen at boot only** - hold one direction while powering up and it
becomes UP. Saved in `Preferences` under `motobuttons/orient`, along with mode and OLED state.

**OLED screens** run in a fixed order: `Booting...` -> orientation message if a direction was held
-> `Connecting` until a phone connects -> `Connected` -> `READY / TO >> / RACE` splash -> mode
screen. Transients carry a priority so a low-priority message cannot cut short a high-priority one.
Fonts are chosen at runtime by `pickFittingFont()`, which walks a largest-first ladder, so all mode
names share one size and the splash is as large as fits.

**The mode screen carries a button overlay** across the top for debugging presses and chords: fixed
slots for `A B C`, four arrows and a centre dot, drawn only while held. It shows the *logical*
(orientation-mapped) states, and deliberately shows raw states so a conflicting pair is visible.

## Debug switches

Both at the top of the `.ino`:

- `DEBUG` - general serial logging at 115200. Currently **true**.
- `DEBUG_INPUTS` - millivolt diagnostics for every input, once a second plus on every transition.
  Currently **false**; flip to true when input trouble comes back.

## Open items

- DMD2 still lists the controller as *Generic Remote Controller* rather than a known device; the
  Generic Remote Controller entry needs a paid license in current DMD2 versions.
- The joystick leakage is worked around, not repaired - the pod should be cleaned and dried.
- `Wiring/Wiring_Diagram_MotoButtons2_analog_mod.png` and `Programming/README.md` still describe the
  original nRF52840 pin map in places.
