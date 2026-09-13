---
name: moto-buttons-skill
description: Everything learned about the MotoButtons2 BLE controller on the Seeed XIAO ESP32C3 (NimBLE-Arduino HID) - user working rules, hardware and user context, verified library facts, the 2.1 port from the nRF52840 branch with its review fixes, tuning results, tooling, PC gotchas and the test procedure. Use for any MotoButtons2 work on the esp32c3 branch.
---

# MotoButtons2 knowledge base, esp32c3 branch (session of 2026-09-13; firmware 2.1 = port of nRF52840 commit 24eb5fc, compiled, NOT hardware-tested)

## 1. User and working rules
- User: Aivis Bush, experienced coder, rides enduro/adventure; the controller is on his motorcycle handlebar, powered from
  the bike's USB, paired to an Android phone running OsmAnd and DMD2 (Drive Mode Dashboard). Not tested on iPhone.
- He flashes from the Arduino IDE himself, tests on the phone, commits himself (commit messages like "Fixed joystick").
  Ask before touching the board, COM port or toolchain. Answer briefly; he asks follow-ups when he wants more.
- Comments: one short line per fact, user-facing purpose only, no mechanism/struct-field docs, no tuning/test history.
  His own rewrite that sets the standard:
  `// Media: volume/brightness keep stepping while held`
  `// OsmAnd: fast map scroll by tapping the arrow; 45/40 ms tuned on Android (not tested on iPhone)`
  Fewer lines is better ("I am coder I know the code"). When unsure, leave the comment out.
- Config section order at the top of the sketch: 0 DEBUG, 1 BLE, 2 wiring, 3 RGB colors + state/mode colors,
  4 modes + key maps + how they are switched, 5 special functions (combos, hold times, orientation), 6 timing.
  Everything user-changeable lives there; code below needs no edits.
- Compact style: K&R braces, one-statement bodies on one line, small structs on one line; target as few lines as readable.
- Incident to never repeat (nRF52840 branch): an untested build hook installed into his board package broke every IDE
  compile, and a firmware with five bundled features incl. a new BLE service stopped the keys working. Rules from it:
  verify toolchain changes with a real arduino-cli compile BEFORE reporting done; deliver firmware features one at a time
  and test on hardware with the logger before handover; never bundle a GATT change with other features; a new BLE
  service or characteristic always comes with "forget the device on the phone and pair again" said up front (Android
  caches GATT handles, HID keys silently stop working otherwise).
- Decisions he took: LED brightness wraps to fully off (255) by design; joystick repeat only for directions (later A/B
  too); A+C combo removed (reset button or IDE auto-reset is enough); watchdog rejected; unbonded-peer filtering rejected
  (would block pairing a second phone); Bluetooth update app wanted but LATER.

## 2. Hardware (this branch)
- Seeed XIAO ESP32C3, common-ANODE RGB LED (OSTAMA5B31A) on D0 red, D6 blue, D7 green; joystick JS5208 contacts
  D3 UP, D8 DOWN, D4 LEFT, D10 RIGHT, D9 CENTER; buttons V12B-10N-A A=D5, B=D2, C=D1. Directions and A/B/C use internal
  pull-down (switch to 3V3). CENTER switches to GND with internal pull-up (active low) - GPIO9 has the boot pull-up.
- D8/GPIO8 and D9/GPIO9 are boot strapping pins: holding DOWN or CENTER at power-on or during upload changes the boot mode.
  Never propose moving them onto other functions without the user; the README warns about it.
- History: "ESP32C6 support" commit swapped key C and RGB green pins because of keypress issues; "pins" commit moved B to
  D2 and blue to D6. Variant D0..D10 = GPIO 2,3,4,5,6,7,21,20,8,9,10 (`variants/XIAO_ESP32C3/pins_arduino.h`).
- Orientation: hold one joystick contact at power-on -> orientation whose UP is that contact (`ORIENTATION_PINS` rows
  0 buttons top {RIGHT,LEFT,UP,DOWN}, 1 left {DOWN,UP,RIGHT,LEFT}, 2 bottom {LEFT,RIGHT,DOWN,UP}, 3 right {UP,DOWN,LEFT,RIGHT}
  = default). Unlike the nRF board the default row maps each contact to its own name. A 90-degree rotated log means the
  user held the unit differently, not a bug. The 2.0 firmware selected orientation differently (up->2, down->0, left->1,
  right->3); 2.1 uses the nRF rule "held contact becomes UP".
- Case is glued/waterproof, so opening it for USB is a chore (motivation for OTA updates).

## 3. Environment on this PC
- FQBN `esp32:esp32:XIAO_ESP32C3`. Core esp32 3.3.8 at `%LOCALAPPDATA%\Arduino15\packages\esp32\hardware\esp32\3.3.8`
  (board menu: CDC on boot enabled by default, partition "Default 4MB with spiffs" 1.2 MB app).
- NimBLE-Arduino 2.5.0 (h2zero) in `%USERPROFILE%\Documents\Arduino\libraries\NimBLE-Arduino` (Library Manager offers
  2.5.1). The core's own `BLE` library is Bluedroid and does not provide `NimBLEDevice.h`; the sketch needs this library.
  Other user libraries present: HijelHID_BLEKeyboard, U8g2 (unused by this branch).
- arduino-cli 1.5.1: `"C:\Program Files\Arduino IDE\resources\app\lib\backend\resources\arduino-cli.exe"`.
  `compile --fqbn esp32:esp32:XIAO_ESP32C3 --warnings all --build-path <dir> ArduinoCode\MotoButtons2`
  takes a few minutes; 2.1 release = 576973 bytes (44 %), 22824 bytes RAM, zero warnings from the sketch (the hundreds
  of `-Wmissing-field-initializers` warnings come from ESP-IDF headers). Upload would be
  `upload -p COMx --fqbn esp32:esp32:XIAO_ESP32C3 --input-dir <build dir> <sketch dir>` (esptool, auto-reset via
  DTR/RTS). No board was connected on 2026-09-13, COM port unknown; ask the user before any upload.
- Debug build: copy the sketch to a temp `MotoButtons2/` folder, replace `#define DEBUG false` with true, build to a
  separate build dir. Debug prints: button pressed/released, "Report sent", "Key report send failed, will retry.",
  "Mode advanced to N", "Writing/Read settings: m,o,b", "Media key <usage>", "LED brightness changed to N",
  "BLE connected/disconnected.", "BLE advertising started." and echoes of the tuning command. Waits up to 2 s for USB
  CDC serial at boot.
- Serial logger (re-create if scratchpad is gone): PowerShell, `New-Object System.IO.Ports.SerialPort COMx,115200`,
  DtrEnable, ReadTimeout 300, ReadLine loop appending `[HH:mm:ss.fff] line` to a log, catch TimeoutException, outer loop
  reconnects when the port vanishes (reset/replug), optional command file whose trimmed content is written + "\n" to the
  port. Start hidden: `Start-Process powershell -ArgumentList -NoProfile,-ExecutionPolicy,Bypass,-File,<script>,... -WindowStyle Hidden -PassThru`.
  Stop it (Stop-Process) before every upload (esptool needs the port). Unverified on the C3: esptool-style DTR/RTS
  toggling can reset the board when the port opens; if the log shows a reboot on every reconnect, open without DTR.
- Key recorder (only useful with a BLE-capable PC dongle): C# low-level keyboard hook via Add-Type, filtered to arrows,
  F6-F8, Enter, OEM_PLUS/MINUS, C, volume and media VKs; log key DOWN/UP with ms timestamps; needs Application.Run().
  Tell the user not to type while it runs; delete the log afterwards.
- PC Bluetooth dongle CSR VID 0A12 PID 0001 is Classic-only (no "Microsoft Bluetooth LE Enumerator"); it cannot see the
  LE-only controller. A real BLE 4.0+ dongle (TP-Link UB400/UB500, ASUS BT400/BT500, Intel) would enable PC-side tests.
- PC gotchas: sandbox blocks a command containing `Remove-Item` together with a `C:\Program Files` path (split them);
  Git-Bash `sed` cannot add CR bytes, use PowerShell for CRLF conversion; repo files are CRLF in the working tree
  (`core.autocrlf=true`; verify CR count == LF count after writes); this SKILL.md is LF; the Edit tool works on CRLF files.
- Local files: transcripts `C:\Users\aivis\.claude\projects\c--git-MotoButtons2\*.jsonl`; memory notes in `...\memory\`;
  scratchpad `%LOCALAPPDATA%\Temp\claude\c--git-MotoButtons2\` (temporary).
- Branches: `esp32c3` (this), `nRF52840` (hardware-tested 2.1 original, skill in the same path), `esp32`, `esp32c3OLED`,
  `dev` (default), `main`; upstream = joncox123/MotoButtons2.

## 4. Library facts verified in the sources (NimBLE-Arduino 2.5.0, esp32 core 3.3.8)
- `NimBLECharacteristic::notify(value, len)` builds an mbuf per connected peer and calls `ble_gattc_notify_custom`;
  returns false on any error (e.g. `BLE_HS_ENOMEM` when the mbuf pool is exhausted) - this is the send check the retry
  logic relies on. It does NOT check the CCCD (sends even if the host has not subscribed). `notify()` without arguments
  uses `ble_gatts_chr_updated` (respects the CCCD) but always returns true. `setValue()` never sends.
- `NimBLEServer` handles `BLE_GAP_EVENT_REPEAT_PAIRING` itself: deletes the old bond and accepts the new pairing.
  `onAuthenticationComplete(connInfo)` runs on every encryption change; `connInfo.isEncrypted()` false = failed.
  The sketch then deletes that peer's bond (`NimBLEDevice::deleteBond(getIdAddress())`) and disconnects.
- `NimBLEDevice::deleteAllBonds()` loops `deleteBond()` over `getNumBonds()`; works while connected.
  `bleServer->getPeerDevices()` = vector of conn handles, `disconnect(handle)`, `getConnectedCount()`,
  `advertiseOnDisconnect(true)`.
- `NimBLEAdvertising::start()` starts the GATT server first; `NimBLEHIDDevice::startServices()` is deprecated.
  `getInputReport(id)` creates a READ|NOTIFY|READ_ENC characteristic with a report-reference descriptor. The library
  defines `HID_KEYBOARD` 0x03C1 but no `HID_KEY_*`/consumer usage constants: the sketch defines its own.
- GATT layout of 2.0 and 2.1 is identical (HID: report map, info, control, protocol mode, input reports 1+2; battery
  level; DIS: manufacturer + PnP). No re-pair should be needed; only the PnP version value changed (0x0200 -> 0x0210).
- Security: `setSecurityAuth(bonding=true, mitm=false, sc=true)`, IO cap NoInputNoOutput -> Just Works bonding.
- Preferences (NVS) namespace "motobuttons", keys mode/orientation/brightness (uint8). `begin(ns, true)` (read-only)
  fails when the namespace does not exist yet; `clear()` wipes the namespace; NVS survives a sketch upload.
- Arduino-ESP32 3.x `analogWrite` = LEDC, 8-bit, value 255 = fully high = LED off for common anode. `delay()` yields to
  FreeRTOS. `INPUT_PULLDOWN` and `INPUT_PULLUP` are both available on the C3.
- Consumer usages are uint16_t; keyboard report has 6 key slots; report 1 = 8 bytes (modifier, reserved, 6 keys),
  report 2 = 2 bytes little-endian usage.

## 5. Review findings from the 2.0 firmware and how they were fixed (all in 2.1, ported from the nRF52840 branch)
1. Sends were fire-and-forget (`notify()` always true) -> `notify(value,len)` checked, retry next loop (`forceKeyReport`).
2. Media keys re-sent every loop while held and key-down/key-up were 5 ms apart with a blocking delay -> edge-triggered,
   press held `CONSUMER_KEY_HOLD_MS` 50, release retried, volume/brightness repeat from the mode table.
3. A+B+C restarted the chip after clearing -> `deleteAllBonds()` + `Preferences::clear()` + disconnect, no restart.
4. Debounce 120 ms delayed every press until 120 ms of silence -> press after 2 samples, release after 50 ms open;
   `time` = press, `edge` = last raw change (hold timers no longer restart on bounce).
5. `flashLED` blocked the loop up to 2 s (orientation flashes) and the not-connected blink used `delay(200)` ->
   non-blocking flash state machine with one queued flash; blink is millis based.
6. Per-button globals (8 x 5 variables) and per-mode copy-pasted key mapping -> `Button` array + `MODES[]` table.
7. Directions repeated as key-up/down every 100 ms in DMD2 too; center bypassed in DMD2 -> repeat only where the
   table says so (DMD2: none), center blocks directions in every mode (center + direction = nothing, center fires on
   release).
8. Connect/disconnect handled inside the BLE task and the loop forced a report on connect -> callbacks set flags,
   handled in loop; key state reset on both, no forced report (host enables notifications seconds later).
9. Pairing failure with a stale bond -> `onAuthenticationComplete` drops that bond and disconnects.
10. A+C 5 s software restart (added by an earlier review pass) removed, matching the user's decision on the nRF branch.
11. Orientation selection did not match "held contact becomes UP" -> `ORIENTATION_PINS` lookup (see 2).
12. Docs: README lists the OsmAnd fast scroll, Media repeats, orientation rule, brightness off-pause, orange status
    LED, boot-pin warning and the BOOT+RESET recovery; Programming README now requires NimBLE-Arduino (the old text
    claimed no extra library) and says settings/bonds survive uploads.
- Not done on purpose: watchdog, unbonded-peer filtering, 80 ms combo grace (parked in 2.2), OTA.

## 6. Firmware 2.1 architecture (what is in the repo)
- Tables: `MODES[]` rows {id, color, KeyMap{key[8], consumer, Repeat rep[2]}}; row order = B+C cycle order.
  `Repeat {mask, delayMs, intervalMs, releaseMs}`: keyboard modes repeat as key-up (release) / key-down; consumer modes
  re-send the press. Combos are button masks (`MODE_CYCLE_COMBO` B+C 1000 ms, `BRIGHTNESS_COMBO` A+B 1000 ms,
  `BOND_RESET_COMBO` A+B+C 5000 ms); `comboActive` = exact set of A/B/C pressed; single A/B/C act only when alone.
- `Button {pin, activeLow, state, prior, flipped, time, edge, name}` array with `pressedMask()`, `comboHoldMs()`.
  Center fires on release (`centerTapPending`), so center + direction suppresses directions silently. Directions are
  held keys.
- HID transport: `sendKeyboardRaw(keys)` (setValue + notify on report 1), `sendConsumerRaw(usage)` (report 2, 0 =
  release). Consumer keys: press, release after `CONSUMER_KEY_HOLD_MS` 50, one at a time, release retried in any mode.
- LED: `COLOR_RGB` table indexed by enum; brightness 0..255 scales channels (255 = off); `startFlash(color,count,period)`;
  `applySteadyLED()` = mode color when connected, blink when not. All status flashes orange, BLE blink blue.
- Settings in NVS, written only on change; invalid or missing -> defaults + rewrite.
- BLE: `ServerCallbacks` sets connect/disconnect flags; `handleBLEEvents()` also compares `getConnectedCount()` so a
  missed callback cannot desync `BLE_connected`. Advertising: appearance keyboard, HID service UUID, name in scan
  response, preferred connection interval 7.5-22.5 ms (`BLE_CONN_INTERVAL_*` x 1.25 ms), TX +9 dBm.
- Debug-only serial tuning: `r <group> <intervalMs> <releaseMs>` overrides `rep[group]` timing live.

## 7. Device/app behaviour and tuned values (measured on the nRF52840 build with the same phone; re-check on the C3)
- OsmAnd keyboard scroll (MapScrollHelper source): key-down starts continuous scroll 1 px / 3 ms; a key held < 250 ms
  and released = instant 200 px jump (no animation). Fast scroll = tap stream. Tuned live: `{DIRECTIONS, 0, 45, 40}`
  (45 ms period, 40 ms release, ~22 jumps/s) = "perfect". Release < 40 ms -> jitter (missed key-ups); 35 ms period
  "too fast". The C3 connection interval (7.5-22.5 ms) differs from the nRF (11.25-20 ms); retune with `r 0 <i> <r>`
  if the scroll jitters.
- OsmAnd zoom A/B repeat `{A|B, 150, 150, 40}`. Faster zoom queues more zoom animations, so arrows fight them for ~1 s
  after zooming. Same after C (move to my location) during its centering animation.
- DMD2 (Enter = follow toggle) ignores pan input for 1-2 s while re-centering.
- Media: `{UP|DOWN|B|C, 400, 150, 0}` volume/brightness repeat; play/pause once. Android: first media key with no active
  player only wakes the player, second press plays. Brightness consumer keys may be ignored by some phones.
- Brightness combo pauses 1500 ms at "off" so releasing there is easy.
- Stray key on mode change: one button lands 4-60 ms before the other, so B+C types F7 / `-` / `c` before the combo forms
  (known, fix parked as 80 ms grace).
- Android enables HID notifications a few seconds after connecting; keys pressed before that are lost by design.

## 8. Parked
- Bluetooth firmware update: the nRF plan (adafruit-nrfutil zip hook, `BLEDfu`, Nordic DFU library) does NOT apply to
  the ESP32-C3. Not researched for this branch; candidates would be a custom NimBLE OTA characteristic writing to the
  OTA partition (`Update.h`) plus a phone app, or ArduinoOTA over WiFi. Adding any service = phone must re-pair.
- Parked 2.2 features the user approved on the nRF branch (one per flash, logger test first, then he flashes from the IDE):
  - 80 ms combo grace: lone A/B/C key-down waits `COMBO_GRACE_MS`; a shorter tap is sent as key-down + key-up on release.
    Sketch: `tapPending` mask (replaces centerTapPending; center = tap on release), `comboSeen` mask (buttons that were
    part of >=2 pressed combo buttons: no tap on release), `consumerSent` mask for consumer edge sends, `keyActive()`
    includes the press delay, send also when the active set changes (not only on state change).
  - Media hold keys: `HoldKey {btn,key}` per mode; LEFT/RIGHT tap = prev/next, held `HOLD_KEY_MS` 400 = REWIND 0xB4 /
    FAST_FORWARD 0xB3, repeating via a second repeat group `{LEFT|RIGHT, 400, 150, 0}`.
  - Key-press feedback: LED dark `KEY_FEEDBACK_OFF_MS` 150 on every press; toggled by center+A held 3 s (yellow flashes
    confirm: 3 = on, 1 long = off); off by default; saved as a 4th NVS key.

## 9. Test procedure that worked (nRF branch; same here)
Stop any logger -> flash debug build -> start logger -> user does a numbered button sequence with the phone as receiver
and reports what the phone showed -> compare with log timestamps (press to "Report sent" is <1 ms when healthy) ->
fix -> reflash. Tune repeat timing live with the serial `r` command ("better/same/worse" per step) instead of
reflashing. Finish by flashing the release build (DEBUG false), stopping the logger, confirming the COM port is free.
First things to verify on the C3 with 2.1: all 8 inputs give exactly one press/release pair (CENTER is the only
active-low input), orientation selection at power-on, bond reset without reboot, OsmAnd scroll timing, media repeats.
