# Proto V1.0 Wiring Reference

## Arduino Nano ESP32

| Nano pin | Function |
|---|---|
| D2 | MG90S servo signal |
| D6 | Reverse relay driver input |
| D7 | Whistle relay driver input |
| D8 | Local/Remote switch |
| A0 | Local throttle pot |

## MG90S
- Signal → D2
- Red → regulated +5 VDC
- Brown/black → GND
- Nano ground and servo-supply ground must be common.
- HOME/OFF is 1000 us, full clockwise viewed from above.
- 100% command is 2200 us.

## Each G6N-2-Y 5 V relay driver

```text
Nano D6 or D7 ---- 1 kΩ ---- Base, 2N2222

+5 V ---- relay coil ---- Collector, 2N2222
                            Emitter ---- GND

Flyback diode across relay coil:
  cathode / striped end ---- +5 V
  anode -------------------- collector / coil low side
```

Firmware logic is active-HIGH:
- GPIO HIGH → 2N2222 ON → relay energized.
- GPIO LOW → 2N2222 OFF → relay released.

Do not drive the relay coil directly from the Nano GPIO.

## Local/Remote
The firmware assumes the present switch wiring makes D8 LOW = Local using
`INPUT_PULLUP`.

## Legacy wiring document
The previous stepper-era `.docx` wiring document is retained under
`archive/docs/` and is not current for Proto V1.0.
