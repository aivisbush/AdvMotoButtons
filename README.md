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
- Seeed Studio XIAO nRF52840 series (I used Seeed Studio XIAO nRF52840 Sense)
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

`DMD2` mode (**NOTE**: bound buttons manually in Settings -> Setup Remote Controller):
- Button A short press or hold - `F6` / zoom in
- Button B short press or hold - `F7` / zoom out
- Button C short press or hold - `Enter` / follow toggle (focus)
- Joystick Up, Down, Left, Right - Arrow keys
- Joystick Middle press - `F8`

`OsmAnd` mode:
- Button A short press or hold - `+` / zoom in
- Button B short press or hold - `-` / zoom out
- Button C short press or hold - `C` / move to my location (focus)
- Joystick Up, Down, Left, Right - Arrow keys; while held, the key is repeated every 30 ms so the map scrolls fast
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
- Hold one joystick direction (up, down, left or right) while powering on the device to select the orientation. The direction you hold becomes "up" (the three buttons are then on the opposite side). The LED flashes red 1 to 4 times to confirm and the orientation is saved
- Button A+B long press - LED brightness changes; hold it until you are satisfied and release. The dimmest step turns the LED completely off, the next step wraps back to full brightness
- Button B+C long press - Mode change: DMD2 (blue) -> OsmAnd (green) -> Media (magenta)
- Button A+B+C long press (5 secs) - Clear all Bluetooth bonds and reset settings to defaults (LED flashes red 4 times). This is the workaround to get BT auto-connect working again: afterwards "forget" the device on your phone/tablet and pair it again

Programming: the Arduino IDE resets the device into DFU mode automatically when uploading. If that fails, double tap the small reset button next to the USB-C connector (see [Programming](Programming/README.md)).

LED indicator:
- Blinking blue - not connected, waiting for the phone/tablet
- Steady blue / green / magenta - connected, showing the current mode
- One long blink in the mode color - mode was changed (or selected at power on)


## Wiring
If something is unclear, please read [the original author's manuals](https://github.com/joncox123/MotoButtons2/tree/main/ConstructionGuide).
<img src="Wiring/Wiring_Diagram_MotoButtons2_analog_mod.png" alt="Wiring Diagram" width="600"/>

## Code
Please read [the original author's manuals](https://github.com/joncox123/MotoButtons2/tree/main/Programming) on how to do that.
