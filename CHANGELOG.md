# Changelog

## Proto V1.0 — 2026-09-04
**UI v1.5 · FW v1.13**
- Full 0–100 UI dial.
- Startup HOME servo holding torque released after 1200 ms.
- First later throttle movement automatically restores servo PWM.

## FW v1.12
- Increased MG90S 100% endpoint from 1800 us to 2200 us.

## FW v1.11
- Replaced 28BYJ-48/ULN2003 throttle with MG90S on D2.
- Eliminated mechanical-stop stepper homing.
- Added Remote→Local soft pickup and bumpless Local→Remote handoff.
- 1000 us established as clockwise HOME/OFF.

## FW v1.10
- Active-HIGH D6/D7 relay outputs for 2N2222 low-side relay drivers.

## UI v1.4
- Throttle BLE command sent only when slider is released.

## UI v1.3
- Serialized Web Bluetooth writes to prevent overlapping GATT operations.
- Added visible Reverse press state.

Earlier project history is omitted from this repository.
