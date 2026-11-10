/*
  Lionel O27 Controller — Arduino Nano ESP32
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
#define SERVO_HOME_US 1000

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

// Average several ADC readings to reduce noise.
#define LOCAL_POT_AVERAGE_SAMPLES 8

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

BLEServer *pServer = nullptr;
BLECharacteristic *pThrottleChar = nullptr;
BLECharacteristic *pHornChar = nullptr;
BLECharacteristic *pDirectionChar = nullptr;
BLECharacteristic *pStatusChar = nullptr;

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
uint16_t currentServoUs = SERVO_HOME_US;
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
  long pulse = map(percent, 0, 100, SERVO_HOME_US, SERVO_MAX_US);
  return (uint16_t)constrain(pulse, SERVO_HOME_US, SERVO_MAX_US);
}

uint8_t servoUsToThrottlePercent(uint16_t pulseUs) {
  pulseUs = constrain(pulseUs, SERVO_HOME_US, SERVO_MAX_US);
  long percent = map(pulseUs, SERVO_HOME_US, SERVO_MAX_US, 0, 100);
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

  pulseUs = constrain(pulseUs, SERVO_HOME_US, SERVO_MAX_US);
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
  currentServoUs = SERVO_HOME_US;
  throttleTargetPercent = 0;

  if (!writeServoPulseUs(SERVO_HOME_US)) {
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
  if (throttleTargetPercent != 0 || currentServoUs != SERVO_HOME_US) {
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

  uint16_t targetUs = throttlePercentToUs(throttleTargetPercent);

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

uint8_t readLocalPotPercent() {
  uint32_t sum = 0;
  for (uint8_t i = 0; i < LOCAL_POT_AVERAGE_SAMPLES; ++i) {
    sum += analogRead(LOCAL_POT_PIN);
  }

  uint16_t raw = (uint16_t)(sum / LOCAL_POT_AVERAGE_SAMPLES);
  long percent = map(raw, 0, 4095, 0, 100);
  percent = constrain(percent, 0, 100);

  if (LOCAL_POT_REVERSED) {
    percent = 100 - percent;
  }

  return (uint8_t)percent;
}

void beginLocalPickup(uint8_t heldPercent) {
  localPickupTargetPercent = heldPercent;
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
  char out[96];

  snprintf(
      out,
      sizeof(out),
      "{\"throttle\":%u,\"mode\":\"%s\"}",
      currentThrottlePercent(),
      stableLocalMode ? "local" : "remote");

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
  Serial.println(" Lionel O27 Controller - Nano ESP32 FW v1.13");
  Serial.println(" UI v1.5 | MG90S throttle | Active-HIGH relays");
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
  Serial.print(SERVO_HOME_US);
  Serial.println(" us (full clockwise viewed from above)");
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

  if (now - lastStatusNotifyMs >= STATUS_NOTIFY_INTERVAL_MS) {
    publishStatus();
    lastStatusNotifyMs = now;
  }
}
