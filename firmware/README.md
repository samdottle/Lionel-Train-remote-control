# Firmware — FW v1.13

Matched PWA: **UI v1.5**

Authoritative sketch:

`Lionel_O27_Controller_Nano_ESP32_FW_v1_13/Lionel_O27_Controller_Nano_ESP32_FW_v1_13.ino`

## Board
Arduino Nano ESP32.

## Throttle
MG90S positional servo on `D2`.

- HOME/OFF: `1000 us`
- 100%: `2200 us`
- Startup HOME PWM hold: `1200 ms`, then PWM is detached.
- First later movement automatically re-attaches PWM.
- Native ESP32 LEDC hardware is used; no ESP32Servo library is required.

## Relays
- Reverse: `D6`, active HIGH through 2N2222 driver.
- Whistle: `D7`, active HIGH through 2N2222 driver.

## Local controls
- Local/Remote: `D8`
- Local throttle: `A0`
- Remote→Local uses soft pickup.

## Serial
115200 baud.
