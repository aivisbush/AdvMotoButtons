# Joystick input: why it is read with the ADC

Short version: the joystick pod leaks. Idle lines do not rest at 3.3 V, they rest somewhere in the
middle, and a digital input cannot tell that apart from a press. Measuring the voltage can.

## The symptom

Phantom presses - directions reported as held for seconds at a time with nothing touched. Bad
enough that a phantom press at boot silently rewrote the saved joystick orientation.

## The measurements

Captured with `DEBUG_INPUTS true` on the bench:

```
[    4420] UP=H/2984mV  DOWN=L/846mV   LEFT=L/1860mV  RIGHT=H/1734mV  CENTER=H/2984mV  A=H B=H C=H
[   24430] UP=H/2984mV  DOWN=L/1696mV  LEFT=L/1219mV  RIGHT=H/2061mV  CENTER=H/2984mV  A=H B=H C=H
```

| Line | Idle reading | Meaning |
|---|---|---|
| CENTER (GPIO1), UP (GPIO2) | 2984 mV, rock steady | healthy; 2984 mV is the ADC ceiling on this chip, i.e. sitting at the 3.3 V rail |
| DOWN (GPIO4), LEFT (GPIO0), RIGHT (GPIO3) | 700-2200 mV, wandering | leakage of roughly 40-70 kOhm to ground, against the ~45 kOhm internal pull-up |
| any line, real press | **3-5 mV** | contact closed, shorted to the common |

## Why digital reads cannot work here

On the ESP32-C3 anything between about 0.8 V and 2.5 V is the input's **undefined band**. The leaky
lines sit in the middle of it, so `digitalRead()` effectively returns a coin flip. Debouncing,
majority sampling and longer confirmation windows cannot repair a reading that was never valid -
they were all tried and all failed.

The gap between a real press (3-5 mV) and a leaky idle line (700+ mV) is a factor of 100, so a
measurement separates them trivially.

## What the firmware does now

All of this lives in [inputs.cpp](../ArduinoCode/MotoButtons2/inputs.cpp); the thresholds are in
[config.h](../ArduinoCode/MotoButtons2/config.h).

- `primeJoystickPin()` puts GPIO0..GPIO4 into analog mode once during `inputsBegin()` and asserts
  the internal pull-up, so an open contact still rests at the rail.
- `assertJoystickPullups()` re-asserts all five pull-ups once per scan, then the scan settles 200 us
  before `readJoystickMillivolts()` takes the median of three `analogReadMilliVolts()` conversions
  per line.
- `readJoystickPressed()` applies hysteresis: **pressed at or below `JOYSTICK_PRESS_MV` (200 mV),
  released at or above `JOYSTICK_RELEASE_MV` (500 mV)**, and the band in between keeps the previous
  state. 200 mV is 40x above a real press and 3.5x below the lowest phantom observed.
- A/B/C are on GPIO7/9/10, which have no ADC, and stay on a plain digital read.
- On top sit the debounce (50 ms for the joystick, 40 ms for A/B/C, `DEBOUNCE_*_MS`) and the
  rejection of physically impossible direction pairs in `directionActive()`.

The debounce used to be 120 ms. That number was raised while the phantom presses were still being
fought digitally; with the millivolt thresholds doing the real work it has come back down, which
takes most of the latency out of every press.

### The trap that broke the first attempt

An earlier version also used `analogRead()` and was *worse* than digital. The reason: **attaching
the ADC to a pad clears its internal pull-up.** That left every joystick line floating with nothing
holding it up, so readings drifted anywhere, frequently near zero, which looked exactly like
presses. Re-asserting `gpio_pullup_en()` after attaching the ADC, and again before each scan, is the
whole difference between the two versions.

That version also classified a reading of 0 as *released*, which threw away the one value a real
press actually produces.

## Caveats

This is a workaround for a hardware fault, not a repair.

- If the leak worsens past roughly 3 kOhm, an idle line drops below 200 mV on its own and the
  phantoms return.
- If a leak path ever appears between a line and the common, a real press could read high and be
  missed.
- The proper fixes remain: clean and dry the pod (isopropyl alcohol, then dielectric grease), or
  fit 10 kOhm pull-ups per line to 3V3 - a single bussed resistor network is one component. A
  resistor in the shared ground cannot substitute: pull-ups are inherently per line, and a series
  resistor in the common only creates cross-talk between buttons.

## Reproducing the diagnostics

Set `DEBUG_INPUTS` to `true` in `config.h`, flash, open the serial monitor at 115200. Output is a
snapshot every second plus a line on every debounced transition, e.g.

```
[   10931] UP     GPIO2  PRESSED   previous state held  10931 ms  line 1660 mV
```

Healthy hardware reads ~2984 mV on every idle line and a few mV while a direction is held.
