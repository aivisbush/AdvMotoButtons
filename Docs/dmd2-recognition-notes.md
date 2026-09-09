# DMD2 recognition: what is known

Goal: have DMD2 treat this controller as a known handlebar remote instead of listing it as a
*Generic Remote Controller*. Not solved yet. This is the state of the investigation so the next
session does not repeat it.

## How DMD2 identifies a controller

From DMD's own (now offline) implementation guide, mirrored at
[dmd2-controller-implementation-guide.md](dmd2-controller-implementation-guide.md):

> "DMD2 will detect the controller type by checking the BT Connected devices and match your
> controller device name with our list of devices."

So it is the **Bluetooth device name**, matched against a list inside the app. The guide publishes
names for DIY builders - `DMD2 CTL 4K` / `5K` / `7K` / `8K` - each with a fixed key set. It also
requires real HID key **down on press, up on release**, because DMD2 owns repeat and long press.

## What has been tried

| Attempt | Result |
|---|---|
| Name `Bush Moto OLED`, center = F8, firmware auto-repeat | Generic |
| Name `DMD2 CTL 8K`, center = F5 (the published 8-button set), raw key down/up | Still Generic |
| Name `CICTRL` (Carpe Iter Adventure Control) | Under test - outcome not yet recorded |

The current key set matches the published 8-button scheme: `ENTER`, four arrows, `F5`, `F6`, `F7`.
That part is believed correct regardless of the name question.

## Things not yet ruled out

- **Stale pairing.** Android caches the device name in the bond record, so the phone must *forget*
  the device (and the controller must clear its bonds, chord A+B+C for 5 s) before re-pairing,
  otherwise the new name is never seen.
- **Missing permission.** Without the Nearby devices / `BLUETOOTH_CONNECT` permission, DMD2 gets
  nothing from `getName()` on Android 12+ and no name can match.
- **The scheme may be retired.** The guide predates DMD2's current *Devices* architecture, in which
  Generic Remote Controller is a virtual device type that needs a paid license. The `DMD2 CTL xK`
  names once required the DMD2 beta (per RallyController's changelog, 2023).

## Reference points

- [StylesRallyIndustries/RallyController](https://github.com/StylesRallyIndustries/RallyController) -
  DIY controller whose changelog tracked which names DMD2 detected (`CICTRL` in v0.4, then the
  `DMD2 CTL xK` names in v0.5). Its shipped name is neither, so nothing there is confirmed working.
  The `69` in its `BleKeyboard(name, manufacturer, 69)` line is just a battery percentage.
- [Raphael713/DMD2---Two-Button-controller](https://github.com/Raphael713/DMD2---Two-Button-controller) -
  ESP32 zoom-only remote sending F6/F7, named `DMD2Buttons`. Useful negative result: a name
  containing "DMD2" that is not on the list is not detected.
- JaxeADV BarButtons is officially DMD2-certified yet sends plain keyboard keys (F6 zoom in, F7
  zoom out, Enter select, F5 back, arrows) - certification is an endorsement, not a different
  protocol.
- Carpe Iter's "native integration" runs through their own Next-Gen Hub app and an accessibility
  service, not through anything on the BLE link.

## Sanctioned route

The guide says certification is free and invites builders to make contact:

> "After you successfully make your controller work with DMD2 you should always contact us to make
> it certified for DMD2 (for free)."

That is how a device name gets onto DMD2's list, and it is the remaining path if no published name
works.
