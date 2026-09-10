# DMD2 Remote Controller Implementation Guide (archived copy)

> **Provenance.** This is a transcription of DMD Navigation's "Controller Implementation Guide",
> which explains how a DIY or third-party handlebar controller is recognised by the DMD2 app.
> The page has been removed from the live site (`https://www.drivemodedashboard.com/controller-implementation-guide/`
> now returns `301` to the site root), so it is mirrored here for reference.
>
> - Original URL: <https://www.drivemodedashboard.com/controller-implementation-guide/>
> - Archived snapshot used: <https://web.archive.org/web/20250121205014/https://www.drivemodedashboard.com/controller-implementation-guide/> (2025-01-21)
> - Retrieved: 2026-09-09
> - Content © THORK RACING / DMD Navigation. Reproduced here for interoperability reference only.
>
> Site navigation, footer and forum widgets have been stripped; the body text is unchanged.

---

## Remote Controller Implementation

If you are building your own handlebar remote controller or if you are a manufacturer seeking
support to make your handlebar controller work with DMD2 you can start by reading this page.

DMD2 requires the controller to send HID key down event when the button is pressed and key up
event when the key is released. This allows DMD2 to internally handle things like:

- Key repeat On / Off
- Set repeat speed if enable
- Use Long Press or Single Press

And DMD2 can and does use different button settings depending on which app section you are. For
example, one button can have a long press function when the user is in the map view but that same
button will do repeat when the user is in the Roadbook. In some sections the user can even set all
the functions, repeat on/off, repeat speed independently for joysticks and buttons and much more.

This is only possible if DMD2 knows exactly when the user presses a button and then when the user
releases the button!

After you successfully make your controller work with DMD2 you should always contact us to make it
certified for DMD2 (for free).

## Controller Types and Key Codes

DMD2 controller functions are complex and depending on the button amount available we will use each
button in different ways. For this reason it is important for DMD2 to know how many buttons are
available in your controller. For a perfect implementation nothing beats talking to us, we can make
DMD2 identify your specific controller and use one of the following modes:

### 4 Button Controllers

While developing or if you will not certify it please set the device Bluetooth Name to
**"DMD2 CTL 4K"** so that DMD2 can apply the correct scheme.

Key codes:

- `KEYCODE_DPAD_LEFT`
- `KEYCODE_DPAD_RIGHT`
- `KEYCODE_DPAD_UP`
- `KEYCODE_DPAD_DOWN`

### 5 Button Controllers

While developing or if you will not certify it please set the device Bluetooth Name to
**"DMD2 CTL 5K"** so that DMD2 can apply the correct scheme.

Key codes:

- `KEYCODE_ENTER`
- `KEYCODE_DPAD_LEFT`
- `KEYCODE_DPAD_RIGHT`
- `KEYCODE_DPAD_UP`
- `KEYCODE_DPAD_DOWN`

### 7 Button Controllers

While developing or if you will not certify it please set the device Bluetooth Name to
**"DMD2 CTL 7K"** so that DMD2 can apply the correct scheme.

Key codes:

- `KEYCODE_ENTER`
- `KEYCODE_DPAD_LEFT`
- `KEYCODE_DPAD_RIGHT`
- `KEYCODE_DPAD_UP`
- `KEYCODE_DPAD_DOWN`
- `KEYCODE_F6`
- `KEYCODE_F7`

### 8 Button Controllers

While developing or if you will not certify it please set the device Bluetooth Name to
**"DMD2 CTL 8K"** so that DMD2 can apply the correct scheme.

Key codes:

- `KEYCODE_ENTER`
- `KEYCODE_DPAD_LEFT`
- `KEYCODE_DPAD_RIGHT`
- `KEYCODE_DPAD_UP`
- `KEYCODE_DPAD_DOWN`
- `KEYCODE_F5`
- `KEYCODE_F6`
- `KEYCODE_F7`

## Other Information

**Controller Detection & Non-HID:**

To keep it it simple DMD2 will detect the controller type by checking the BT Connected devices and
match your controller device name with our list of devices. If you have different models that are
identifiable by querying a GATT service we can also implement that.

If your controller does not use HID and does everything by GATT services we can also implement that
but we will need you to contact us on both cases.

**More Complex Features:**

If your controller does more than HID key presses, like 360 degree joystick, voltage reading, other
sensors input and you would like DMD2 to integrate those functions, please contact us and we will do
our best.

Be sure to provide clear instructions!

---

## How MotoButtons 2 implements this

This controller has 8 inputs, so it follows the **8 button** scheme:

| Input | Key sent | HID usage | Android keycode |
|---|---|---|---|
| Joystick UP | Arrow Up | `0x52` | `KEYCODE_DPAD_UP` |
| Joystick DOWN | Arrow Down | `0x51` | `KEYCODE_DPAD_DOWN` |
| Joystick LEFT | Arrow Left | `0x50` | `KEYCODE_DPAD_LEFT` |
| Joystick RIGHT | Arrow Right | `0x4F` | `KEYCODE_DPAD_RIGHT` |
| Joystick CENTER | F5 | `0x3E` | `KEYCODE_F5` |
| Button A | F6 | `0x3F` | `KEYCODE_F6` |
| Button B | F7 | `0x40` | `KEYCODE_F7` |
| Button C | Enter | `0x28` | `KEYCODE_ENTER` |

The table lives in [keymap.cpp](../ArduinoCode/MotoButtons2/keymap.cpp). In DMD2 mode the firmware
sends **raw key down on press and key up on release** for every button, with no firmware-side
auto-repeat and nothing fired on release only. That is the guide's first requirement: DMD2 owns
repeat on/off, repeat speed and long press, per app section, and can only do that if it sees one
clean press and one clean release. (OsmAnd and Media modes keep their own key repeat, since those
apps do not implement it. For the OsmAnd arrows the repeat is a tap of `OSMAND_DIRECTION_KEY_DOWN_MS`
down and `OSMAND_DIRECTION_KEY_UP_MS` up, because OsmAnd scrolls the map itself while a key is down
and adds a 200 px nudge for every press shorter than 250 ms.)

The one deliberate deviation: a lone A, B or C press is reported after a short grace period
(`CHORD_GRACE_MS`, 50 ms), so that a two-button chord landing within it does not leak its first key
to DMD2. The press and release DMD2 sees are still one clean pair, just a few tens of milliseconds
late.

The Bluetooth name is `BLE_DEVICE_NAME` in [config.h](../ArduinoCode/MotoButtons2/config.h). The
guide prescribes `DMD2 CTL 8K` for this key set; the current build advertises as `Bush Moto OLED`,
which DMD2 does not recognise. See [dmd2-recognition-notes.md](dmd2-recognition-notes.md) for what
has been tried. After changing the name, forget the old pairing on the phone and pair again -
Android caches the previously advertised name.
