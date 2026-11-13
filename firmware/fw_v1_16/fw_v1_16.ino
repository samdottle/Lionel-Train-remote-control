/*
  Lionel O27 Controller - Arduino Nano ESP32
  Firmware: FW v1.16 SG DRAFT
  Matched UI: UI v1.7 SG DRAFT (UI v1.6 still works for HOME-only calibration)
  Date: 2026-11-13   (Nov 13 2026)
  Based on: FW v1.15 (2026-11-12). v1.13 / v1.14 / v1.15 files are untouched.
  Status: DRAFT - UNTESTED on hardware. Not approved, not final, not released.
  Provenance: SG

  FW v1.16 CHANGES vs v1.15
  -------------------------
  Reason: bench measurements showed the engine does not start moving until
  about 4 VAC on the track, which is ~73% of the v1.15 slider (~1876 us), so
  the lower ~3/4 of the slider did nothing. v1.16 adds a calibrated START
  point and a calibrated MAX point so the whole 1..100% slider is usable.

  1. THREE saved calibration points, all in NVS namespace "o27":
        HOME  key "homeUs"   default 1000 us  0% / park / zero track volts
        START key "startUs"  default 1876 us  1% = train just begins to move
                                              (~4 VAC, from bench data)
        MAX   key "maxUs"    default 2200 us  100% (v1.15's fixed value; kept
                                              as the safe default. Calibrate
                                              upward, e.g. toward ~15 VAC.)
     Limits (clamped when loaded from NVS, when nudged, and when saved):
        HOME  : 900 .. 2000 us
        START : HOME + 20  .. MAX - 100 us
        MAX   : START + 100 .. 2500 us (SERVO_MAX_LIMIT_US, hard cap)
     i.e. always HOME < START < MAX with minimum gaps of 20 us and 100 us.
     v1.15 saved "homeUs" values are still honoured (then clamped as above).
     On load the three values are checked in order HOME, START, MAX; a stored
     value that violates the ordering is moved to the nearest legal value
     (the later point moves, not the earlier one) and a Serial line says so.
  2. Throttle mapping (applies everywhere percent <-> pulse is converted:
     throttlePercentToUs, servoUsToThrottlePercent / status reporting, local
     pot pickup, slew target):
        0%      = HOME exactly (park; zero track volts)
        1..100% = linear START..MAX:  us = START + round((p-1)*(MAX-START)/99)
     Inverse (status "throttle"): a pulse below START reports 0%; otherwise
     1 + round((us-START)*99/(MAX-START)). The two are exact inverses for
     every percent because MAX-START >= 100.
     The Local pot / BLE slider still work in whole percent 0..100, so the
     soft-takeover (pickup) logic is unchanged and pickup at 0% still works.
  3. Slew: START..MAX still moves at SERVO_SLEW_STEP_US = 6 us / 20 ms
     (unchanged). NEW design judgment: pulses BELOW the effective START (the
     "dead zone" HOME..START, ~876 us by default, where the engine does not
     move anyway) are crossed at SERVO_GAP_SLEW_STEP_US = 18 us / 20 ms, so
     a 0% -> 1% change responds in ~1 s instead of ~3 s. Set
     SERVO_GAP_SLEW_STEP_US to 6 to get exactly the v1.15 slew everywhere.
  4. Calibration characteristic (0027aaa5-...) is EXTENDED, same UUID, and
     backward compatible: 01 Enter, 02 Cancel, 03 Save, 04 Reset, 05 Nudge keep
     working and act on HOME unless another target was selected:
        06 <uint8 t>  SELECT TARGET (0=HOME, 1=START, 2=MAX)
     See "CALIBRATION COMMANDS" below.
  5. Status JSON keeps "throttle","mode","cal","home" and adds
        "start" : START in us   (working value while it is being calibrated)
        "max"   : MAX in us     (working value while it is being calibrated)
        "calTarget" : 0/1/2, the selected target (0 when not calibrating)
     (no "fw" field: v1.15 has none and the longer packet is a BLE-MTU risk;
     UI v1.7 detects v1.16 by the presence of "start"/"max".)
  6. Banner / Serial prints now say FW v1.16 / UI v1.7.
  7. Absolute servo clamp in writeServoPulseUs() is now 900..2500 us
     (SERVO_MAX_LIMIT_US). Normal throttle never exceeds the saved MAX.
  Unchanged from v1.15: startup HOME hold + PWM release, local pot 16-sample
  average and 1% deadband, 3% pickup tolerance, relays, BLE UUIDs, remote
  throttle protocol, 20 ms servo update interval.

  CALIBRATION COMMANDS (write to CAL_CHAR_UUID 0027aaa5-0001-4000-8000-00805f9b0027)
  ---------------------------------------------------------------------
  Byte 0 = command, optional byte 1 = argument. One write = one command.
     0x01            ENTER   enter calibration; target = HOME (always, for
                             v1.15 client compatibility); servo drives to the
                             saved HOME
     0x02            CANCEL  leave calibration, discard working value; servo
                             returns to 0% = saved HOME
     0x03            SAVE    validate + write the SELECTED target to NVS only,
                             then leave calibration (0% / HOME)
     0x04            RESET   selected target's working value = its default
                             (HOME 1000 / START 1876 / MAX 2200, clamped to
                             the ordering rules); not saved until SAVE
     0x05 <int8 d>   NUDGE   selected target's working value += d us (-50..+50),
                             clamped to that target's legal range; + = more
                             pulse = more throttle
     0x06 <uint8 t>  SELECT  select target 0=HOME 1=START 2=MAX; the servo
                             drives to that target's value. Any unsaved edit
                             of the previously selected target is discarded
                             (reverts to its saved value). Values >2 ignored.
  All except ENTER are ignored (with a Serial message) when not in calibration.
  BLE disconnect while calibrating = CANCEL. Commands are queued in the BLE
  callback and executed in loop(), never inside the BLE task.
  While calibrating, normal throttle (slider and Local pot) is ignored.
  Nudging limits use the SAVED values of the other two points, so save HOME
  before START before MAX if you need to move them far.

  ----- Below: FW v1.15 header, kept for history -----

  Lionel O27 Controller - Arduino Nano ESP32
  Firmware: FW v1.15 SG DRAFT
  Matched UI: UI v1.6 SG DRAFT (UI v1.5 still works; it just has no
              calibration screen)
  Date: 2026-11-12   (Nov 12 2026)
  Based on: FW v1.13 (2026-09-03). v1.13 / v1.14 files are untouched.
  Status: DRAFT - not bench-tested by the author. Not approved, not final.

  FW v1.15 CHANGES vs v1.13
  -------------------------
  1. Servo HOME (0% throttle pulse) is no longer the fixed constant 1000 us.
     It is a run-time value, loaded at boot from non-volatile storage (ESP32
     Preferences / NVS, namespace "o27", key "homeUs"). Default is 1000 us if
     nothing has been saved. The loaded/saved value is clamped to
     SERVO_HOME_MIN_US..SERVO_HOME_MAX_US (900..2000 us), and is always kept at
     least SERVO_HOME_MIN_SPAN_US (200 us) below SERVO_MAX_US.
     Throttle 0..100% now maps to saved HOME..2200 us. The 100% pulse
     (SERVO_MAX_US = 2200 us) is unchanged.
  2. New BLE characteristic CAL_CHAR_UUID (0027aaa5-...) for calibration
     commands. WRITE only. See "CALIBRATION COMMANDS" below.
  3. The status characteristic JSON gains two fields:
        "cal"  : 1 while calibration mode is active, else 0
        "home" : the working HOME in us while calibrating (what the servo is
                 being driven to), otherwise the saved HOME in us.
     Existing fields ("throttle", "mode") are unchanged, so UI v1.5 still
     parses the status packet.
  4. While calibration mode is active the normal throttle (BLE slider and
     Local pot) is ignored and the servo is driven to the working HOME value
     with the same 6 us / 20 ms slew as normal motion. Leaving calibration
     (Save, Cancel, BLE disconnect) returns to 0% throttle: remote target is
     set to 0%, and if the toggle is in Local the pot must be brought back to
     0% (same pickup rule as power-up) before it takes control.
  5. Local pot noise: LOCAL_POT_AVERAGE_SAMPLES raised from 8 to 16, and a 1%
     deadband (LOCAL_POT_DEADBAND_PERMILLE = 10) is applied in
     readLocalPotPercent(): the filtered value only moves when the pot has
     moved >= 1% (of full scale) from the last accepted value, or when it
     reaches either end of travel. This is intended to stop the servo from
     dithering on ADC noise. Percent is still floor(pot * 100 / full scale),
     as in v1.13.
  6. Calibration events are printed on Serial (115200).
  7. Servo pulse clamp in writeServoPulseUs() now uses the absolute range
     SERVO_HOME_MIN_US..SERVO_MAX_US (needed so calibration can go below or
     above the saved HOME). Normal throttle targets are never below the saved
     HOME, so normal behavior is unchanged.
  Unchanged: all four v1.13 BLE UUIDs and their protocol, relay logic
  (D6 Reverse, D7 Whistle, 150 ms pulse, 5 s whistle cutoff), Local/Remote
  pickup logic (3% tolerance), startup HOME hold + PWM release, slew rate.

  CALIBRATION COMMANDS (write to CAL_CHAR_UUID 0027aaa5-0001-4000-8000-00805f9b0027)
  ---------------------------------------------------------------------
  Byte 0 = command, optional byte 1 = argument. One write = one command.
     0x01            ENTER   enter calibration; working value = saved HOME
     0x02            CANCEL  leave calibration, discard working value
     0x03            SAVE    write working value to NVS and leave calibration
     0x04            RESET   working value = 1000 us default (NOT saved until
                             SAVE; CANCEL still discards it)
     0x05 <int8 d>   NUDGE   working value += d us (d signed, -50..+50 us;
                             UI uses -10, -1, +1, +10)
  All except ENTER are ignored (with a Serial message) when not in calibration.
  Commands are queued in the BLE callback and executed in loop(), never inside
  the BLE task.

  ----- Below: original FW v1.13 header, kept for history -----

  Firmware: FW v1.13
  Matched UI: UI v1.5
  Date: 2026-09-03

  FW v1.13 STARTUP HOME RELEASE
  ------------------------------
  On power-up the MG90S is commanded to 1000 us HOME/OFF. After a short
  settling interval, the LEDC PWM signal is detached so the servo is not
  left holding against HOME. The first later throttle movement
  automatically re-attaches PWM and resumes normal servo control.
  UI v1.5 uses the same BLE protocol.

  FW v1.12 CALIBRATION CHANGE
  ---------------------------
  SERVO_MAX_US increased from 1800 us to 2200 us for greater
  counterclockwise throttle travel. SERVO_HOME_US remains 1000 us.
  UI remains v1.4; BLE, relay, and Local/Remote behavior are unchanged.

  MAJOR CHANGE IN FW v1.11
  ------------------------
  The 28BYJ-48 stepper + ULN2003 throttle actuator and the old mechanical
  stall-homing routine are removed.

  Throttle is now driven by an MG90S positional servo:
    D2 -> MG90S signal
    1000 us = throttle OFF / HOME (full clockwise viewed from above)
    2200 us = 100% commanded throttle

  Mechanical setup:
    On the first power-up with the coupling loose/disconnected, the firmware
    immediately commands 1000 us. With the servo at that clockwise endpoint,
    put the AC1803000 native throttle pot at OFF and attach the coupling.
    Thereafter, startup simply commands the known servo HOME position.
    There is no homing search, stop grinding, current sensing, or limit switch.

  LOCAL/REMOTE HANDOFF
  --------------------
  Remote -> Local uses soft takeover ("pickup"):
    - Servo freezes at its present throttle.
    - Local pot is ignored until it reaches/crosses the held throttle.
    - Then Local takes over smoothly.

  Local -> Remote is also bumpless:
    - Servo freezes at its present throttle.
    - The Remote target is initialized to that same throttle.
    - The next UI throttle release may then command a new speed.

  If the controller powers up in Local with the local pot away from OFF,
  the servo remains at HOME until the local pot is brought to/crosses 0%.

  RELAYS
  ------
  D6 Reverse  -> ACTIVE HIGH 2N2222 low-side relay driver
  D7 Whistle  -> ACTIVE HIGH 2N2222 low-side relay driver
  LOW = relay released; HIGH = relay energized

  BLE
  ---
  BLE protocol and UUIDs remain compatible with UI v1.5.
  Binary-safe 0% throttle handling is retained.
  Whistle remains press-and-hold with a 5 second safety cutoff.
  Reverse remains a momentary pulse.

  PWM IMPLEMENTATION
  ------------------
  MG90S PWM is generated with the ESP32-S3 LEDC hardware peripheral rather
  than ESP32Servo. This avoids the no-output behavior observed with the
  ESP32Servo library on this Nano ESP32 setup.

  No external servo library is required.
*/

#include <Arduino.h>
#include "esp_arduino_version.h"
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>
#include <Preferences.h>

// ============================================================================
// Pin assignments — Arduino Nano ESP32 silkscreen names
// ============================================================================

#define SERVO_PIN D2

#define RELAY_REVERSE_PIN D6
#define RELAY_WHISTLE_PIN D7

#define LOCAL_REMOTE_TOGGLE_PIN D8
#define LOCAL_POT_PIN A0

// LOW on D8 means Local with the present panel wiring.
#define TOGGLE_LOW_MEANS_LOCAL true

// ============================================================================
// MG90S throttle configuration
// ============================================================================

// Confirmed on the installed orientation:
// 1000 us drives the MG90S fully clockwise when viewed from above.
// FW v1.15: this is now only the DEFAULT home. The working value is the
// run-time variable homeUs, loaded from NVS at boot.
#define SERVO_HOME_DEFAULT_US 1000

// Allowed range for a saved/calibrated HOME.
#define SERVO_HOME_MIN_US 900
#define SERVO_HOME_MAX_US 2000

// FW v1.16: START = 1% pulse (engine just begins to move, ~4 VAC on the
// bench). MAX = 100% pulse. Both are saved in NVS and calibrated over BLE.
// Defaults are the safe v1.15 values / bench measurement.
#define SERVO_START_DEFAULT_US 1876
#define SERVO_MAX_DEFAULT_US 2200   // v1.15's fixed 100% value, kept as default

// Absolute hard cap for any servo pulse (MG90S practical limit ~2500 us).
#define SERVO_MAX_LIMIT_US 2500

// Minimum gaps enforced between the saved points: HOME < START < MAX.
#define SERVO_START_MIN_GAP_US 20    // START >= HOME + 20
#define SERVO_MAX_MIN_GAP_US 100     // MAX   >= START + 100 (must stay >= 99
                                     // so every percent maps to a distinct us)

// Standard analog-servo frame.
#define SERVO_PWM_FREQ_HZ 50
#define SERVO_PWM_FRAME_US 20000UL

// ESP32-S3 LEDC supports up to 14-bit resolution.
#define SERVO_PWM_RES_BITS 14

// Slew the commanded pulse instead of instantly jumping to a new throttle.
// 6 us every 20 ms = 300 us/sec; 1000 -> 2200 us takes about 4.0 seconds,
// close to the deliberate smooth behavior of the previous stepper system.
#define SERVO_UPDATE_INTERVAL_MS 20
#define SERVO_SLEW_STEP_US 6
// FW v1.16: faster slew while below START (dead zone where the engine does not
// move). Set equal to SERVO_SLEW_STEP_US for exactly the v1.15 behavior.
#define SERVO_GAP_SLEW_STEP_US 18

// Hold the initial HOME command only long enough for the MG90S to reach
// the mechanically indexed OFF position, then remove PWM/holding torque.
#define STARTUP_HOME_HOLD_MS 1200

// Used only on Arduino-ESP32 2.x. Arduino-ESP32 3.x attaches LEDC by pin.
#if ESP_ARDUINO_VERSION_MAJOR < 3
#define SERVO_LEDC_CHANNEL 0
#endif

// ============================================================================
// Local control / soft-takeover configuration
// ============================================================================

// Pot must come within this many percent of the held throttle, OR cross the
// held throttle, before Local takes control.
#define LOCAL_PICKUP_TOLERANCE_PERCENT 3

// Debounce the physical Local/Remote toggle.
#define MODE_SWITCH_DEBOUNCE_MS 35

// Set true only if the local throttle pot electrically reads backwards.
#define LOCAL_POT_REVERSED false

// Average several ADC readings to reduce noise. (v1.13: 8, v1.15: 16)
#define LOCAL_POT_AVERAGE_SAMPLES 16

// FW v1.15: ignore local pot changes smaller than this (parts per thousand of
// full scale; 10 = 1%) unless the pot reaches either end of travel.
#define LOCAL_POT_DEADBAND_PERMILLE 10
#define LOCAL_POT_END_PERMILLE 2

// ============================================================================
// Relay configuration
// ============================================================================

#define RELAY_ACTIVE_LEVEL HIGH
#define RELAY_INACTIVE_LEVEL LOW

// Reverse is a momentary one-shot.
#define RELAY_PULSE_MS 150

// Whistle is held while the UI button is held. This is a fail-safe if the
// release packet is lost.
#define HORN_MAX_HOLD_MS 5000

// ============================================================================
// BLE identifiers — unchanged for UI v1.5 compatibility
// ============================================================================

#define BLE_DEVICE_NAME "O27 Controller"

#define SERVICE_UUID        "0027aaa0-0001-4000-8000-00805f9b0027"
#define THROTTLE_CHAR_UUID  "0027aaa1-0001-4000-8000-00805f9b0027" // write uint8 0..100
#define HORN_CHAR_UUID      "0027aaa2-0001-4000-8000-00805f9b0027" // write 0=release, nonzero=hold
#define DIRECTION_CHAR_UUID "0027aaa3-0001-4000-8000-00805f9b0027" // write = Reverse request
#define STATUS_CHAR_UUID    "0027aaa4-0001-4000-8000-00805f9b0027" // read + notify JSON
// FW v1.15: calibration command characteristic (write 1-2 bytes, see header)
#define CAL_CHAR_UUID       "0027aaa5-0001-4000-8000-00805f9b0027"

#define CAL_CMD_ENTER  0x01
#define CAL_CMD_CANCEL 0x02
#define CAL_CMD_SAVE   0x03
#define CAL_CMD_RESET  0x04
#define CAL_CMD_NUDGE  0x05
#define CAL_CMD_SELECT 0x06   // FW v1.16: arg = target (0 HOME, 1 START, 2 MAX)

#define CAL_TARGET_HOME  0
#define CAL_TARGET_START 1
#define CAL_TARGET_MAX   2
#define CAL_NUDGE_MAX_US 50

BLEServer *pServer = nullptr;
BLECharacteristic *pThrottleChar = nullptr;
BLECharacteristic *pHornChar = nullptr;
BLECharacteristic *pDirectionChar = nullptr;
BLECharacteristic *pStatusChar = nullptr;
BLECharacteristic *pCalChar = nullptr;

volatile bool bleClientConnected = false;
volatile uint8_t remoteThrottlePercent = 0;
volatile bool directionRequested = false;
volatile bool hornHeld = false;

// ============================================================================
// Servo state
// ============================================================================

bool servoPwmAttached = false;
bool startupHomeReleasePending = false;
unsigned long startupHomeCommandedAtMs = 0;

// "Current" means the last commanded pulse width. When PWM is detached at
// startup HOME, this remains 1000 us so the logical throttle state stays 0%.
// HOME (0% throttle) pulse in us. Loaded from NVS in setup(); default 1000.
uint16_t homeUs = SERVO_HOME_DEFAULT_US;
// FW v1.16: START (1%) and MAX (100%) pulses, loaded from NVS in setup().
uint16_t startUs = SERVO_START_DEFAULT_US;
uint16_t maxUs = SERVO_MAX_DEFAULT_US;

// Calibration mode state (FW v1.15, extended in v1.16)
bool calActive = false;
uint8_t calTarget = CAL_TARGET_HOME;   // selected target while calibrating
// Working value of the selected target. The other two points always equal
// their saved values (selecting another target discards an unsaved edit).
uint16_t calWorkingUs = SERVO_HOME_DEFAULT_US;
Preferences prefs;

// Calibration command queue: BLE task produces, loop() consumes.
#define CAL_QUEUE_SIZE 16
struct CalCmd { uint8_t cmd; int8_t arg; };
volatile CalCmd calQueue[CAL_QUEUE_SIZE];
volatile uint8_t calQueueHead = 0;   // written by BLE task
volatile uint8_t calQueueTail = 0;   // written by loop()
volatile bool calCancelOnDisconnect = false;
bool statusNotifyForced = false;

uint16_t currentServoUs = SERVO_HOME_DEFAULT_US;
uint8_t throttleTargetPercent = 0;
unsigned long lastServoUpdateMs = 0;

// ============================================================================
// Local/Remote mode state
// ============================================================================

bool stableLocalMode = false;
bool lastRawLocalMode = false;
unsigned long modeRawChangedAtMs = 0;

bool localPickupPending = false;
uint8_t localPickupTargetPercent = 0;
uint8_t previousLocalPotPercent = 0;

// ============================================================================
// Relay state
// ============================================================================

bool reverseRelayActive = false;
unsigned long reverseRelayOffAtMs = 0;

bool hornHeldPrev = false;
bool hornSafetyCutoffLatched = false;
unsigned long hornHeldStartMs = 0;

// ============================================================================
// Status timing
// ============================================================================

unsigned long lastStatusNotifyMs = 0;
const unsigned long STATUS_NOTIFY_INTERVAL_MS = 250;

// ============================================================================
// Servo / throttle helpers
// ============================================================================

// FW v1.16: effective calibration points. While calibrating, the selected
// target uses its working value; the others are their saved values.
uint16_t effHomeUs() {
  return (calActive && calTarget == CAL_TARGET_HOME) ? calWorkingUs : homeUs;
}
uint16_t effStartUs() {
  return (calActive && calTarget == CAL_TARGET_START) ? calWorkingUs : startUs;
}
uint16_t effMaxUs() {
  return (calActive && calTarget == CAL_TARGET_MAX) ? calWorkingUs : maxUs;
}

// 0% = HOME (park). 1..100% = linear START..MAX (saved values, used for all
// normal throttle control; calibration drives the servo directly instead).
uint16_t throttlePercentToUs(uint8_t percent) {
  if (percent > 100) percent = 100;
  if (percent == 0) {
    return homeUs;
  }
  long span = (long)maxUs - (long)startUs;
  long pulse = (long)startUs + (((long)(percent - 1) * span + 49) / 99);
  if (pulse < (long)startUs) pulse = startUs;
  if (pulse > (long)maxUs) pulse = maxUs;
  return (uint16_t)pulse;
}

// Inverse of throttlePercentToUs(): a pulse below START is 0%.
uint8_t servoUsToThrottlePercent(uint16_t pulseUs) {
  if (pulseUs < startUs) {
    return 0;
  }
  if (pulseUs > maxUs) pulseUs = maxUs;
  long span = (long)maxUs - (long)startUs;
  long percent = 1 + ((((long)pulseUs - (long)startUs) * 99 + (span / 2)) / span);
  if (percent < 1) percent = 1;
  if (percent > 100) percent = 100;
  return (uint8_t)percent;
}

uint8_t currentThrottlePercent() {
  return servoUsToThrottlePercent(currentServoUs);
}

uint32_t servoPulseUsToDuty(uint16_t pulseUs) {
  // LEDC duty uses 2^resolution counts per PWM period.
  const uint32_t dutyLevels = (1UL << SERVO_PWM_RES_BITS);
  uint64_t numerator = (uint64_t)pulseUs * dutyLevels;
  uint32_t duty = (uint32_t)((numerator + (SERVO_PWM_FRAME_US / 2)) /
                             SERVO_PWM_FRAME_US);

  if (duty >= dutyLevels) {
    duty = dutyLevels - 1;
  }
  return duty;
}

bool attachServoPwm() {
  if (servoPwmAttached) {
    return true;
  }

  // Keep the signal low before the LEDC peripheral takes ownership.
  pinMode(SERVO_PIN, OUTPUT);
  digitalWrite(SERVO_PIN, LOW);

#if ESP_ARDUINO_VERSION_MAJOR >= 3
  bool attached = ledcAttach(SERVO_PIN, SERVO_PWM_FREQ_HZ, SERVO_PWM_RES_BITS);
  if (!attached) {
    return false;
  }
#else
  double actualFrequency =
      ledcSetup(SERVO_LEDC_CHANNEL, SERVO_PWM_FREQ_HZ, SERVO_PWM_RES_BITS);
  if (actualFrequency <= 0.0) {
    return false;
  }
  ledcAttachPin(SERVO_PIN, SERVO_LEDC_CHANNEL);
#endif

  servoPwmAttached = true;
  return true;
}

bool detachServoPwm() {
  if (!servoPwmAttached) {
    return true;
  }

#if ESP_ARDUINO_VERSION_MAJOR >= 3
  bool detached = ledcDetach(SERVO_PIN);
  if (!detached) {
    return false;
  }
#else
  ledcDetachPin(SERVO_PIN);
#endif

  // Leave the signal wire at a defined LOW level after PWM is removed.
  pinMode(SERVO_PIN, OUTPUT);
  digitalWrite(SERVO_PIN, LOW);
  servoPwmAttached = false;
  return true;
}

bool writeServoPulseUs(uint16_t pulseUs) {
  if (!servoPwmAttached) {
    return false;
  }

  // v1.16: absolute clamp 900..2500 us (calibration may command outside the
  // saved HOME..MAX range, but never beyond SERVO_MAX_LIMIT_US).
  if (pulseUs < SERVO_HOME_MIN_US) pulseUs = SERVO_HOME_MIN_US;
  if (pulseUs > SERVO_MAX_LIMIT_US) pulseUs = SERVO_MAX_LIMIT_US;
  uint32_t duty = servoPulseUsToDuty(pulseUs);

#if ESP_ARDUINO_VERSION_MAJOR >= 3
  bool ok = ledcWrite(SERVO_PIN, duty);
#else
  ledcWrite(SERVO_LEDC_CHANNEL, duty);
  bool ok = true;
#endif

  if (ok) {
    currentServoUs = pulseUs;
  }
  return ok;
}

bool setupServoPwm() {
  if (!attachServoPwm()) {
    return false;
  }

  // The first servo operation after power-up is always HOME/OFF.
  currentServoUs = homeUs;
  throttleTargetPercent = 0;

  if (!writeServoPulseUs(homeUs)) {
    return false;
  }

  startupHomeCommandedAtMs = millis();
  startupHomeReleasePending = true;
  return true;
}

void setThrottleTargetPercent(uint8_t percent) {
  throttleTargetPercent = (uint8_t)constrain(percent, 0, 100);
}

void serviceStartupHomeRelease() {
  if (!startupHomeReleasePending) {
    return;
  }

  // If a real throttle command arrives before the settling interval expires,
  // keep PWM attached and let normal motion take over instead of detaching it.
  if (throttleTargetPercent != 0 || currentServoUs != homeUs) {
    startupHomeReleasePending = false;
    return;
  }

  if (millis() - startupHomeCommandedAtMs < STARTUP_HOME_HOLD_MS) {
    return;
  }

  if (detachServoPwm()) {
    Serial.println("[SERVO] Startup HOME reached; PWM detached / holding torque released");
  } else {
    Serial.println("[SERVO] WARNING: could not detach PWM after startup HOME");
  }

  startupHomeReleasePending = false;
}

void serviceServoMotion() {
  unsigned long now = millis();
  if (now - lastServoUpdateMs < SERVO_UPDATE_INTERVAL_MS) {
    return;
  }
  lastServoUpdateMs = now;

  // v1.15/v1.16: in calibration mode the servo is driven to the working value
  // of the selected target instead of the throttle-derived value.
  uint16_t targetUs = calActive ? calWorkingUs
                                : throttlePercentToUs(throttleTargetPercent);

  if (currentServoUs == targetUs) {
    return;
  }

  // Startup HOME may have detached PWM. Re-enable it automatically only
  // when a later throttle command actually requires movement.
  if (!servoPwmAttached) {
    if (!attachServoPwm()) {
      Serial.println("[SERVO] ERROR: could not re-attach LEDC PWM");
      return;
    }

    // Re-establish the last known command before beginning the slew.
    if (!writeServoPulseUs(currentServoUs)) {
      Serial.println("[SERVO] ERROR: could not restore servo PWM command");
      return;
    }

    Serial.println("[SERVO] PWM re-attached for throttle movement");
  }

  uint16_t nextUs = currentServoUs;

  // v1.16: faster step while still below START (engine dead zone), normal
  // step once at/above START. Based on the current position so a move that
  // ends inside the dead zone (e.g. to HOME) is also fast.
  uint16_t stepUs = (currentServoUs < effStartUs()) ? SERVO_GAP_SLEW_STEP_US
                                                    : SERVO_SLEW_STEP_US;

  if (currentServoUs < targetUs) {
    uint16_t remaining = targetUs - currentServoUs;
    nextUs = currentServoUs + min(stepUs, remaining);
  } else {
    uint16_t remaining = currentServoUs - targetUs;
    nextUs = currentServoUs - min(stepUs, remaining);
  }

  if (!writeServoPulseUs(nextUs)) {
    Serial.println("[SERVO] ERROR: LEDC duty write failed");
  }
}

// ============================================================================
// Local pot / mode helpers
// ============================================================================

bool readRawLocalMode() {
  bool pinIsLow = (digitalRead(LOCAL_REMOTE_TOGGLE_PIN) == LOW);
  return TOGGLE_LOW_MEANS_LOCAL ? pinIsLow : !pinIsLow;
}

// FW v1.15: filtered pot value in parts-per-thousand (0..1000) with a
// deadband. potFilterValid is cleared by resetLocalPotFilter() so the filter
// re-seeds from the next reading (done when Local pickup begins).
int16_t potAcceptedPermille = 0;
bool potFilterValid = false;

void resetLocalPotFilter() {
  potFilterValid = false;
}

uint8_t readLocalPotPercent() {
  uint32_t sum = 0;
  for (uint8_t i = 0; i < LOCAL_POT_AVERAGE_SAMPLES; ++i) {
    sum += analogRead(LOCAL_POT_PIN);
  }

  uint16_t raw = (uint16_t)(sum / LOCAL_POT_AVERAGE_SAMPLES);
  int16_t permille = (int16_t)(((uint32_t)raw * 1000UL) / 4095UL);
  if (permille < 0) permille = 0;
  if (permille > 1000) permille = 1000;

  if (LOCAL_POT_REVERSED) {
    permille = 1000 - permille;
  }

  if (!potFilterValid) {
    potAcceptedPermille = permille;
    potFilterValid = true;
  } else if (permille != potAcceptedPermille) {
    int16_t diff = permille - potAcceptedPermille;
    if (diff < 0) diff = -diff;

    bool atEnd = (permille <= LOCAL_POT_END_PERMILLE) ||
                 (permille >= (1000 - LOCAL_POT_END_PERMILLE));

    if (diff >= LOCAL_POT_DEADBAND_PERMILLE || atEnd) {
      potAcceptedPermille = permille;
    }
  }

  // Floor to whole percent, same as v1.13's map(raw, 0, 4095, 0, 100).
  return (uint8_t)(potAcceptedPermille / 10);
}

void beginLocalPickup(uint8_t heldPercent) {
  localPickupTargetPercent = heldPercent;
  resetLocalPotFilter();   // v1.15: re-seed the deadband filter
  previousLocalPotPercent = readLocalPotPercent();
  localPickupPending = true;

  Serial.print("[MODE] Local pickup waiting at ");
  Serial.print(localPickupTargetPercent);
  Serial.print("%; local pot currently ");
  Serial.print(previousLocalPotPercent);
  Serial.println("%");
}

bool localPotReachedOrCrossedTarget(uint8_t currentLocalPercent) {
  int target = localPickupTargetPercent;
  int previous = previousLocalPotPercent;
  int current = currentLocalPercent;

  bool withinTolerance =
      abs(current - target) <= LOCAL_PICKUP_TOLERANCE_PERCENT;

  bool crossed =
      ((previous < target) && (current >= target)) ||
      ((previous > target) && (current <= target));

  return withinTolerance || crossed;
}

void handleModeTransition(bool newLocalMode) {
  // Freeze the servo exactly where the currently issued PWM command is.
  uint8_t heldPercent = currentThrottlePercent();
  setThrottleTargetPercent(heldPercent);

  if (newLocalMode) {
    Serial.print("[MODE] Remote -> Local; holding ");
    Serial.print(heldPercent);
    Serial.println("% until local pot pickup");
    beginLocalPickup(heldPercent);
  } else {
    // Prevent a jump to an old/stale remote slider value.
    remoteThrottlePercent = heldPercent;
    localPickupPending = false;

    Serial.print("[MODE] Local -> Remote; holding ");
    Serial.print(heldPercent);
    Serial.println("% until next remote throttle command");
  }
}

void serviceModeSwitch() {
  bool rawLocal = readRawLocalMode();
  unsigned long now = millis();

  if (rawLocal != lastRawLocalMode) {
    lastRawLocalMode = rawLocal;
    modeRawChangedAtMs = now;
  }

  if ((rawLocal != stableLocalMode) &&
      (now - modeRawChangedAtMs >= MODE_SWITCH_DEBOUNCE_MS)) {
    stableLocalMode = rawLocal;
    handleModeTransition(stableLocalMode);
  }
}

void serviceThrottleSource() {
  // v1.15: normal throttle (BLE slider and Local pot) is ignored while
  // calibrating.
  if (calActive) {
    return;
  }

  if (!stableLocalMode) {
    setThrottleTargetPercent(remoteThrottlePercent);
    return;
  }

  uint8_t localPercent = readLocalPotPercent();

  if (localPickupPending) {
    if (localPotReachedOrCrossedTarget(localPercent)) {
      localPickupPending = false;
      setThrottleTargetPercent(localPercent);

      Serial.print("[MODE] Local pickup acquired at ");
      Serial.print(localPercent);
      Serial.println("%");
    }

    previousLocalPotPercent = localPercent;
    return;
  }

  setThrottleTargetPercent(localPercent);
}

// ============================================================================
// HOME / START / MAX calibration (FW v1.15, extended in v1.16)
// ============================================================================

// Legal range for each target, given the SAVED values of the other two.
long targetMinUs(uint8_t target) {
  switch (target) {
    case CAL_TARGET_START: return (long)homeUs + SERVO_START_MIN_GAP_US;
    case CAL_TARGET_MAX:   return (long)startUs + SERVO_MAX_MIN_GAP_US;
    default:               return SERVO_HOME_MIN_US;
  }
}

long targetMaxUs(uint8_t target) {
  switch (target) {
    case CAL_TARGET_START: return (long)maxUs - SERVO_MAX_MIN_GAP_US;
    case CAL_TARGET_MAX:   return SERVO_MAX_LIMIT_US;
    default: {
      long m = (long)startUs - SERVO_START_MIN_GAP_US;
      return (m < SERVO_HOME_MAX_US) ? m : SERVO_HOME_MAX_US;
    }
  }
}

uint16_t clampTargetUs(uint8_t target, long us) {
  long lo = targetMinUs(target);
  long hi = targetMaxUs(target);
  if (hi < lo) hi = lo;   // cannot happen with the saved ordering invariant
  if (us < lo) us = lo;
  if (us > hi) us = hi;
  return (uint16_t)us;
}

uint16_t targetDefaultUs(uint8_t target) {
  switch (target) {
    case CAL_TARGET_START: return SERVO_START_DEFAULT_US;
    case CAL_TARGET_MAX:   return SERVO_MAX_DEFAULT_US;
    default:               return SERVO_HOME_DEFAULT_US;
  }
}

uint16_t targetSavedUs(uint8_t target) {
  switch (target) {
    case CAL_TARGET_START: return startUs;
    case CAL_TARGET_MAX:   return maxUs;
    default:               return homeUs;
  }
}

const char *targetName(uint8_t target) {
  switch (target) {
    case CAL_TARGET_START: return "START";
    case CAL_TARGET_MAX:   return "MAX";
    default:               return "HOME";
  }
}

const char *targetKey(uint8_t target) {
  switch (target) {
    case CAL_TARGET_START: return "startUs";
    case CAL_TARGET_MAX:   return "maxUs";
    default:               return "homeUs";
  }
}

void loadCalibrationFromNvs() {
  homeUs = SERVO_HOME_DEFAULT_US;
  startUs = SERVO_START_DEFAULT_US;
  maxUs = SERVO_MAX_DEFAULT_US;

  // Opened read/write so the namespace is created on first boot (a read-only
  // open of a missing namespace fails and logs an NVS error).
  if (!prefs.begin("o27", false)) {
    Serial.println("[CAL] WARNING: could not open NVS; using default HOME/START/MAX");
    return;
  }

  uint16_t storedHome = prefs.getUShort("homeUs", SERVO_HOME_DEFAULT_US);
  uint16_t storedStart = prefs.getUShort("startUs", SERVO_START_DEFAULT_US);
  uint16_t storedMax = prefs.getUShort("maxUs", SERVO_MAX_DEFAULT_US);
  bool hasHome = prefs.isKey("homeUs");
  bool hasStart = prefs.isKey("startUs");
  bool hasMax = prefs.isKey("maxUs");
  prefs.end();

  // Validate in order HOME, START, MAX; later points move to the nearest
  // legal value (HOME < START < MAX with the minimum gaps).
  long h = storedHome;
  if (h < SERVO_HOME_MIN_US) h = SERVO_HOME_MIN_US;
  if (h > SERVO_HOME_MAX_US) h = SERVO_HOME_MAX_US;
  long s = storedStart;
  if (s < h + SERVO_START_MIN_GAP_US) s = h + SERVO_START_MIN_GAP_US;
  if (s > (long)SERVO_MAX_LIMIT_US - SERVO_MAX_MIN_GAP_US) {
    s = (long)SERVO_MAX_LIMIT_US - SERVO_MAX_MIN_GAP_US;
  }
  long m = storedMax;
  if (m < s + SERVO_MAX_MIN_GAP_US) m = s + SERVO_MAX_MIN_GAP_US;
  if (m > SERVO_MAX_LIMIT_US) m = SERVO_MAX_LIMIT_US;

  homeUs = (uint16_t)h;
  startUs = (uint16_t)s;
  maxUs = (uint16_t)m;

  Serial.print("[CAL] NVS: HOME ");
  Serial.print(hasHome ? "saved " : "default ");
  Serial.print(storedHome);
  Serial.print(" -> ");
  Serial.print(homeUs);
  Serial.print(" | START ");
  Serial.print(hasStart ? "saved " : "default ");
  Serial.print(storedStart);
  Serial.print(" -> ");
  Serial.print(startUs);
  Serial.print(" | MAX ");
  Serial.print(hasMax ? "saved " : "default ");
  Serial.print(storedMax);
  Serial.print(" -> ");
  Serial.print(maxUs);
  Serial.println(" us");
  if ((hasHome && h != storedHome) || (hasStart && s != storedStart) ||
      (hasMax && m != storedMax)) {
    Serial.println("[CAL] WARNING: a stored value was out of range / out of order and was clamped");
  }
}

bool saveTargetToNvs(uint8_t target, uint16_t us) {
  if (!prefs.begin("o27", false)) {
    Serial.println("[CAL] ERROR: could not open NVS for writing");
    return false;
  }
  prefs.putUShort(targetKey(target), us);
  uint16_t back = prefs.getUShort(targetKey(target), 0);
  prefs.end();
  return back == us;
}

// Return to normal throttle control at 0% after leaving calibration.
void leaveCalibrationToZeroThrottle() {
  calActive = false;
  calTarget = CAL_TARGET_HOME;
  remoteThrottlePercent = 0;
  setThrottleTargetPercent(0);

  if (stableLocalMode) {
    // Same protection as power-up: the Local pot must be brought to 0%
    // before it can take control.
    beginLocalPickup(0);
  } else {
    localPickupPending = false;
  }
}

void calEnter() {
  if (calActive) {
    Serial.println("[CAL] Already in calibration mode");
    return;
  }

  startupHomeReleasePending = false;

  // If startup HOME detached PWM, re-attach and re-assert the last command
  // before slewing to the working value.
  if (!servoPwmAttached) {
    if (!attachServoPwm() || !writeServoPulseUs(currentServoUs)) {
      Serial.println("[CAL] ERROR: could not attach servo PWM; calibration not entered");
      return;
    }
  }

  // Always start on HOME (v1.15 clients never send SELECT).
  calTarget = CAL_TARGET_HOME;
  calWorkingUs = homeUs;
  calActive = true;

  Serial.print("[CAL] ENTER; target HOME, working = ");
  Serial.print(calWorkingUs);
  Serial.println(" us (normal throttle ignored)");
}

void calSelect(int target) {
  if (!calActive) {
    Serial.println("[CAL] Select ignored (not in calibration)");
    return;
  }
  if (target < CAL_TARGET_HOME || target > CAL_TARGET_MAX) {
    Serial.print("[CAL] Select ignored; bad target ");
    Serial.println(target);
    return;
  }
  if ((uint8_t)target != calTarget) {
    Serial.print("[CAL] Discarding unsaved ");
    Serial.print(targetName(calTarget));
    Serial.print(" edit (");
    Serial.print(calWorkingUs);
    Serial.print(" us); saved is ");
    Serial.print(targetSavedUs(calTarget));
    Serial.println(" us");
  }
  calTarget = (uint8_t)target;
  calWorkingUs = targetSavedUs(calTarget);
  Serial.print("[CAL] SELECT ");
  Serial.print(targetName(calTarget));
  Serial.print("; servo driving to ");
  Serial.print(calWorkingUs);
  Serial.println(" us");
}

void calNudge(int delta) {
  if (!calActive) {
    Serial.println("[CAL] Nudge ignored (not in calibration)");
    return;
  }
  if (delta > CAL_NUDGE_MAX_US) delta = CAL_NUDGE_MAX_US;
  if (delta < -CAL_NUDGE_MAX_US) delta = -CAL_NUDGE_MAX_US;

  calWorkingUs = clampTargetUs(calTarget, (long)calWorkingUs + delta);

  Serial.print("[CAL] NUDGE ");
  Serial.print(delta);
  Serial.print(" -> working ");
  Serial.print(targetName(calTarget));
  Serial.print(" = ");
  Serial.print(calWorkingUs);
  Serial.println(" us");
}

void calReset() {
  if (!calActive) {
    Serial.println("[CAL] Reset ignored (not in calibration)");
    return;
  }
  calWorkingUs = clampTargetUs(calTarget, targetDefaultUs(calTarget));
  Serial.print("[CAL] RESET working ");
  Serial.print(targetName(calTarget));
  Serial.print(" to default ");
  Serial.print(calWorkingUs);
  Serial.println(" us (not saved until SAVE)");
}

void calCancel(const char *why) {
  if (!calActive) {
    return;
  }
  Serial.print("[CAL] CANCEL (");
  Serial.print(why);
  Serial.print("); discarding ");
  Serial.print(targetName(calTarget));
  Serial.print(" ");
  Serial.print(calWorkingUs);
  Serial.print(" us, saved stays ");
  Serial.print(targetSavedUs(calTarget));
  Serial.println(" us");
  leaveCalibrationToZeroThrottle();
}

void calSave() {
  if (!calActive) {
    Serial.println("[CAL] Save ignored (not in calibration)");
    return;
  }

  // Re-validate ordering against the saved values of the other two points.
  uint16_t value = clampTargetUs(calTarget, calWorkingUs);

  if (!saveTargetToNvs(calTarget, value)) {
    Serial.println("[CAL] ERROR: NVS save failed/verify mismatch; staying in calibration");
    return;
  }

  switch (calTarget) {
    case CAL_TARGET_START: startUs = value; break;
    case CAL_TARGET_MAX:   maxUs = value;   break;
    default:               homeUs = value;  break;
  }
  Serial.print("[CAL] SAVED ");
  Serial.print(targetName(calTarget));
  Serial.print(" = ");
  Serial.print(value);
  Serial.println(" us to NVS");
  leaveCalibrationToZeroThrottle();
}

void processCalCommand(uint8_t cmd, int8_t arg) {
  switch (cmd) {
    case CAL_CMD_ENTER:  calEnter();            break;
    case CAL_CMD_CANCEL: calCancel("user");     break;
    case CAL_CMD_SAVE:   calSave();             break;
    case CAL_CMD_RESET:  calReset();            break;
    case CAL_CMD_NUDGE:  calNudge(arg);         break;
    case CAL_CMD_SELECT: calSelect((int)(uint8_t)arg); break;
    default:
      Serial.print("[CAL] Unknown command 0x");
      Serial.println(cmd, HEX);
      break;
  }
  statusNotifyForced = true;
}

void serviceCalibrationCommands() {
  // BLE disconnect while calibrating: revert to the saved values.
  if (calCancelOnDisconnect) {
    calCancelOnDisconnect = false;
    if (calActive) {
      calCancel("BLE disconnect");
      statusNotifyForced = true;
    }
  }

  while (calQueueTail != calQueueHead) {
    CalCmd c;
    c.cmd = calQueue[calQueueTail].cmd;
    c.arg = calQueue[calQueueTail].arg;
    calQueueTail = (uint8_t)((calQueueTail + 1) % CAL_QUEUE_SIZE);
    processCalCommand(c.cmd, c.arg);
  }
}

// ============================================================================
// Relay helpers
// ============================================================================

void initializeRelayPin(uint8_t pin) {
  // Active-HIGH safety: preload inactive LOW before enabling OUTPUT mode.
  digitalWrite(pin, RELAY_INACTIVE_LEVEL);
  pinMode(pin, OUTPUT);
  digitalWrite(pin, RELAY_INACTIVE_LEVEL);
}

void setRelay(uint8_t pin, bool active) {
  digitalWrite(pin, active ? RELAY_ACTIVE_LEVEL : RELAY_INACTIVE_LEVEL);
}

void startRelayPulse(uint8_t pin, bool &activeFlag, unsigned long &offAtMs) {
  setRelay(pin, true);
  activeFlag = true;
  offAtMs = millis() + RELAY_PULSE_MS;

  Serial.print("[Relay] Pulse start, pin=");
  Serial.println(pin);
}

void serviceRelayPulse(uint8_t pin, bool &activeFlag, unsigned long &offAtMs) {
  if (activeFlag && (long)(millis() - offAtMs) >= 0) {
    setRelay(pin, false);
    activeFlag = false;

    Serial.print("[Relay] Pulse end, pin=");
    Serial.println(pin);
  }
}

// ============================================================================
// BLE callbacks
// ============================================================================

class ServerCallbacks : public BLEServerCallbacks {
  void onConnect(BLEServer *server) override {
    bleClientConnected = true;
    Serial.println("[BLE] Client connected");
  }

  void onDisconnect(BLEServer *server) override {
    bleClientConnected = false;

    // Lost BLE must never leave a level-driven output active.
    hornHeld = false;
    directionRequested = false;

    // FW v1.15/v1.16: abandon an unsaved calibration (handled in loop()).
    calCancelOnDisconnect = true;

    // Remote throttle target is intentionally not changed here. A brief BLE
    // dropout should not unexpectedly change train speed.
    server->getAdvertising()->start();
    Serial.println("[BLE] Client disconnected; advertising restarted");
  }
};

class ThrottleCallbacks : public BLECharacteristicCallbacks {
  void onWrite(BLECharacteristic *characteristic) override {
    // Keep the BLE value binary-safe. 0x00 is a legitimate 0% command.
    auto value = characteristic->getValue();

    if (value.length() >= 1) {
      uint8_t percent = (uint8_t)value[0];
      if (percent > 100) {
        percent = 100;
      }

      remoteThrottlePercent = percent;

      Serial.print("[BLE] Throttle write received: ");
      Serial.print(percent);
      Serial.println("%");
    }
  }
};

class HornCallbacks : public BLECharacteristicCallbacks {
  void onWrite(BLECharacteristic *characteristic) override {
    auto value = characteristic->getValue();
    bool held = (value.length() >= 1) && ((uint8_t)value[0] != 0);

    hornHeld = held;

    Serial.print("[BLE] Horn write received, held=");
    Serial.println(held ? "true" : "false");
  }
};

class DirectionCallbacks : public BLECharacteristicCallbacks {
  void onWrite(BLECharacteristic *characteristic) override {
    directionRequested = true;
    Serial.println("[BLE] Direction write received");
  }
};

class CalCallbacks : public BLECharacteristicCallbacks {
  void onWrite(BLECharacteristic *characteristic) override {
    auto value = characteristic->getValue();
    if (value.length() < 1) {
      return;
    }

    uint8_t cmd = (uint8_t)value[0];
    int8_t arg = (value.length() >= 2) ? (int8_t)value[1] : 0;

    uint8_t next = (uint8_t)((calQueueHead + 1) % CAL_QUEUE_SIZE);
    if (next == calQueueTail) {
      Serial.println("[CAL] Command queue full; command dropped");
      return;
    }
    calQueue[calQueueHead].cmd = cmd;
    calQueue[calQueueHead].arg = arg;
    calQueueHead = next;
  }
};

// ============================================================================
// BLE setup / status
// ============================================================================

void setupBle() {
  BLEDevice::init(BLE_DEVICE_NAME);

  pServer = BLEDevice::createServer();
  pServer->setCallbacks(new ServerCallbacks());

  BLEService *service = pServer->createService(SERVICE_UUID);

  pThrottleChar = service->createCharacteristic(
      THROTTLE_CHAR_UUID, BLECharacteristic::PROPERTY_WRITE);
  pThrottleChar->setCallbacks(new ThrottleCallbacks());

  pHornChar = service->createCharacteristic(
      HORN_CHAR_UUID, BLECharacteristic::PROPERTY_WRITE);
  pHornChar->setCallbacks(new HornCallbacks());

  pDirectionChar = service->createCharacteristic(
      DIRECTION_CHAR_UUID, BLECharacteristic::PROPERTY_WRITE);
  pDirectionChar->setCallbacks(new DirectionCallbacks());

  pCalChar = service->createCharacteristic(
      CAL_CHAR_UUID, BLECharacteristic::PROPERTY_WRITE);
  pCalChar->setCallbacks(new CalCallbacks());

  pStatusChar = service->createCharacteristic(
      STATUS_CHAR_UUID,
      BLECharacteristic::PROPERTY_READ | BLECharacteristic::PROPERTY_NOTIFY);
  pStatusChar->addDescriptor(new BLE2902());

  service->start();

  BLEAdvertising *advertising = BLEDevice::getAdvertising();
  advertising->addServiceUUID(SERVICE_UUID);
  advertising->setScanResponse(true);

  BLEDevice::startAdvertising();
}

void publishStatus() {
  char out[128];

  // v1.15: "cal" and "home". v1.16: "start", "max", "calTarget".
  // home/start/max show the working value of the selected target while
  // calibrating, otherwise the saved values. Longest case is ~92 bytes.
  snprintf(
      out,
      sizeof(out),
      "{\"throttle\":%u,\"mode\":\"%s\",\"cal\":%u,\"home\":%u,"
      "\"start\":%u,\"max\":%u,\"calTarget\":%u}",
      currentThrottlePercent(),
      stableLocalMode ? "local" : "remote",
      calActive ? 1U : 0U,
      (unsigned)effHomeUs(),
      (unsigned)effStartUs(),
      (unsigned)effMaxUs(),
      (unsigned)(calActive ? calTarget : 0));

  pStatusChar->setValue(out);

  if (bleClientConnected) {
    pStatusChar->notify();
  }
}

// ============================================================================
// Setup
// ============================================================================

void setup() {
  Serial.begin(115200);
  delay(800);

  Serial.println();
  Serial.println("==================================================");
  Serial.println(" Lionel O27 Controller - Nano ESP32 FW v1.16 SG DRAFT");
  Serial.println(" UI v1.7 | MG90S throttle | Active-HIGH relays | HOME/START/MAX calibration");
  Serial.println("==================================================");

#ifdef ESP_ARDUINO_VERSION_STR
  Serial.print("ESP32 Arduino core: ");
  Serial.println(ESP_ARDUINO_VERSION_STR);
#endif

  // Relays must be OFF before anything else can command them.
  initializeRelayPin(RELAY_REVERSE_PIN);
  initializeRelayPin(RELAY_WHISTLE_PIN);
  setRelay(RELAY_REVERSE_PIN, false);
  setRelay(RELAY_WHISTLE_PIN, false);

  Serial.println("Relay control: ACTIVE HIGH (D6 Reverse, D7 Whistle)");

  pinMode(LOCAL_REMOTE_TOGGLE_PIN, INPUT_PULLUP);
  analogReadResolution(12);

  // FW v1.16: load saved HOME / START / MAX before the first servo command.
  loadCalibrationFromNvs();

  // ------------------------------------------------------------------------
  // First throttle action after every power-up: command HOME/OFF.
  // ------------------------------------------------------------------------
  if (!setupServoPwm()) {
    Serial.println("[FATAL] Could not initialize MG90S LEDC PWM on D2.");
    Serial.println("[FATAL] BLE/control startup halted for safety.");

    // Keep relays released and stop here. The fault is visible in Serial.
    setRelay(RELAY_REVERSE_PIN, false);
    setRelay(RELAY_WHISTLE_PIN, false);

    while (true) {
      delay(1000);
    }
  }

  Serial.print("[SERVO] HOME commanded at ");
  Serial.print(homeUs);
  Serial.println(" us (saved HOME = 0%; default 1000 us = full clockwise viewed from above)");
  Serial.print("[SERVO] Startup HOME hold = ");
  Serial.print(STARTUP_HOME_HOLD_MS);
  Serial.println(" ms, then PWM is released");

  Serial.print("[SERVO] 1% (START) = ");
  Serial.print(startUs);
  Serial.print(" us, 100% (MAX) = ");
  Serial.print(maxUs);
  Serial.print(" us, hard cap ");
  Serial.print(SERVO_MAX_LIMIT_US);
  Serial.println(" us");

  // Establish debounced switch state after HOME has already been commanded.
  stableLocalMode = readRawLocalMode();
  lastRawLocalMode = stableLocalMode;
  modeRawChangedAtMs = millis();

  // Remote always starts at 0%.
  remoteThrottlePercent = 0;
  setThrottleTargetPercent(0);

  if (stableLocalMode) {
    // Critical startup protection: Local pot cannot immediately pull the
    // throttle away from HOME if it was left at a nonzero setting.
    beginLocalPickup(0);
    Serial.println("[MODE] Power-up mode: LOCAL (pickup required from HOME)");
  } else {
    localPickupPending = false;
    Serial.println("[MODE] Power-up mode: REMOTE (holding HOME)");
  }

  setupBle();
  Serial.println("[BLE] Advertising started as \"O27 Controller\"");
}

// ============================================================================
// Main loop
// ============================================================================

void loop() {
  // FW v1.15/v1.16: execute queued BLE calibration commands first.
  serviceCalibrationCommands();

  // Physical mode switch and source selection.
  serviceModeSwitch();
  serviceThrottleSource();

  // Release startup HOME holding torque after the MG90S has settled.
  serviceStartupHomeRelease();

  // Non-blocking, rate-limited servo motion. If startup HOME detached PWM,
  // the first actual throttle movement re-attaches it automatically.
  serviceServoMotion();

  // ------------------------------------------------------------------------
  // Reverse: one-shot momentary pulse
  // ------------------------------------------------------------------------
  if (directionRequested && !reverseRelayActive) {
    Serial.println("[Relay] Reverse triggered from BLE request");

    startRelayPulse(
        RELAY_REVERSE_PIN,
        reverseRelayActive,
        reverseRelayOffAtMs);

    directionRequested = false;
  }

  serviceRelayPulse(
      RELAY_REVERSE_PIN,
      reverseRelayActive,
      reverseRelayOffAtMs);

  // ------------------------------------------------------------------------
  // Whistle: press-and-hold, with lost-release safety timeout
  // ------------------------------------------------------------------------
  bool hornHeldNow = hornHeld;

  if (hornHeldNow && !hornHeldPrev) {
    hornHeldStartMs = millis();
    hornSafetyCutoffLatched = false;
    Serial.println("[Relay] Horn ON");
  }

  if (!hornHeldNow && hornHeldPrev) {
    hornSafetyCutoffLatched = false;
    Serial.println("[Relay] Horn OFF");
  }

  bool hornSafetyExpired =
      hornHeldNow &&
      (millis() - hornHeldStartMs > HORN_MAX_HOLD_MS);

  if (hornSafetyExpired && !hornSafetyCutoffLatched) {
    hornSafetyCutoffLatched = true;
    Serial.println(
        "[Relay] Horn safety cutoff hit (HORN_MAX_HOLD_MS) - forcing OFF");
  }

  setRelay(
      RELAY_WHISTLE_PIN,
      hornHeldNow && !hornSafetyCutoffLatched);

  hornHeldPrev = hornHeldNow;

  // ------------------------------------------------------------------------
  // BLE status
  // ------------------------------------------------------------------------
  unsigned long now = millis();

  if (statusNotifyForced ||
      (now - lastStatusNotifyMs >= STATUS_NOTIFY_INTERVAL_MS)) {
    publishStatus();
    lastStatusNotifyMs = now;
    statusNotifyForced = false;
  }
}
