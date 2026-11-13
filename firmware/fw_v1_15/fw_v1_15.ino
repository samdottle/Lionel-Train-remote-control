/*
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

// Allowed range for a saved/calibrated HOME, and minimum span kept between
// HOME and the 100% pulse.
#define SERVO_HOME_MIN_US 900
#define SERVO_HOME_MAX_US 2000
#define SERVO_HOME_MIN_SPAN_US 200

// Conservative useful maximum. Full native-pot travel is not required.
#define SERVO_MAX_US 2200

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

// Calibration mode state (FW v1.15)
bool calActive = false;
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

uint16_t throttlePercentToUs(uint8_t percent) {
  percent = constrain(percent, 0, 100);
  long pulse = map(percent, 0, 100, homeUs, SERVO_MAX_US);
  return (uint16_t)constrain(pulse, homeUs, SERVO_MAX_US);
}

uint8_t servoUsToThrottlePercent(uint16_t pulseUs) {
  pulseUs = constrain(pulseUs, homeUs, SERVO_MAX_US);
  long percent = map(pulseUs, homeUs, SERVO_MAX_US, 0, 100);
  return (uint8_t)constrain(percent, 0, 100);
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

  // v1.15: absolute clamp (calibration may command below the saved HOME).
  pulseUs = constrain(pulseUs, SERVO_HOME_MIN_US, SERVO_MAX_US);
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

  // v1.15: in calibration mode the servo is driven to the working HOME value
  // instead of the throttle-derived value.
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

  if (currentServoUs < targetUs) {
    uint16_t remaining = targetUs - currentServoUs;
    nextUs = currentServoUs + min((uint16_t)SERVO_SLEW_STEP_US, remaining);
  } else {
    uint16_t remaining = currentServoUs - targetUs;
    nextUs = currentServoUs - min((uint16_t)SERVO_SLEW_STEP_US, remaining);
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
// HOME calibration (FW v1.15)
// ============================================================================

uint16_t clampHomeUs(long us) {
  long maxHome = SERVO_HOME_MAX_US;
  if (maxHome > (long)SERVO_MAX_US - SERVO_HOME_MIN_SPAN_US) {
    maxHome = (long)SERVO_MAX_US - SERVO_HOME_MIN_SPAN_US;
  }
  if (us < SERVO_HOME_MIN_US) us = SERVO_HOME_MIN_US;
  if (us > maxHome) us = maxHome;
  return (uint16_t)us;
}

void loadHomeFromNvs() {
  homeUs = SERVO_HOME_DEFAULT_US;

  // Opened read/write so the namespace is created on first boot (a read-only
  // open of a missing namespace fails and logs an NVS error).
  if (!prefs.begin("o27", false)) {
    Serial.println("[CAL] WARNING: could not open NVS; using default HOME");
    return;
  }

  if (prefs.isKey("homeUs")) {
    uint16_t stored = prefs.getUShort("homeUs", SERVO_HOME_DEFAULT_US);
    uint16_t clamped = clampHomeUs(stored);
    homeUs = clamped;

    Serial.print("[CAL] Saved HOME loaded from NVS: ");
    Serial.print(stored);
    Serial.print(" us");
    if (clamped != stored) {
      Serial.print(" (out of range, clamped to ");
      Serial.print(clamped);
      Serial.print(" us)");
    }
    Serial.println();
  } else {
    Serial.println("[CAL] No saved HOME; using default");
  }

  prefs.end();
}

bool saveHomeToNvs(uint16_t us) {
  if (!prefs.begin("o27", false)) {
    Serial.println("[CAL] ERROR: could not open NVS for writing");
    return false;
  }
  prefs.putUShort("homeUs", us);
  uint16_t back = prefs.getUShort("homeUs", 0);
  prefs.end();
  return back == us;
}

// Return to normal throttle control at 0% after leaving calibration.
void leaveCalibrationToZeroThrottle() {
  calActive = false;
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

  calWorkingUs = homeUs;
  calActive = true;

  Serial.print("[CAL] ENTER; working HOME = ");
  Serial.print(calWorkingUs);
  Serial.println(" us (normal throttle ignored)");
}

void calNudge(int delta) {
  if (!calActive) {
    Serial.println("[CAL] Nudge ignored (not in calibration)");
    return;
  }
  if (delta > CAL_NUDGE_MAX_US) delta = CAL_NUDGE_MAX_US;
  if (delta < -CAL_NUDGE_MAX_US) delta = -CAL_NUDGE_MAX_US;

  calWorkingUs = clampHomeUs((long)calWorkingUs + delta);

  Serial.print("[CAL] NUDGE ");
  Serial.print(delta);
  Serial.print(" -> working HOME = ");
  Serial.print(calWorkingUs);
  Serial.println(" us");
}

void calReset() {
  if (!calActive) {
    Serial.println("[CAL] Reset ignored (not in calibration)");
    return;
  }
  calWorkingUs = clampHomeUs(SERVO_HOME_DEFAULT_US);
  Serial.print("[CAL] RESET working HOME to default ");
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
  Serial.print(calWorkingUs);
  Serial.print(" us, saved HOME stays ");
  Serial.print(homeUs);
  Serial.println(" us");
  leaveCalibrationToZeroThrottle();
}

void calSave() {
  if (!calActive) {
    Serial.println("[CAL] Save ignored (not in calibration)");
    return;
  }

  uint16_t value = clampHomeUs(calWorkingUs);

  if (!saveHomeToNvs(value)) {
    Serial.println("[CAL] ERROR: NVS save failed/verify mismatch; staying in calibration");
    return;
  }

  homeUs = value;
  Serial.print("[CAL] SAVED HOME = ");
  Serial.print(homeUs);
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
    default:
      Serial.print("[CAL] Unknown command 0x");
      Serial.println(cmd, HEX);
      break;
  }
  statusNotifyForced = true;
}

void serviceCalibrationCommands() {
  // BLE disconnect while calibrating: revert to the saved HOME.
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

    // FW v1.15: abandon an unsaved calibration (handled in loop()).
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
  char out[112];

  // v1.15: added "cal" and "home" (working HOME while calibrating, else saved)
  snprintf(
      out,
      sizeof(out),
      "{\"throttle\":%u,\"mode\":\"%s\",\"cal\":%u,\"home\":%u}",
      currentThrottlePercent(),
      stableLocalMode ? "local" : "remote",
      calActive ? 1U : 0U,
      (unsigned)(calActive ? calWorkingUs : homeUs));

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
  Serial.println(" Lionel O27 Controller - Nano ESP32 FW v1.15 SG DRAFT");
  Serial.println(" UI v1.6 | MG90S throttle | Active-HIGH relays | HOME calibration");
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

  // FW v1.15: load saved HOME before the first servo command.
  loadHomeFromNvs();

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
  Serial.println(" us (saved HOME; default 1000 us = full clockwise viewed from above)");
  Serial.print("[SERVO] Startup HOME hold = ");
  Serial.print(STARTUP_HOME_HOLD_MS);
  Serial.println(" ms, then PWM is released");

  Serial.print("[SERVO] 100% limit = ");
  Serial.print(SERVO_MAX_US);
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
  // FW v1.15: execute queued BLE calibration commands first.
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
