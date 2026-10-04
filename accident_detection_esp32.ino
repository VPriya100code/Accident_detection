#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

// ============================================================
// MULTISENSOR ACCIDENT DETECTION + GSM ALERT
// ESP32 DevKit V1
//
// Hardware used:
// SW-420 vibration sensor  -> GPIO 19
// KY-038 sound sensor AO   -> GPIO 34
// HC-SR04 TRIG             -> GPIO 5
// HC-SR04 ECHO             -> GPIO 18 THROUGH 1k/2k divider
// OLED SDA                 -> GPIO 21
// OLED SCL                 -> GPIO 22
// LED                      -> GPIO 25 THROUGH 220 ohm
// Buzzer                   -> GPIO 23
// Push Button              -> GPIO 4 to GND (INPUT_PULLUP)
// SIM800L TX               -> ESP32 GPIO 16
// SIM800L RX               -> ESP32 GPIO 17
//
// Serial monitor / Python interface:
// 'R' = reset accident state
// 'D' = run a controlled demo accident event
// 'S' = print current status
// ============================================================

// ---------------- PIN DEFINITIONS ----------------
const int VIBRATION_PIN = 19;
const int SOUND_PIN     = 34;
const int TRIG_PIN      = 5;
const int ECHO_PIN      = 18;

const int OLED_SDA      = 21;
const int OLED_SCL      = 22;

const int LED_PIN       = 25;
const int BUZZER_PIN    = 23;
const int BUTTON_PIN    = 4;

const int SIM800_RX     = 16;  // ESP32 receives from SIM800L TX
const int SIM800_TX     = 17;  // ESP32 sends to SIM800L RX

// ---------------- OLED ----------------
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_RESET -1
#define OLED_ADDRESS 0x3C

Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

// ---------------- GSM ----------------
HardwareSerial sim800(1);

// CHANGE THIS TO YOUR REAL EMERGENCY NUMBER
const char PHONE_NUMBER[] = "+91XXXXXXXXXX";

// Fixed-location prototype information
const char LOCATION_ID[] = "RS-01";
const char LOCATION_NAME[] = "College Main Gate";

// ---------------- THRESHOLDS ----------------
// These are PROTOTYPE values and should be calibrated.
const int SOUND_THRESHOLD = 1800;
const float PROXIMITY_THRESHOLD_CM = 20.0;

// Confidence weights
const int VIBRATION_WEIGHT = 40;
const int SOUND_WEIGHT     = 30;
const int PROXIMITY_WEIGHT = 30;

// Accident is confirmed only when confidence >= this value
const int ACCIDENT_THRESHOLD = 60;

// Number of consecutive qualifying readings required
const int REQUIRED_CONSECUTIVE = 3;

// IMPORTANT:
// Most SW-420 modules used in common ESP32 kits go HIGH when vibration
// is detected. If your Serial Monitor shows the opposite, change HIGH to LOW.
const int VIBRATION_ACTIVE_STATE = HIGH;

// Sensor reading interval
const unsigned long SENSOR_INTERVAL_MS = 250;

// Button debounce
const unsigned long BUTTON_DEBOUNCE_MS = 50;

// ---------------- STATE ----------------
bool accidentConfirmed = false;
bool alertSent = false;
bool demoEvent = false;

int consecutiveHits = 0;

int vibrationState = LOW;
int soundValue = 0;
float distanceCm = -1.0;
int confidence = 0;

String severity = "NONE";
String systemState = "MONITORING";

unsigned long lastSensorRead = 0;
unsigned long lastButtonChange = 0;
bool lastButtonState = HIGH;

// ---------------- FUNCTION DECLARATIONS ----------------
float readDistanceCm();
int calculateConfidence(bool vibrationDetected, bool soundDetected, bool proximityDetected);
String calculateSeverity(int score);

void updateOLED();
void activateLocalAlert();
void clearLocalAlert();

bool sendSMS(const String &message);
bool waitForSIM800(const String &expected, unsigned long timeoutMs);
void initializeSIM800();

void confirmAccident(bool fromDemo);
void resetAccident();
void printLiveData();
void processSerialCommand();

// ---------------- SETUP ----------------
void setup() {
  Serial.begin(115200);
  delay(500);

  pinMode(VIBRATION_PIN, INPUT);
  pinMode(SOUND_PIN, INPUT);
  pinMode(TRIG_PIN, OUTPUT);
  pinMode(ECHO_PIN, INPUT);

  pinMode(LED_PIN, OUTPUT);
  pinMode(BUZZER_PIN, OUTPUT);

  pinMode(BUTTON_PIN, INPUT_PULLUP);

  digitalWrite(TRIG_PIN, LOW);
  digitalWrite(LED_PIN, LOW);
  digitalWrite(BUZZER_PIN, LOW);

  // OLED
  Wire.begin(OLED_SDA, OLED_SCL);

  if (!display.begin(SSD1306_SWITCHCAPVCC, OLED_ADDRESS)) {
    Serial.println("ERROR|OLED_INIT_FAILED");
  } else {
    display.clearDisplay();
    display.setTextColor(SSD1306_WHITE);
    display.setTextSize(1);
    display.setCursor(0, 0);
    display.println("ACCIDENT SAFETY");
    display.println("SYSTEM");
    display.println();
    display.println("Initializing...");
    display.display();
  }

  // SIM800L
  sim800.begin(9600, SERIAL_8N1, SIM800_RX, SIM800_TX);
  delay(1000);
  initializeSIM800();

  updateOLED();

  Serial.println("READY|ACCIDENT_SYSTEM");
  Serial.println("INFO|Commands: R=Reset, D=Demo Alert, S=Status");
}

// ---------------- MAIN LOOP ----------------
void loop() {
  processSerialCommand();

  // Push-button reset
  bool currentButtonState = digitalRead(BUTTON_PIN);

  if (currentButtonState != lastButtonState) {
    lastButtonChange = millis();
    lastButtonState = currentButtonState;
  }

  if ((millis() - lastButtonChange) > BUTTON_DEBOUNCE_MS) {
    if (currentButtonState == LOW) {
      // Wait for release so one press causes one reset
      delay(20);
      resetAccident();

      while (digitalRead(BUTTON_PIN) == LOW) {
        delay(5);
      }
    }
  }

  // Do not overwrite a confirmed accident until reset
  if (accidentConfirmed) {
    delay(10);
    return;
  }

  if (millis() - lastSensorRead >= SENSOR_INTERVAL_MS) {
    lastSensorRead = millis();

    vibrationState = digitalRead(VIBRATION_PIN);
    soundValue = analogRead(SOUND_PIN);
    distanceCm = readDistanceCm();

    bool vibrationDetected = (vibrationState == VIBRATION_ACTIVE_STATE);
    bool soundDetected = (soundValue >= SOUND_THRESHOLD);
    bool proximityDetected =
        (distanceCm > 0 && distanceCm <= PROXIMITY_THRESHOLD_CM);

    confidence = calculateConfidence(
        vibrationDetected,
        soundDetected,
        proximityDetected
    );

    if (confidence >= ACCIDENT_THRESHOLD) {
      consecutiveHits++;
    } else {
      consecutiveHits = 0;
    }

    systemState = "MONITORING";
    severity = "NONE";

    if (consecutiveHits >= REQUIRED_CONSECUTIVE) {
      confirmAccident(false);
    }

    updateOLED();
    printLiveData();
  }
}

// ---------------- SENSOR FUNCTIONS ----------------
float readDistanceCm() {
  digitalWrite(TRIG_PIN, LOW);
  delayMicroseconds(2);

  digitalWrite(TRIG_PIN, HIGH);
  delayMicroseconds(10);
  digitalWrite(TRIG_PIN, LOW);

  // 30 ms timeout prevents the loop from getting stuck
  unsigned long duration = pulseIn(ECHO_PIN, HIGH, 30000UL);

  if (duration == 0) {
    return -1.0;
  }

  float distance = (duration * 0.0343) / 2.0;

  // Ignore clearly invalid values
  if (distance < 2.0 || distance > 400.0) {
    return -1.0;
  }

  return distance;
}

int calculateConfidence(bool vibrationDetected,
                        bool soundDetected,
                        bool proximityDetected) {
  int score = 0;

  if (vibrationDetected) {
    score += VIBRATION_WEIGHT;
  }

  if (soundDetected) {
    score += SOUND_WEIGHT;
  }

  if (proximityDetected) {
    score += PROXIMITY_WEIGHT;
  }

  return score;
}

String calculateSeverity(int score) {
  // Prototype thresholds
  if (score >= 90) {
    return "HIGH";
  } else if (score >= 75) {
    return "MEDIUM";
  } else if (score >= 60) {
    return "LOW";
  }

  return "NONE";
}

// ---------------- ACCIDENT HANDLING ----------------
void confirmAccident(bool fromDemo) {
  accidentConfirmed = true;
  demoEvent = fromDemo;
  systemState = fromDemo ? "DEMO ALERT" : "ACCIDENT";

  if (fromDemo) {
    confidence = 100;
    severity = "HIGH";
  } else {
    severity = calculateSeverity(confidence);
  }

  activateLocalAlert();
  updateOLED();
  printLiveData();

  Serial.println("EVENT|ACCIDENT_CONFIRMED");
  Serial.print("EVENT|SOURCE|");
  Serial.println(fromDemo ? "DEMO" : "SENSORS");

  if (!alertSent) {
    String sms =
        "ACCIDENT ALERT!\n"
        "Severity: " + severity +
        "\nConfidence: " + String(confidence) + "%" +
        "\nLocation ID: " + String(LOCATION_ID) +
        "\nLocation: " + String(LOCATION_NAME);

    bool sent = sendSMS(sms);

    if (sent) {
      alertSent = true;
      Serial.println("SMS_SENT|1");
    } else {
      Serial.println("SMS_SENT|0");
    }
  }
}

void resetAccident() {
  accidentConfirmed = false;
  alertSent = false;
  demoEvent = false;

  consecutiveHits = 0;
  confidence = 0;
  severity = "NONE";
  systemState = "MONITORING";

  clearLocalAlert();
  updateOLED();

  Serial.println("RESET|SYSTEM_RESET");
}

void activateLocalAlert() {
  digitalWrite(LED_PIN, HIGH);

  // Simple active-buzzer control
  digitalWrite(BUZZER_PIN, HIGH);
}

void clearLocalAlert() {
  digitalWrite(LED_PIN, LOW);
  digitalWrite(BUZZER_PIN, LOW);
}

// ---------------- OLED ----------------
void updateOLED() {
  if (!display.width()) {
    return;
  }

  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);

  if (!accidentConfirmed) {
    display.setTextSize(1);
    display.setCursor(0, 0);
    display.println("ACCIDENT SAFETY");
    display.println("SYSTEM");
    display.println("----------------");

    display.print("Vibration: ");
    display.println(
      vibrationState == VIBRATION_ACTIVE_STATE ? "YES" : "NO"
    );

    display.print("Sound: ");
    display.println(soundValue);

    display.print("Distance: ");
    if (distanceCm < 0) {
      display.println("--");
    } else {
      display.print(distanceCm, 1);
      display.println(" cm");
    }

    display.print("Confidence: ");
    display.print(confidence);
    display.println("%");

    display.println();
    display.println("MONITORING...");
  } else {
    display.setTextSize(1);
    display.setCursor(0, 0);
    display.println(demoEvent ? "DEMO ALERT" : "ACCIDENT DETECTED");
    display.println("----------------");
    display.print("Confidence: ");
    display.print(confidence);
    display.println("%");

    display.print("Severity: ");
    display.println(severity);

    display.println();
    display.println("LED: ON");
    display.println("Buzzer: ON");
    display.println("SMS: SENT/ATTEMPTED");
  }

  display.display();
}

// ---------------- SERIAL LIVE DATA ----------------
void printLiveData() {
  // Fixed CSV format for the Python interface:
  // LIVE,vibration,sound,distance,confidence,state,severity,alertSent

  Serial.print("LIVE,");
  Serial.print(vibrationState == VIBRATION_ACTIVE_STATE ? 1 : 0);
  Serial.print(",");
  Serial.print(soundValue);
  Serial.print(",");
  Serial.print(distanceCm, 1);
  Serial.print(",");
  Serial.print(confidence);
  Serial.print(",");
  Serial.print(systemState);
  Serial.print(",");
  Serial.print(severity);
  Serial.print(",");
  Serial.println(alertSent ? 1 : 0);
}

// ---------------- GSM ----------------
void initializeSIM800() {
  Serial.println("GSM|INITIALIZING");

  sim800.println("AT");
  delay(500);

  sim800.println("ATE0");
  delay(300);

  sim800.println("AT+CMGF=1");
  delay(500);

  sim800.println("AT+CSCS=\"GSM\"");
  delay(500);

  Serial.println("GSM|READY");
}

bool waitForSIM800(const String &expected, unsigned long timeoutMs) {
  unsigned long start = millis();
  String response = "";

  while (millis() - start < timeoutMs) {
    while (sim800.available()) {
      char c = sim800.read();
      response += c;

      if (response.indexOf(expected) >= 0) {
        return true;
      }

      if (response.indexOf("ERROR") >= 0) {
        return false;
      }
    }
  }

  return false;
}

bool sendSMS(const String &message) {
  // Safety check: do not try to use an unconfigured number.
  if (String(PHONE_NUMBER).indexOf("XXXXXXXXXX") >= 0) {
    Serial.println("GSM|PHONE_NUMBER_NOT_SET");
    return false;
  }

  Serial.println("GSM|SENDING_SMS");

  // Clear old responses
  while (sim800.available()) {
    sim800.read();
  }

  sim800.println("AT");
  if (!waitForSIM800("OK", 3000)) {
    Serial.println("GSM|NO_AT_RESPONSE");
    return false;
  }

  sim800.println("AT+CMGF=1");
  if (!waitForSIM800("OK", 3000)) {
    Serial.println("GSM|CMGF_FAILED");
    return false;
  }

  sim800.print("AT+CMGS=\"");
  sim800.print(PHONE_NUMBER);
  sim800.println("\"");

  if (!waitForSIM800(">", 5000)) {
    Serial.println("GSM|NO_PROMPT");
    return false;
  }

  sim800.print(message);
  sim800.write(26);  // Ctrl+Z

  bool sent = waitForSIM800("OK", 15000);

  if (sent) {
    Serial.println("GSM|SMS_OK");
  } else {
    Serial.println("GSM|SMS_FAILED");
  }

  return sent;
}

// ---------------- PYTHON / SERIAL COMMANDS ----------------
void processSerialCommand() {
  while (Serial.available()) {
    char cmd = Serial.read();

    if (cmd == 'R' || cmd == 'r') {
      resetAccident();
    }

    else if (cmd == 'D' || cmd == 'd') {
      // Controlled presentation/demo event.
      // This is clearly marked as DEMO ALERT.
      if (!accidentConfirmed) {
        confirmAccident(true);
      }
    }

    else if (cmd == 'S' || cmd == 's') {
      printLiveData();
      Serial.print("STATUS|Confidence=");
      Serial.print(confidence);
      Serial.print("|Severity=");
      Serial.print(severity);
      Serial.print("|State=");
      Serial.println(systemState);
    }
  }
}
