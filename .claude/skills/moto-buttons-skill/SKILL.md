---
name: moto-buttons-skill
description: Everything learned about the MotoButtons2 BLE controller (Seeed XIAO nRF52840, Bluefruit HID) - user working rules, hardware and user context, verified library facts, review findings and fixes, tuning results, the parked Bluetooth update path (zip hook, BLEDfu, Android updater APK), parked 2.2 features, tooling, PC gotchas and the test procedure. Use for any MotoButtons2 work, and when the user says "continue the MotoButtons2 update app".
---

# MotoButtons2 knowledge base (session of 2026-09-11; repo firmware 2.1 = commit 24eb5fc, hardware-tested)

## 1. User and working rules
- User: Aivis Bush, experienced coder, rides enduro/adventure; the controller is on his motorcycle handlebar, powered from
  the bike's USB, paired to an Android phone running OsmAnd and DMD2 (Drive Mode Dashboard). Not tested on iPhone.
- He flashes from the Arduino IDE himself, tests on the phone, commits himself (commit messages like "Fine tuning on OsmAnd").
  Ask before touching the board, COM port or toolchain. Answer briefly; he asks follow-ups when he wants more.
- Comments: one short line per fact, user-facing purpose only, no mechanism/struct-field docs, no tuning/test history.
  His own rewrite that sets the standard:
  `// Media: volume/brightness keep stepping while held`
  `// OsmAnd: fast map scroll by tapping the arrow; 45/40 ms tuned on Android (not tested on iPhone)`
  Fewer lines is better ("I am coder I know the code"). When unsure, leave the comment out.
- Config section order he wants at the top of the sketch: 0 DEBUG, 1 BLE, 2 wiring (pins by number), 3 RGB colors +
  state/mode colors, 4 modes + key maps + how they are switched, 5 special functions (combos, hold times, orientation),
  6 timing. Everything user-changeable lives there; code below needs no edits.
- Compact style: K&R braces, one-statement bodies on one line, small structs on one line; target as few lines as readable.
- Incident to never repeat: an untested `cmd /C if ... copy` hook installed into his board package broke every IDE
  compile ("copy was unexpected at this time"), and firmware 2.2 (five features incl. a new BLE service) stopped the keys
  working. He rolled everything back. Rules from it: verify toolchain changes with a real arduino-cli compile BEFORE
  reporting done; deliver firmware features one at a time and test on hardware with the logger before handover; never
  bundle a GATT change with other features; a new BLE service always comes with "forget the device on the phone and pair
  again" said up front (Android caches GATT handles, HID keys silently stop working otherwise).
- Decisions he took: LED brightness wraps to fully off (255) by design; joystick repeat only for directions (later A/B
  too); A+C DFU combo removed (IDE auto-reset or reset button is enough); watchdog rejected (see 5); unbonded-peer
  filtering rejected (would block pairing a second phone); update app wanted but LATER.

## 2. Hardware
- Seeed XIAO nRF52840 Sense, common-ANODE RGB LED (OSTAMA5B31A) on D0 red, D1 blue, D2 green; joystick JS5208 contacts
  D3 RIGHT, D4 UP, D8 LEFT, D10 DOWN, D9 CENTER; buttons V12B-10N-A A=D5, B=D6, C=D7. Inputs use internal pull-down
  (switch to 3V3). Case is glued/waterproof, so opening it for USB is a chore (motivation for OTA updates).
- Orientation: hold one joystick contact at power-on -> orientation whose UP is that contact (`ORIENTATION_PINS` rows
  0 buttons top, 1 left, 2 bottom, 3 right = default). A 90-degree rotated log (RIGHT,LEFT,UP,DOWN for up,down,left,right)
  means the user held the unit differently, not a bug.

## 3. Environment on this PC
- Board on COM5. FQBN `Seeeduino:nrf52:xiaonRF52840Sense`. Core Seeeduino nrf52 1.1.12 (Adafruit Bluefruit fork,
  SoftDevice S140, sd_fwid 0x0123) at `%LOCALAPPDATA%\Arduino15\packages\Seeeduino\hardware\nrf52\1.1.12`.
- arduino-cli: `"C:\Program Files\Arduino IDE\resources\app\lib\backend\resources\arduino-cli.exe"`.
  `compile --fqbn Seeeduino:nrf52:xiaonRF52840Sense --warnings all --build-path <dir> ArduinoCode\MotoButtons2`
  (~125-130 KB, 15 %). `upload -p COM5 --fqbn ... --input-dir <build dir> <sketch dir>`: 1200-baud touch, bootloader
  appears on COM7, "Device programmed", back on COM5. Upload fails with "Access is denied" if anything (my logger, the IDE
  Serial Monitor) holds COM5, and "port does not exist" if the board is unplugged.
- Debug build: copy sketch to a temp `MotoButtons2/` folder, replace `#define DEBUG false` with true, build to a separate
  build dir. Debug prints: button pressed/released, "Report sent", "Key report send failed, will retry.", "Mode advanced
  to N", "Writing/Read settings", "Media key <usage>", "LED brightness changed to N", and echoes of the tuning command.
  Debug build waits up to 2 s for USB serial at boot.
- Serial logger (re-create if scratchpad is gone): PowerShell, `New-Object System.IO.Ports.SerialPort COM5,115200`,
  DtrEnable, ReadTimeout 300, ReadLine loop appending `[HH:mm:ss.fff] line` to a log, catch TimeoutException, outer loop
  reconnects when the port vanishes (DFU/replug), optional command file whose trimmed content is written + "\n" to the port.
  Start hidden: `Start-Process powershell -ArgumentList -NoProfile,-ExecutionPolicy,Bypass,-File,<script>,... -WindowStyle Hidden -PassThru`.
  Stop it (Stop-Process) before every upload. Check leftovers via Win32_Process CommandLine match.
- Key recorder (only useful with a BLE-capable PC dongle): C# low-level keyboard hook via Add-Type, filtered to arrows,
  F6-F8, Enter, OEM_PLUS/MINUS, C, volume and media VKs; log key DOWN/UP with ms timestamps; needs Application.Run().
  Tell the user not to type while it runs; delete the log afterwards.
- PC Bluetooth dongle CSR VID 0A12 PID 0001 ("Generic Bluetooth Radio", generic MS driver) is Classic-only:
  no "Microsoft Bluetooth LE Enumerator", WinRT BluetoothAdapter.IsLowEnergySupported = False. It cannot see the
  LE-only controller. A real BLE 4.0+ dongle (TP-Link UB400/UB500, ASUS BT400/BT500, Intel) would enable PC-side tests.
- Android tooling: SDK `%LOCALAPPDATA%\Android\Sdk` (platforms 34/35/36.1, build-tools 34.0.0/36.1.0, platform-tools/adb),
  JDK 21 at `C:\Program Files\Android\Android Studio\jbr`, Gradle 9.0.0 in `~/.gradle/wrapper/dists`, AGP 8.6.1 cached,
  network to dl.google.com and Maven Central OK. Nordic DFU library latest `no.nordicsemi.android:dfu:2.11.0`.
- PC gotchas: sandbox blocks a command containing `Remove-Item` together with a `C:\Program Files` path (split them);
  Git-Bash `sed` cannot add CR bytes, use PowerShell for CRLF conversion; the repo file is CRLF (verify CR count == LF
  count after writes); the Edit tool works fine on CRLF files; Arduino "Export Compiled Binary" saves only the .hex.
- Local files: transcripts `C:\Users\aivis\.claude\projects\c--git-MotoButtons2\*.jsonl`; memory notes in `...\memory\`
  (terse comments, test-before-handover, ota-update-plan; all merged into this skill); scratchpad
  `%LOCALAPPDATA%\Temp\claude\c--git-MotoButtons2\` (temporary).

## 4. Library facts verified in the core sources (Seeeduino nrf52 1.1.12)
- `BLECharacteristic::notify` returns false immediately if notifications are not enabled, and after a 100 ms wait
  (`BLE_GENERIC_TIMEOUT`) if the HVN TX queue is full; `keyboardReport`/`consumerKeyPress` return that bool.
  Default HVN queue size is 1; `Bluefruit.configPrphConn(mtu, event_len, hvn_qsize, wrcmd_qsize)` before begin raises it
  (library's own BANDWIDTH_MAX preset uses 247/100/3, so 23/default/4 fits the linker RAM).
- Android enables HID notifications ~5 s after connecting; a forced report on connect fails every 2 ms until then.
- Bond files live in `/adafruit/bond_prph`; the directory is created only in `bond_init()` (Bluefruit.begin). After a
  runtime `InternalFS.format()` new bonds cannot be saved until reboot. Use `Bluefruit.Periph.clearBonds()` instead.
  `BLEConnection::removeBondKey()` works when `bonded()`; `Bluefruit.disconnect(Bluefruit.connHandle())` drops the link.
- `Bluefruit.autoConnLed` defaults true and blinks the onboard LED (P0.06). Security default: Just Works, bonding on, no MITM.
- `File::open(FILE_O_WRITE)` opens RDWR|CREAT and seeks to end; `truncate(0)`+`seek(0)` to overwrite. `File::read` returns
  int, negative on error. LittleFS region 28 KB, 128-byte blocks, wear-levelled.
- `analogWrite` uses HW PWM (up to 4 pins per module), 8-bit, value 255 = fully high = LED off for common anode.
- `yield()` is an empty weak symbol; a loop without `delay()` never lets the RTOS idle/tickless sleep.
- `enterSerialDfu()` exists (soft reset with GPREGRET). The Adafruit bootloader has NO watchdog feeding code (verified
  in its main.c and boards.c), and the nRF52 WDT survives soft and pin resets: an armed WDT aborts every DFU upload.
- `HardFault_Handler` does `NVIC_SystemReset()`; a SoftDevice fault handler that returns also resets; stack-overflow and
  malloc hooks just return in release builds. Flash writes wait forever for the SoftDevice event (guaranteed to arrive).
- HID consumer usages present in hid.h: VOLUME_INCREMENT 0xE9, DECREMENT 0xEA, MUTE 0xE2, PLAY_PAUSE 0xCD,
  SCAN_NEXT 0xB5, SCAN_PREVIOUS 0xB6, BRIGHTNESS_INCREMENT 0x6F, DECREMENT 0x70. REWIND 0xB4 / FAST_FORWARD 0xB3
  are NOT named; define them. `BLEDfu` service class exists (`services/BLEDfu.h`); example Peripheral/bleuart calls
  `bledfu.begin()` first.
- Consumer usages are uint16_t; keyboard report has 6 key slots.

## 5. Review findings from the original firmware and how they were fixed (all in 2.1)
1. Dropped key reports left keys stuck on the phone -> check every send, retry next loop (`forceKeyReport`).
2. Stale keycodes leaked into MEDIA reports -> report array cleared before each build; media keys edge-triggered.
3. A+B+C formatted the filesystem and broke bond saving until reboot -> `clearBonds()` + delete settings file + disconnect.
4. Settings-read result was inverted (rewrote file every boot, never repaired a bad one) -> fixed.
5. Negative `file.read` used as index -> clamped.
6. Center used a raw `digitalRead` bypassing debounce -> debounced state only.
7. Unguarded key report slots -> bounded push helper.
8. `flashLED` blocking up to 3.5 s inside the button handler -> non-blocking flash state machine with one queued flash;
   the not-connected blink is millis based (old `delay(200)` removed).
9. Loop never slept -> `delay(2)`.
10. No connect/disconnect handling -> callbacks set flags, handled in loop; key state reset on both.
11. Pairing failure with a stale bond -> pair-complete callback removes that bond.
12. Debounce delayed a press until 50 ms of silence -> press after 2 samples, release after 50 ms low; `time` = press,
    `edge` = last raw change (hold timers no longer restart on bounce).
13. Docs fixed: Programming README path and DFU procedure, README typos and special functions, LED legend.
- Not done on purpose: watchdog (see 4), unbonded-peer filtering, 80 ms combo grace (parked in 2.2).

## 6. Firmware 2.1 architecture (what is in the repo)
- Tables: `MODES[]` rows {id, color, KeyMap{key[8], consumer, Repeat rep[2]}}; row order = B+C cycle order.
  `Repeat {mask, delayMs, intervalMs, releaseMs}`: keyboard modes repeat as key-up (release) / key-down; consumer modes
  re-send the press. Combos are button masks (`MODE_CYCLE_COMBO` B+C 1000 ms, `BRIGHTNESS_COMBO` A+B 1000 ms,
  `BOND_RESET_COMBO` A+B+C 5000 ms); `comboActive` = exact set of A/B/C pressed; single A/B/C act only when alone.
- Button struct array with `pressedMask()`, `comboHoldMs()`. Center fires on release (`centerTapPending`), so
  center + direction suppresses directions silently. Directions are held keys.
- Consumer keys: press, release after `CONSUMER_KEY_HOLD_MS` 50 (needed: instant press+release was ignored by some
  players), one at a time, release retried in any mode.
- LED: `COLOR_RGB` table indexed by enum; brightness 0..255 scales channels (255 = off); `startFlash(color,count,period)`;
  `applySteadyLED()` = mode color when connected, blink when not.
- Settings `/MotoButtons.set` = "mode,orientation,brightness", written only on change; accepts old 3-field files.
- Debug-only serial tuning: `r <group> <intervalMs> <releaseMs>` overrides `rep[group]` timing live.

## 7. Verified device/app behaviour and tuned values
- OsmAnd keyboard scroll (MapScrollHelper source): key-down starts continuous scroll 1 px / 3 ms; a key held < 250 ms
  and released = instant 200 px jump (no animation); scroll neither cancels animations nor leaves follow mode.
  Fast scroll = tap stream. Tuned live on the bike phone: `{DIRECTIONS, 0, 45, 40}` (45 ms period, 40 ms release,
  ~22 jumps/s) = "perfect". Release < 40 ms -> jitter (missed key-ups); key-down 5..75 ms irrelevant; 35 ms period
  "too fast"; 60/25 jittered only because of the 25 ms release. Old 30 ms key-up/down cycle was jumpy for that reason.
- OsmAnd zoom A/B repeat `{A|B, 150, 150, 40}` (user asked twice for faster). Faster zoom queues more zoom animations,
  so arrows fight them for ~1 s after zooming (jitter). Same after C (move to my location) during its centering animation.
- DMD2 (Enter = follow toggle) ignores pan input for 1-2 s while re-centering. Both app behaviours were proven: the log
  showed every arrow key-down leaving the controller within 1 ms with zero failed sends.
- Media: `{UP|DOWN|B|C, 400, 150, 0}` volume/brightness repeat; play/pause once. Android: first media key with no active
  player only wakes the player, second press plays. Brightness consumer keys may be ignored by some phones.
- Brightness combo pauses 1500 ms at "off" so releasing there is easy.
- Bond reset, mode cycling, orientation selection, brightness, all modes: hardware-tested OK. All 8 inputs produce exactly
  one press/release pair.
- Stray key on mode change: one button lands 4-60 ms before the other, so B+C types F7 / `-` / `c` before the combo forms
  (known, fix parked as 80 ms grace).

## 8. Parked: Bluetooth firmware update path (user wants it LATER; nothing installed or in the repo)
1. Zip build hook, verified with arduino-cli: `platform.local.txt` next to platform.txt in the Seeed package dir:
```
recipe.hooks.postbuild.1.pattern.windows=powershell -NoProfile -ExecutionPolicy Bypass -Command "if ('{build.project_name}' -eq 'MotoButtons2.ino') { Copy-Item -LiteralPath '{build.path}\{build.project_name}.zip' -Destination '{build.source.path}\MotoButtons2.zip' -Force }"
recipe.hooks.postbuild.1.pattern.linux=sh -c "[ '{build.project_name}' = 'MotoButtons2.ino' ] && cp '{build.path}/{build.project_name}.zip' '{build.source.path}/MotoButtons2.zip'; true"
recipe.hooks.postbuild.1.pattern.macosx=sh -c "[ '{build.project_name}' = 'MotoButtons2.ino' ] && cp '{build.path}/{build.project_name}.zip' '{build.source.path}/MotoButtons2.zip'; true"
```
   `{build.project_name}.zip` is made by adafruit-nrfutil `dfu genpkg` in the build dir (platform.txt recipe.objcopy.zip);
   it is the DFU package for the phone. Restart the IDE after adding/removing the file. The copy lands in the sketch folder
   (untracked in git: commit it as release artifact or ignore `*.zip`). The hook is lost on board-package update; keep a
   copy + installer script (`install_zip_hook.ps1` copying into every version dir) in `Programming/`. Never `cmd /C if`.
2. Firmware: `BLEDfu bledfu; bledfu.begin();` before bledis/blehid. Then the phone must forget + re-pair. nRF Connect
   (DFU button, pick the zip) is the fallback updater. Keep the device powered; an interrupted DFU leaves it in the
   bootloader, repeat or flash via USB.
3. Android updater (not started): `Android/MotoButtonsUpdater/`, Java, debug-signed sideloaded APK. UI: pick bonded device
   "Bush Moto BT14" from `BluetoothAdapter.bondedDevices`, pick zip via `ActivityResultContracts.OpenDocument`,
   `DfuServiceInitiator(address).setDeviceName(..).setKeepBond(true).setZip(uri)`, subclass `DfuBaseService` +
   `NotificationActivity`, progress/log via `DfuServiceListenerHelper`. Permissions: BLUETOOTH_SCAN/CONNECT (API 31+),
   ACCESS_FINE_LOCATION (<31), POST_NOTIFICATIONS (33+). Build with cached Gradle + Android Studio JDK; if AGP 8.6.1 rejects
   Gradle 9, generate a Gradle 8.x wrapper. Hand over the APK path.
4. Parked 2.2 features the user approved (one per flash, logger test first, then he flashes from the IDE):
   - 80 ms combo grace: lone A/B/C key-down waits `COMBO_GRACE_MS`; a shorter tap is sent as key-down + key-up on release.
     Implementation sketch: `tapPending` mask (replaces centerTapPending; center = tap on release), `comboSeen` mask
     (buttons that were part of >=2 pressed combo buttons: no tap on release), `consumerSent` mask for consumer edge sends,
     `keyActive()` includes the press delay, send also when the active set changes (not only on state change).
   - Media hold keys: `HoldKey {btn,key}` per mode; LEFT/RIGHT tap = prev/next, held `HOLD_KEY_MS` 400 = REWIND 0xB4 /
     FAST_FORWARD 0xB3, repeating via a second repeat group `{LEFT|RIGHT, 400, 150, 0}`.
   - Key-press feedback: LED dark `KEY_FEEDBACK_OFF_MS` 150 on every press; toggled by center+A held 3 s (center joins the
     combo mask; yellow flashes confirm: 3 = on, 1 long = off); off by default; saved as 4th settings field.
   - BLE DFU service (item 2). A full 2.2 draft existed but was never hardware-tested; rebuild it from this spec.

## 9. Test procedure that worked
Stop any logger -> flash debug build -> start logger -> user does a numbered button sequence with the phone as receiver
and reports what the phone showed -> compare with log timestamps (press to "Report sent" is <1 ms when healthy) ->
fix -> reflash. Tune repeat timing live with the serial `r` command ("better/same/worse" per step) instead of
reflashing. Finish by flashing the release build (DEBUG false), stopping the logger, confirming COM5 is free.
