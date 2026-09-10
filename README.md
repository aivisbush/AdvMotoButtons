# Bush's Moto Buttons v2.1
### Thanks to [the original author](https://github.com/joncox123/MotoButtons2/tree/main) for free sourcing this amazing piece of art, especially the dev board wiring and the code part! So, I needed some modifications to it to move forward to perfection. To continue supporting enduro, offroad, adventure enthusiasts who can also tinker with electronics, and build a relatively inexpensive alternative to moto buttons, I also want to share my work.

A handlebar Bluetooth controller for motorcycle navigation. An **ESP32-C3 OLED Mini** board with a
5-way joystick and three buttons presents itself as a Bluetooth keyboard and drives DMD2, OsmAnd or
a media player, showing what it is doing on the board's 72x40 OLED.

### My changes against the original
- Runs on the ESP32-C3 OLED Mini instead of the nRF52840. The onboard OLED replaces the RGB LED as
  the indicator and shows the mode, the connection state and which inputs are pressed
- Separate key codes for button A and the joystick centre (they were the same in the original)
- DMD2 mode sends raw key down / key up, so DMD2 does its own key repeat and long press
- The joystick lines are measured with the ADC instead of read digitally, which keeps a leaky
  joystick pod usable ([why](Docs/joystick-input-notes.md))
- Button chords for mode change, display on/off, restart and factory reset, with an on-screen
  countdown for the long ones
- Joystick orientation chosen by holding a direction right after power-on, for four mounting positions
- Once a phone has paired, only bonded phones are accepted for a while after each power-on
- Watchdog, versioned settings, and dimming and blanking to spare the OLED

### Design:

| | |
| --- | --- |
| ![Bike](Design/Bike.jpg) | ![Front](Design/Front.jpg) |
| ![Inside](Design/Inside.jpg) | ![Top](Design/Top.jpg) |

### Parts:
- Some old USB-A cable for power and other wires
- ESP32-C3 OLED Mini (an ESP32-C3 with a 0.42" 72x40 SSD1306 OLED on board)
- 3x waterproof buttons (I used V12B-10N-A)
- 5-way joystick (I used JS5208)
- 3D printed case with some screws (the case is waterproof enough to hold rain and some submersion. I've already been using it for 1 year, no issues. But use some super glue on the TPU gasket and glue gun around holes from inside). All TPU and PETG (or alternative filament) STLs are in 3D folder.

<img src="3D/Parts.png" alt="Parts" width="600"/>

## User Manual
The device has:
- 3x buttons (A, B, C) and 1x 5-way joystick as inputs
- the onboard 72x40 OLED and one status LED as indicators

### Modes
Hold **B+C** for one second to step through the modes: DMD2 -> OsmAnd -> Media -> DMD2. The mode is
remembered across power cycles.

`DMD2` mode - raw key down on press and key up on release for every input; DMD2 handles repeat and
long press itself (see the [controller implementation guide](Docs/dmd2-controller-implementation-guide.md)):
- Button A - `F6` / zoom in
- Button B - `F7` / zoom out
- Button C - `Enter` / follow toggle (focus)
- Joystick Up, Down, Left, Right - arrow keys
- Joystick centre - `F5`

`OsmAnd` mode - the firmware repeats keys while they are held:
- Button A - `+` / zoom in (repeats)
- Button B - `-` / zoom out (repeats)
- Button C - `C` / move to my location (once per press)
- Joystick Up, Down, Left, Right - arrow keys (repeat)
- Joystick centre - unbound

`Media` mode - media keys:
- Button A - play / pause (once per press)
- Button B - screen brightness up (repeats)
- Button C - screen brightness down (repeats)
- Joystick Up / Down - volume up / down (repeat while held)
- Joystick Left / Right - previous / next track (once per press)
- Joystick centre - mute (once per press)

The Bluetooth name is set by `BLE_DEVICE_NAME` in [config.h](ArduinoCode/MotoButtons2/config.h) and
is currently `Bush Moto OLED`. DMD2 recognises controllers by name and does not know this one, so it
lists the controller as a Generic Remote Controller; see the
[recognition notes](Docs/dmd2-recognition-notes.md).

### Button chords
| Buttons | Hold | Action |
|---|---|---|
| B + C | 1 s | Next mode |
| A + B | 1 s | Display on / off |
| A + C | 5 s | Restart the controller (countdown on screen) |
| A + B + C | 5 s | Factory reset: settings and Bluetooth bonds (countdown on screen) |

While two or three buttons are held, none of them is sent as a key. A lone A, B or C press is sent
about 50 ms after it is registered, so a chord pressed as one movement never leaks a key to the
phone; a chord pressed one finger after the other may still send the first key briefly.

### Joystick orientation
The controller can be mounted in four positions. During the first 2.5 seconds after power-on, while
the boot screen is showing, hold the joystick in the direction that should be UP for about half a
second. The display confirms with `Orientation is set, UP is ...` and the choice is saved. Without a
held direction the saved orientation is kept.

Press the direction *after* power is on rather than holding it while switching on: joystick RIGHT
and button B sit on chip strapping pins, and the board will not boot normally with either held.

### Screens
1. `Booting` with the firmware version
2. `Orientation is set` if a direction was held
3. `Connecting` until a phone connects
4. `Connected`, then a `READY TO >> RACE` splash, once per power-on
5. The mode screen: the mode name plus a button overlay along the top. Each input has a fixed slot -
   `A B C`, the four directions as arrows, centre as a dot - and a slot is drawn only while that
   input is held. The arrows show the orientation-mapped directions.

The display dims after 30 seconds without a press or connection change and switches off after a
minute with no phone connected. Any press, or a phone connecting, wakes it.

### Status LED
Breathes while waiting for a phone, glows dimly once connected.

### Bluetooth pairing
The controller bonds with "Just Works" pairing; there is no PIN. Pair from the phone's Bluetooth
settings while the display shows `Connecting`. From then on the controller reconnects on its own.

Once a phone has bonded, the controller advertises to bonded phones only for the first 90 seconds
after power-on and after every disconnect, so a stranger's phone cannot grab it at a fuel stop. After
that it opens up until the next disconnect, so a phone that lost its bond can pair again. To pair an
additional phone, either wait for that open window or do a factory reset (A+B+C for 5 s) and pair
everything again. If a phone refuses to connect after a factory reset, forget the controller on the
phone and pair afresh.

## Wiring
GPIO numbers, not Arduino `D` aliases. All button commons go to GND.

```
GPIO0  joystick UP        GPIO5  OLED SDA        GPIO9  button B   (strapping)
GPIO1  joystick CENTER    GPIO6  OLED SCL        GPIO10 button C
GPIO2  joystick RIGHT     GPIO7  button A        GPIO20 RX, free
GPIO3  joystick DOWN      GPIO8  status LED      GPIO21 TX, free
GPIO4  joystick LEFT             (strapping)
```

If something is unclear, please read [the original author's manuals](https://github.com/joncox123/MotoButtons2/tree/main/ConstructionGuide).
The diagram below is still the original nRF52840 drawing, hand-annotated for the C3 pins above.

<img src="Wiring/Wiring_Diagram_MotoButtons2_analog_mod.png" alt="Wiring Diagram" width="600"/>

## Code
See the [programming instructions](Programming/README.md). The sketch is split into units; every
tunable (pins, timings, key codes, Bluetooth name, debug switches) is in
[config.h](ArduinoCode/MotoButtons2/config.h), and the key tables are in
[keymap.cpp](ArduinoCode/MotoButtons2/keymap.cpp).

## References
- https://github.com/sigmdel/mini_esp32c3_oled_sketches
- [DMD2 controller implementation guide](Docs/dmd2-controller-implementation-guide.md) - archived copy of the
  DMD Navigation page describing how DMD2 recognises a controller by its Bluetooth name, and the key codes for
  each button scheme ([original](https://www.drivemodedashboard.com/controller-implementation-guide/), now offline;
  [archived snapshot](https://web.archive.org/web/20250121205014/https://www.drivemodedashboard.com/controller-implementation-guide/))
- https://github.com/StylesRallyIndustries/RallyController - another DIY DMD2 controller; its changelog confirms
  the `DMD2 CTL xK` device names are what DMD2 detects

## Notes
Background notes for anyone (or any agent) picking this up:
- [CLAUDE.md](CLAUDE.md) - project overview, pin map, build notes and design decisions
- [Docs/joystick-input-notes.md](Docs/joystick-input-notes.md) - why the joystick is read with the ADC
- [Docs/dmd2-recognition-notes.md](Docs/dmd2-recognition-notes.md) - state of the DMD2 remote recognition work
