# Bush's Moto Buttons v1.0
### Thanks to [the original author](https://github.com/joncox123/MotoButtons2/tree/main) for free sourcing this amazing piece of art, especially the dev board wiring and the code part! So, I needed some modifications to it to move forward to perfection. To continue supporting enduro, offroad, adventure enthusiasts who can also tinker with electronics, and build a relatively inexpensive alternative to moto buttons, I also want to share my work.

### My changes:
- LED brightness control by holding the A key
- Added possibility to add any color or brightness (within code)
- Separate key A and Joystick Center key codes (in original code they were the same)
- DMD2 mode color changed to blue (looks cooler)
- Button special function changes (will be described later)
- Added reset option by holding A,B,C keys. This is a workaround for the issue when Bluetooth auto-pairing did not work anymore, e.g., when the moto buttons are powered on, auto-connection to the phone/tablet does not work

### Design:

| | |
| --- | --- |
| ![Bike](Design/Bike.jpg) | ![Front](Design/Front.jpg) |
| ![Inside](Design/Inside.jpg) | ![Top](Design/Top.jpg) |

### Parts:
- Some old USB-A cable for power and other wires
- Seeed Studio XIAO ESP32C3
- 3x waterproof buttons (I used V12B-10N-A)
- 5-way joystick (I used JS5208)
- RGB LED common **anode ONLY** (I used OSTAMA5B31A) **Do not use common cathode, I already burned 2x Seeed boards to realize that**
- 3D printed case with some screws (the case is waterproof enough to hold rain and some submersion. I've already been using it for 1 year, no issues. But use some super glue on the TPU gasket and glue gun around holes from inside). All TPU and PETG (or alternative filament) STLs are in 3D folder.

<img src="3D/Parts.png" alt="Parts" width="600"/>

## User Manual
The device has:
- 3x buttons and 1x joystick as inputs
- 1x LED indicator (for understanding what is going on)

Standard button functionality by mode:

`DMD2` mode - the controller advertises as `DMD2 CTL 8K`, the name DMD2 matches to apply its
8 button scheme (see the [controller implementation guide](Docs/dmd2-controller-implementation-guide.md)):
- Button A short press or hold - `F6` / zoom in
- Button B short press or hold - `F7` / zoom out
- Button C short press or hold - `Enter` / follow toggle (focus)
- Joystick Up, Down, Left, Right - Arrow keys
- Joystick Middle press - `F5`

`OsmAnd` mode:
- Button A short press or hold - `+` / zoom in
- Button B short press or hold - `-` / zoom out
- Button C short press or hold - `C` / move to my location (focus)
- Joystick Up, Down, Left, Right - Arrow keys
- Joystick Middle press - Unbound

`Media` mode:
- Button A short press or hold - Play / Pause
- Button B short press or hold - Screen brightness up
- Button C short press or hold - Screen brightness down
- Joystick Up - Volume up
- Joystick Down - Volume down
- Joystick Left - Previous track
- Joystick Right - Next track
- Joystick Middle press - Mute

Special functions:
- Hold one joystick direction while powering on the device - that direction becomes UP (joystick orientation is saved); the display shows "Orientation is set" plus which direction is now UP (e.g. "UP is LEFT") for 3 secs. Orientation can only be changed at boot, never while the controller is running
- Button A+B long press - LED brightness changes; hold it until you are satisfied and release
- Button B+C long press - Mode change: DMD2 (blue) -> OsmAnd (green) -> Media (magenta)
- Button A+B+C long press (5 secs) - Reset saved settings and Bluetooth bonds. Use this if automatic reconnection stops working
- Button A+C long press (5 secs) - Restart the controller
- Screens in order: "Booting...", then the orientation message if a direction was held, then "Connecting" until a phone connects, then "Connected", then a "READY TO >> RACE" splash, then the mode screen. With no phone connected it stays on "Connecting" and the splash is never shown
- The mode screen shows a button overlay along the top for debugging presses and combos. Each button has a fixed slot - `A B C`, the four directions as arrows, center as a dot - and a slot is drawn only while that button is held


## Wiring
If something is unclear, please read [the original author's manuals](https://github.com/joncox123/MotoButtons2/tree/main/ConstructionGuide).
<img src="Wiring/Wiring_Diagram_MotoButtons2_analog_mod.png" alt="Wiring Diagram" width="600"/>

## Code
See the [programming instructions](Programming/README.md).

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
