# FW v1.16 SG DRAFT + UI v1.7 SG DRAFT - what changed (Nov 13 2026)

Provenance: SG
Status: DRAFT. Compiled, but **UNTESTED on the real controller or train.** Nothing here is approved, final, released or submitted.
Earlier files (FW v1.13 old.ino, FW v1.15, UI v1.5, UI v1.6) were not modified.

## Why
Your bench numbers: the engine needs about 4 VAC to start moving, which is 73% on the v1.15 slider (about 1876 us), and 100% (2200 us) only gave 10.6 VAC. So the lower three quarters of the slider did nothing and the top end is probably short of the voltage you want (you estimate ~15 VAC). v1.16 adds two more saved calibration points so the slider has no dead zone and 100% can be tuned.

## Files
- fw_v1_16/fw_v1_16.ino  (the folder name must stay fw_v1_16 for the Arduino IDE)
- Lionel_UI_v1_7_index.html  (box source: ui_v1_7/index.html)
- CHANGES_v1_16.md  (this file)

## Firmware changes (FW v1.16)
1. **Three saved points** in flash (NVS namespace `o27`):

| Point | Key | Default | Meaning |
|---|---|---|---|
| HOME | `homeUs` (same as v1.15) | 1000 us | 0% = park = zero track volts |
| START | `startUs` (new) | 1876 us | 1% = engine just begins to move (~4 VAC) |
| MAX | `maxUs` (new) | 2200 us | 100%. Default is v1.15's safe value; you calibrate it upward. |

2. **Limits, enforced on load, on nudge and on save** (HOME < START < MAX always):
   - HOME 900..2000 us, and at most START - 20
   - START at least HOME + 20 and at most MAX - 100
   - MAX at least START + 100, hard cap **2500 us**
   - The servo's absolute clamp is now 900..2500 us.
   - On load, a stored value that breaks the order is moved to the nearest legal value (the later point moves) and Serial says so. A HOME saved with v1.15 is kept (then clamped).
   - While you nudge one point, its limits use the SAVED values of the other two. So save HOME, then START, then MAX if you move them a long way.
3. **Throttle mapping**: 0% = HOME exactly. 1..100% = straight line from START to MAX (1% = START, 100% = MAX). The same mapping is used for the servo target, the status "throttle" value, and the Local-pot pickup. The slider and the add-on pot still work in whole percent, so pickup at 0% works as before.
4. **Slew**: 6 us per 20 ms between START and MAX (unchanged). *My judgment call:* while the pulse is below START (the dead zone, ~876 us wide, where the engine does not move) it moves 18 us per 20 ms (3x faster), so going from 0% to 1% responds in about 1 s instead of about 3 s. To get exactly v1.15 behavior, set `SERVO_GAP_SLEW_STEP_US` to 6 in the sketch.
5. **Status packet** keeps `throttle`, `mode`, `cal`, `home` and adds `start`, `max`, `calTarget` (0 HOME / 1 START / 2 MAX; 0 when not calibrating). While calibrating, the selected point shows its working (unsaved) value. Example: `{"throttle":0,"mode":"remote","cal":1,"home":1000,"start":1876,"max":2260,"calTarget":2}` (about 92 bytes at most; v1.15's was about 50, so this still relies on Bluetooth MTU negotiation, untested). There is no `fw` field; the UI detects v1.16 by the presence of `start` and `max`.
6. Banner and Serial prints say FW v1.16 / UI v1.7.
7. Unchanged: startup HOME hold and PWM release, pot 16-sample average and 1% deadband, 3% pickup tolerance, relays, all BLE UUIDs, slider protocol.

## BLE calibration commands (write to 0027aaa5-0001-4000-8000-00805f9b0027)
Same characteristic as v1.15, backward compatible. Byte 0 = command, optional byte 1 = argument.

| Bytes | Meaning |
|---|---|
| `01` | ENTER calibration. Always starts on target HOME (so v1.15-style clients work unchanged) |
| `02` | CANCEL: leave, discard the unsaved value; servo goes back to 0% = saved HOME |
| `03` | SAVE: re-check the order, write ONLY the selected point to flash, then leave calibration (back to 0%) |
| `04` | RESET: selected point's working value = its default (1000 / 1876 / 2200, clamped to the order rules). Not saved until SAVE |
| `05 dd` | NUDGE the selected point by signed int8 `dd` us (limited to +/-50, then to that point's legal range). + = more throttle |
| `06 tt` | SELECT target: `00` HOME, `01` START, `02` MAX. The servo drives to that point's value. An unsaved edit of the previously selected point is discarded. Other values are ignored |

Anything except ENTER is ignored when not calibrating. A Bluetooth disconnect while calibrating = CANCEL. While calibrating, the slider and the add-on pot are ignored.

## UI changes (UI v1.7)
- The calibration card now has a **HOME / START / MAX** selector, each button showing its value in us (from the status packet), with a one-line hint:
  - HOME: 0% park position, zero track volts
  - START: 1% position, train just begins to move (about 4 VAC)
  - MAX: track voltage at 100%, ~15 VAC target, capped at 2500 us
- Buttons: Enter Calibration, -50 -10 -1 +1 +10 +50, Save, Cancel, Reset (to the selected point's default). All act on the selected point. (I added -50/+50 because MAX may need to move a few hundred us; the firmware allows up to 50 per press.)
- If you pick START or MAX before Enter, the app sends Enter, then `06 tt` right after.
- If the controller is on FW v1.15 (status has no `start`/`max`), START and MAX are greyed out with "START/MAX need FW v1.16"; HOME still works. With FW v1.13 (no calibration characteristic) everything stays greyed out as before.
- Throttle slider and Emergency Stop remain disabled while calibrating.
- Save toast names the point, e.g. "MAX saved: 2400 us".
- **Service worker / caching:** index.html itself contains no cache name; it just registers `./sw.js`. The repo's `sw.js` will need its cache name bumped to `o27-v1.7` when this is published, or Android may keep serving v1.6. **I did not publish anything and did not touch GitHub.**

## How to calibrate (step by step) - do it in this order
Keep the train on the track, track power on, and be ready to cancel.
1. Flash FW v1.16 (Arduino IDE, board Arduino Nano ESP32). Open the v1.7 UI (after it is published, or open Lionel_UI_v1_7_index.html in Chrome) and Connect. You should see HOME 1000 / START 1876 / MAX 2200 the first time. If START/MAX are greyed out, the controller is still running v1.15 (or Android cached the old Bluetooth service list: toggle Bluetooth or unpair, then reconnect).
2. **HOME**: select HOME, press Enter. Nudge until the native knob is at its fully counter-clockwise OFF position and the track reads zero volts (use your meter). Back off if you hear the servo pushing against a stop. Save HOME. Do not go below 1000 us unless you are sure the knob has more room.
3. **START**: select START, press Enter. The servo moves from HOME toward START (about 1876 us). Nudge up/down until the engine just begins to move with no cars (about 4 VAC). Back off a hair so it just does not move, or stay where it just creeps, as you prefer. Save START.
4. **MAX**: select MAX, press Enter. The servo goes to the saved MAX (2200 us, about 10.6 VAC on your first reading). Nudge **up** in small steps (+10, then +1), reading the track each time, until it shows about **15 VAC**. Save MAX.
   - Stop BEFORE the servo binds, strains or stalls. If it hums or the linkage flexes, back off.
   - **Never exceed the native knob's hard stop** (about 270 degrees, 19.8 VAC max on your meter). The firmware cannot see the knob stop; its only limit is 2500 us.
   - My linear estimate put 15 VAC near 2400 to 2415 us. That is an unverified extrapolation; trust your meter, not that number.
5. Test from the UI: 0% should be zero volts (parked); 1% should just start the engine; 100% should be about 15 VAC; the middle of the slider should be smooth. Cancel at any time discards the unsaved point.
6. If you leave on Local mode after Save/Cancel, bring the add-on pot to 0% before it takes control.

## Compile
arduino-cli 1.6.0, core arduino:esp32 2.0.18-arduino.5, FQBN arduino:esp32:nano_nora: **0 errors**. 1 warning (`-Wtype-limits` from `constrain()` on a uint8_t in setThrottleTargetPercent; v1.15 had 2 of the same kind, one was in code that was rewritten). Size 940,953 bytes (29%); RAM 56,588 bytes (17%). No extra libraries.

Checks done off-hardware: the percent/pulse formulas were checked for exact round trip (every percent 0..100 maps to a distinct pulse and back to itself) over every legal START/MAX combination. The UI was run in headless Chrome against a mocked Bluetooth device (a simulation I wrote, not the real firmware) with v1.16-style, v1.15-style and v1.13-style status packets: selector enable/disable, command bytes (`01`, `06 02`, `05 32`, `03`), and the save toast. The firmware's calibration state machine itself has not been run anywhere.

## Open questions / not verified
1. Everything about the real behavior on the train is untested: NVS save/load of the new keys, BLE command handling, pickup feel, the faster dead-zone slew.
2. The ~15 VAC at ~2400 us figure is an extrapolation. The pot may not be linear that far.
3. Servo still holds position at 0% after the first throttle move (same as v1.15). It may hum against the linkage at HOME. Not addressed in this release.
4. 900 us HOME floor is still below 1000 us (kept as specified); consider 1000.
5. The status packet is longer; it relies on Bluetooth MTU negotiation.
6. "Reset" resets only the selected point, and clamps it to the order rules against the saved values of the other points.
7. Selecting another point throws away an unsaved edit of the current one (by design, so Save only ever saves what you can see).
8. Not compiled with Arduino-ESP32 3.x.
