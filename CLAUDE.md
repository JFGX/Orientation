# Orientation (PicoW)

Uses the rpi-harness bench (PicoW, portal 192.168.0.198:8081). Spec: `FSD.md`.

## Every session
Before working on a new FSD task, redo tasks 1 and 2, even though they're marked complete:
1. Check that the file references in `FSD.md` are valid and useful.
2. Check the bench and wiring: `python tools/bench.py devices`, then `python tools/bench.py swd`, then
   `python tools/bench.py deploy firmware/conncheck --until "check: done"` (look for `check: BNO085 PASS`).

## Firmware
- `firmware/conncheck` — FSD task 2: I2C scan, BNO085 IDs + one gyro report, WS128 colour bars.
  Pass line: `check: BNO085 PASS`, end line: `check: done`.
- `firmware/orientation` — FSD task 4 (task 3 source kept in `firmware/orientation/task3-main.cpp.txt`):
  BNO085 game rotation vector (quaternion, no magnetometer) -> Pitch (about X) / Roll (Y) / Yaw (Z)
  in degrees, relative to a zero orientation, shown at 2 Hz. Gyro sampled at 2 Hz; all axes
  < 1 deg/s for 5 s -> angles zeroed (`zero: angles initialised...`), then no re-zero until an
  axis exceeds 1 deg/s (`zero: re-armed...`). Each update is echoed as `show: ...` on Serial1.
  After an SWD flash the BNO085 sometimes isn't found at boot; the 5 s retry picks it up.

```
python tools/bench.py deploy firmware/orientation --until "zero: angles initialised"
python tools/bench.py log --seconds 5
```

## Extra wiring (beyond the bench)
- BNO085 (Adafruit breakout) on I2C0: SDA GP4 (pin 6), SCL GP5 (pin 7), VIN from VBUS. RST/INT not
  wired, so the sensor is polled. Address 0x4A (0x4B tried as fallback).
- WS128 (GC9A01) on software SPI: see `WS128ToPicoWPins.md`. Adafruit's software SPI is far too
  slow (~1.2 s for three 200x40 rows), so `orientation` pushes pixels with a direct SIO bit-bang
  (`fastWrite16`, ~73 ms). Keep sampling in `service()` so a redraw can't skip a reading.

## Bench caveat
The Pi has rebooted itself twice right after a flash, with under-voltage logged (`throttled=0x50000`).
If the bench becomes unreachable after a deploy, wait ~2 min for it to come back before asking the user.
