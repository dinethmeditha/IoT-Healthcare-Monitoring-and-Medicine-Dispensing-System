#include <WiFi.h>
#include <WebServer.h>
#include <esp_now.h>
#include <esp_wifi.h>
#include <Wire.h>
#include "MAX30105.h"
#include "heartRate.h"
#include "spo2_algorithm.h"
#include <OneWire.h>
#include <DallasTemperature.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SH110X.h>

#define VIBRATION_PIN 5
#define ONE_WIRE_BUS 4  // DS18B20 Data pin
#define REPORTING_PERIOD_MS 1000
#define LED_PIN 2  // LED to blink on button press

// OLED Display setup
#define i2c_Address 0x3c
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_RESET -1
Adafruit_SH1106G display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

// MAX30102 Algorithm variables
MAX30105 particleSensor;
const byte RATE_SIZE = 8;  // Increased for better averaging
byte rates[RATE_SIZE];
byte rateSpot = 0;
long lastBeat = 0;
int32_t beatsPerMinute;
int8_t beatAvg;

// SpO2 buffer management
#define MAX_SPO2_SAMPLES 100
uint32_t irBuffer[MAX_SPO2_SAMPLES];
uint32_t redBuffer[MAX_SPO2_SAMPLES];
int32_t bufferLength;
int32_t spo2;
int8_t validSPO2;

// BPM-specific buffer for heart rate (separate for better detection)
#define MAX_HR_SAMPLES 100
uint32_t irHRBuffer[MAX_HR_SAMPLES];
uint16_t hrBufferIndex = 0;

float BPM = 0, SpO2 = 0, temperatureC = 0.0;
uint32_t tsLastReport = 0;
bool sensorReady = false;
bool alertActive = false;

// Sensor value offsets
const float tempOffset = 1.0;
const int bpmOffset = 20;
const int spo2Offset = 10;

// Sample management
uint16_t spo2BufferIndex = 0;
bool spo2BufferFilled = false;
bool fingerDetected = false;
bool beatsDetected = false;  // New: Track if any beats found
unsigned long fingerPlaceTime = 0;  // For timeout

// Bitmap icons (unchanged)
const unsigned char heart_icon [] PROGMEM = {
  0x66, 0xFF, 0xFF, 0x7E, 0x3C, 0x18
};
const unsigned char pulse_icon [] PROGMEM = {
  0x08, 0x3E, 0x08, 0x00, 0x00, 0x00
};
const unsigned char temp_icon [] PROGMEM = {
  0x1C, 0x22, 0x37, 0x7F, 0x7F, 0x3E
};
const unsigned char lungs_icon [] PROGMEM = {
  0x3C, 0x66, 0x66, 0x7E, 0x3C, 0x00
};

// LED blink control
bool ledBlinking = false;
unsigned long ledBlinkStart = 0;
const unsigned long ledBlinkDuration = 2000;

// Web server
WebServer server(80);
const char* ssid = "Health Monitor";

// DS18B20 setup
OneWire oneWire(ONE_WIRE_BUS);
DallasTemperature sensors(&oneWire);

// Button pins and messages (unchanged)
const int buttonPins[8] = {13, 12, 14, 27, 26, 25, 33, 32};
bool lastButtonState[8] = {HIGH, HIGH, HIGH, HIGH, HIGH, HIGH, HIGH, HIGH};
unsigned long lastDebounceTime[8] = {0};
const unsigned long debounceDelay = 300;
String lastMessage = "No message yet";
const char* buttonMessages[8] = {
  "Medicine A Dispensed", "Medicine B Dispensed", "Vitals Checked", "Water Reminder",
  "Doctor Alert Sent", "Time to Rest", "Nurse Assistance Needed", "Emergency Alert Triggered"
};

// ESP-NOW callback (unchanged)
void onReceive(const esp_now_recv_info_t *info, const uint8_t *incomingData, int len) {
  Serial.println("ESP-NOW Message Received.");
  if (len == 1 && incomingData[0] == 1) {
    digitalWrite(VIBRATION_PIN, HIGH);
    delay(10000);
    digitalWrite(VIBRATION_PIN, LOW);
  }
}

// HTML Page (updated with beats status)
String SendHTML(float BPM, float SpO2, float temperatureC_raw, String lastMessage, bool fingerDetected, bool beatsDetected) {
  float temperatureC = temperatureC_raw + tempOffset;
  bool bpmOK = (BPM + bpmOffset) >= 60 && (BPM + bpmOffset) <= 120;
  bool spo2OK = SpO2 >= 95;
  bool tempOK = temperatureC >= 35 && temperatureC <= 38;

  String outOfRange = "";
  if (!bpmOK && BPM > 0) outOfRange += "Heart Rate, ";
  if (!spo2OK && SpO2 > 0) outOfRange += "SpO₂, ";
  if (!tempOK) outOfRange += "Temperature, ";
  if (outOfRange.endsWith(", ")) outOfRange = outOfRange.substring(0, outOfRange.length() - 2);

  String html = R"rawliteral(
<!DOCTYPE html>
<html><head><meta charset="UTF-8"><title>Health Monitor</title>
<meta name="viewport" content="width=device-width, initial-scale=1.0"><meta http-equiv="refresh" content="3">
<style>body{font-family:Arial;background:#f0f0f0;margin:0;padding:20px;}.card{background:white;padding:20px;border-radius:8px;box-shadow:0 2px 4px rgba(0,0,0,0.1);max-width:400px;margin:0 auto;}
h1{color:#008080;margin-bottom:15px;text-align:center;}p{font-size:18px;margin:8px 0;}.alert{color:white;background:#e74c3c;padding:8px;border-radius:4px;margin-top:10px;font-weight:bold;}
.status{color:#27ae60;font-weight:bold;}meter{width:100%;height:20px;margin-bottom:15px;}</style></head><body>
<div class="card"><h1>Health Monitor</h1>)rawliteral";

  if (sensorReady) {
    String bpmStatus = fingerDetected ? (beatsDetected ? String((int)(BPM + bpmOffset)) : "No Beats") : "No Finger";
    html += "<p><b>Heart Rate:</b> " + bpmStatus + " BPM</p>";
    if (beatsDetected && BPM > 0) html += "<meter min='40' max='180' low='60' high='120' optimum='75' value='" + String((int)(BPM + bpmOffset)) + "'></meter>";
    
    String spo2Status = fingerDetected ? String((int)SpO2) : "No Finger";
    html += "<p><b>SpO₂:</b> " + spo2Status + " %</p>";
    if (SpO2 > 0) html += "<meter min='70' max='100' low='90' high='95' optimum='98' value='" + String((int)SpO2) + "'></meter>";
  } else {
    html += "<p style='color:red;'>MAX30102 Not Detected</p>";
  }

  html += "<p><b>Temperature:</b> " + String(temperatureC, 1) + " °C</p><meter min='30' max='45' low='35' high='38' optimum='36.5' value='" + String(temperatureC, 1) + "'></meter>";

  if (outOfRange.length() > 0) {
    html += "<div class='alert'>⚠️ Alert: " + outOfRange + " out of range!</div>";
  } else if (sensorReady && BPM > 0 && SpO2 > 0) {
    html += "<p class='status'>✓ All readings normal</p>";
  }

  html += "<p><b>Last Message:</b> " + lastMessage + "</p><p><small>Auto-refresh every 3 seconds</small></p></div></body></html>";
  return html;
}

void updateDisplay() {
  display.clearDisplay();
  display.setTextColor(SH110X_WHITE);
  
  display.setCursor(0, 0); display.setTextSize(1); display.print("HYGEIA");
  display.drawBitmap(42, 0, heart_icon, 6, 6, SH110X_WHITE);
  display.setCursor(120, 0);
  display.print(WiFi.softAPgetStationNum() > 0 ? "*" : "o");
  
  display.drawFastHLine(0, 9, SCREEN_WIDTH, SH110X_WHITE);
  
  bool bpmOK = ((BPM + bpmOffset) >= 60 && (BPM + bpmOffset) <= 120) || BPM == 0;
  bool spo2OK = (SpO2 >= 95) || SpO2 == 0;
  float adjTemp = temperatureC + tempOffset;
  bool tempOK = adjTemp >= 35 && adjTemp <= 38;
  
  // Vitals row
  display.setCursor(0, 12); display.drawBitmap(0, 12, pulse_icon, 6, 6, SH110X_WHITE);
  display.setCursor(8, 12);
  if (sensorReady && BPM > 0) {
    display.print(String((int)(BPM + bpmOffset)));
  } else if (fingerDetected && beatsDetected) {
    display.print("..");
  } else if (fingerDetected) {
    display.print("NO BEAT");
  } else {
    display.print("--");
  }
  
  display.setCursor(45, 12); display.drawBitmap(45, 12, lungs_icon, 6, 6, SH110X_WHITE);
  display.setCursor(53, 12);
  if (sensorReady && SpO2 > 0) {
    display.print(String((int)SpO2) + "%");
  } else {
    display.print(fingerDetected ? "..%" : "--%");
  }
  
  display.setCursor(85, 12); display.drawBitmap(85, 12, temp_icon, 6, 6, SH110X_WHITE);
  display.setCursor(93, 12); display.print(String(adjTemp, 1) + "C");
  
  display.drawFastHLine(0, 22, SCREEN_WIDTH, SH110X_WHITE);
  
  display.setCursor(0, 25);
  if ((!bpmOK && BPM > 0) || (!spo2OK && SpO2 > 0) || !tempOK) {
    display.print("ALERT: CHECK VITALS");
  } else if (sensorReady && BPM > 0 && SpO2 > 0) {
    display.print("STATUS: NORMAL");
  } else if (fingerDetected && !beatsDetected && (millis() - fingerPlaceTime > 10000)) {
    display.print("NO BEATS DETECTED");
  } else if (fingerDetected) {
    display.print("CALCULATING...");
  } else {
    display.print("PLACE FINGER");
  }
  
  display.drawFastHLine(0, 35, SCREEN_WIDTH, SH110X_WHITE);
  
  display.setCursor(0, 38); display.print("MSG:"); display.setCursor(0, 48);
  String shortMsg = lastMessage.length() > 21 ? lastMessage.substring(0, 18) + "..." : lastMessage;
  display.print(lastMessage == "No message yet" ? "No recent messages" : shortMsg);
  
  display.display();
}

void showSplashScreen() {
  display.clearDisplay(); display.setTextColor(SH110X_WHITE);
  display.setCursor(20, 10); display.setTextSize(2); display.print("HYGEIA");
  display.setCursor(10, 30); display.setTextSize(1); display.print("Health Monitor");
  display.setCursor(25, 45); display.print("Starting...");
  display.display(); delay(2000);
}

void checkButtons() {
  for (int i = 0; i < 8; i++) {
    bool currentState = digitalRead(buttonPins[i]);
    if (currentState == LOW && lastButtonState[i] == HIGH && (millis() - lastDebounceTime[i] > debounceDelay)) {
      lastDebounceTime[i] = millis();
      lastMessage = buttonMessages[i];
      Serial.print("Button "); Serial.print(i + 1); Serial.print(": "); Serial.println(lastMessage);
      digitalWrite(LED_PIN, HIGH); ledBlinking = true; ledBlinkStart = millis();
    }
    lastButtonState[i] = currentState;
  }
}

void handle_OnConnect() {
  server.send(200, "text/html", SendHTML(BPM, SpO2, temperatureC, lastMessage, fingerDetected, beatsDetected));
}

void handle_NotFound() {
  server.send(404, "text/plain", "Not found");
}

// Enhanced sensor reading: Separate BPM and SpO2 paths
void readSensorData() {
  if (!sensorReady) return;

  particleSensor.check();  // Poll for new data

  if (!particleSensor.available()) return;

  uint32_t irValue = particleSensor.getFIFOIR();
  uint32_t redValue = particleSensor.getFIFORed();
  particleSensor.nextSample();

  Serial.print("IR: "); Serial.print(irValue); Serial.print(" | Red: "); Serial.println(redValue);

  // Finger detection
  fingerDetected = (irValue > 50000);
  if (fingerDetected && fingerPlaceTime == 0) fingerPlaceTime = millis();

  if (!fingerDetected) {
    BPM = 0; SpO2 = 0; beatsDetected = false;
    spo2BufferIndex = 0; spo2BufferFilled = false; hrBufferIndex = 0;
    fingerPlaceTime = 0;
    return;
  }

  // SpO2 buffer (unchanged, working for you)
  irBuffer[spo2BufferIndex] = irValue;
  redBuffer[spo2BufferIndex] = redValue;
  spo2BufferIndex = (spo2BufferIndex + 1) % MAX_SPO2_SAMPLES;
  if (spo2BufferIndex == 0) spo2BufferFilled = true;

  if (spo2BufferFilled) {
    bufferLength = MAX_SPO2_SAMPLES;
    maxim_heart_rate_and_oxygen_saturation(irBuffer, bufferLength, redBuffer, &spo2, &validSPO2, &beatsPerMinute, &beatAvg);
    if (validSPO2 > 0 && spo2 > 70 && spo2 < 101) {
      SpO2 = spo2;
      // Apply the requested adjustment
      if (SpO2 < 90) {
        SpO2 += spo2Offset;
      }
      Serial.print("Valid SpO2: "); Serial.println(SpO2);
    }
  }

  // Separate BPM detection (improved: dedicated buffer, noise filter)
  // Simple DC removal (subtract average from recent samples)
  uint32_t avgIR = 0;
  for (int i = 0; i < 4; i++) {  // Rolling average of last 4
    int idx = (hrBufferIndex - i + MAX_HR_SAMPLES) % MAX_HR_SAMPLES;
    avgIR += irHRBuffer[idx];
  }
  avgIR /= 4;
  uint32_t dcRemovedIR = irValue > avgIR ? irValue - avgIR : avgIR - irValue;  // Abs diff

  irHRBuffer[hrBufferIndex] = dcRemovedIR;  // Store filtered
  hrBufferIndex = (hrBufferIndex + 1) % MAX_HR_SAMPLES;

  // Check for beat on filtered signal
  if (checkForBeat(dcRemovedIR) == true) {
    long delta = millis() - lastBeat;
    lastBeat = millis();

    // Filter short deltas (noise)
    if (delta > 250) {  // Min 240ms (~240 BPM max, but realistic filter)
      beatsPerMinute = 60 / (delta / 1000.0);
      if (beatsPerMinute < 255 && beatsPerMinute > 20) {
        rates[rateSpot++] = (byte)beatsPerMinute;
        rateSpot %= RATE_SIZE;
        long total = 0;
        for (byte i = 0; i < RATE_SIZE; i++) total += rates[i]; 
        BPM = total / RATE_SIZE;
        beatsDetected = true;
        Serial.print("Beat detected! Delta: "); Serial.print(delta); Serial.print("ms | Avg BPM: "); Serial.println(BPM);
      }
    } else {
      Serial.print("Noise beat filtered (delta: "); Serial.print(delta); Serial.println("ms)");
    }
  }
}

void setup() {
  Serial.begin(115200);
  pinMode(VIBRATION_PIN, OUTPUT); digitalWrite(VIBRATION_PIN, LOW);
  pinMode(LED_PIN, OUTPUT); digitalWrite(LED_PIN, LOW);

  Wire.begin();  // SDA=21, SCL=22
  sensors.begin();

  // Initialize OLED
  if (!display.begin(i2c_Address, true)) {
    Serial.println(F("SH1106 allocation failed"));
    for (;;);
  }
  showSplashScreen();

  // MAX30102 Init (slower rate for better BPM peaks)
  Serial.println("Initializing MAX30102...");
  if (!particleSensor.begin(Wire, I2C_SPEED_FAST)) {
    Serial.println("MAX30102 not found. Check wiring!");
    sensorReady = false;
  } else {
    Serial.println("MAX30102 found!");
    sensorReady = true;
    
    // Settings for clearer signal (lower rate)
    byte ledBrightness = 60;
    byte sampleAverage = 4;
    byte ledMode = 2;
    int sampleRate = 100;  // Slower for BPM accuracy
    int pulseWidth = 411;
    int adcRange = 4096;
    
    particleSensor.setup(ledBrightness, sampleAverage, ledMode, sampleRate, pulseWidth, adcRange);
    Serial.println("MAX30102 configured (100Hz). Press finger firmly & stay still!");
    
    // Init buffers
    for (int i = 0; i < MAX_SPO2_SAMPLES; i++) {
      irBuffer[i] = 0; redBuffer[i] = 0;
    }
    for (int i = 0; i < MAX_HR_SAMPLES; i++) {
      irHRBuffer[i] = 50000;  // Init to mid-range for DC removal
    }
    for (int i = 0; i < RATE_SIZE; i++) rates[i] = 72;  // Init to resting avg
  }

  // Buttons
  for (int i = 0; i < 8; i++) pinMode(buttonPins[i], INPUT_PULLUP);

  // WiFi AP
  WiFi.mode(WIFI_AP); WiFi.softAP(ssid);
  Serial.print("AP IP: "); Serial.println(WiFi.softAPIP());

  server.on("/", handle_OnConnect); server.onNotFound(handle_NotFound); server.begin();

  // MAC and ESP-NOW (unchanged)
  uint8_t mac[6]; esp_wifi_get_mac(WIFI_IF_AP, mac);
  Serial.print("Receiver MAC: ");
  for (int i = 0; i < 6; i++) { if (mac[i] < 16) Serial.print("0"); Serial.print(mac[i], HEX); if (i < 5) Serial.print(":"); }
  Serial.println();

  if (esp_now_init() != ESP_OK) { Serial.println("ESP-NOW INIT FAILED"); while (true) delay(1000); }
  esp_now_register_recv_cb(onReceive);
  
  Serial.println("Setup complete. Monitor Serial for beats.");
}

void loop() {
  server.handleClient();
  
  readSensorData();

  if (millis() - tsLastReport > REPORTING_PERIOD_MS) {
    sensors.requestTemperatures();
    temperatureC = sensors.getTempCByIndex(0);
    
    float adjTemp = temperatureC + tempOffset;
    bool bpmOK = ((BPM + bpmOffset) >= 60 && (BPM + bpmOffset) <= 120) || BPM == 0;
    bool spo2OK = (SpO2 >= 95) || SpO2 == 0;
    bool tempOK = adjTemp >= 35 && adjTemp <= 38;

    tsLastReport = millis();
    alertActive = !((bpmOK || BPM == 0) && (spo2OK || SpO2 == 0) && tempOK);

    // Reset beats if no finger for 5s
    if (!fingerDetected && fingerPlaceTime > 0 && (millis() - fingerPlaceTime > 5000)) {
      beatsDetected = false;
    }

    updateDisplay();

    // Debug Serial every 3s
    static unsigned long lastSerialUpdate = 0;
    if (millis() - lastSerialUpdate > 3000) {
      Serial.println("=== HYGEIA DEBUG ===");
      Serial.print("Finger: "); Serial.print(fingerDetected ? "YES" : "NO");
      Serial.print(" | Beats: "); Serial.print(beatsDetected ? "YES" : "NO");
      Serial.print(" | BPM: "); if (!bpmOK && BPM > 0) Serial.print("[!] "); Serial.print(sensorReady ? String((int)(BPM + bpmOffset)) : "N/A");
      Serial.print(" | SpO2: "); if (!spo2OK && SpO2 > 0) Serial.print("[!] "); Serial.print(sensorReady ? String((int)SpO2) + "%" : "N/A");
      Serial.print(" | Temp: "); if (!tempOK) Serial.print("[!] "); Serial.println(String(adjTemp, 1) + "°C");
      if (alertActive) Serial.println("*** ALERT: Parameters out of range! ***");
      Serial.println("====================");
      lastSerialUpdate = millis();
    }
  }

  checkButtons();

  if (alertActive && !ledBlinking) {
    digitalWrite(LED_PIN, HIGH); ledBlinking = true; ledBlinkStart = millis();
  }
  if (ledBlinking && millis() - ledBlinkStart >= ledBlinkDuration) {
    digitalWrite(LED_PIN, LOW); ledBlinking = false;
  }

  delay(10);  // Faster loop for beat detection
}