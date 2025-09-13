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
const byte RATE_SIZE = 4;
byte rates[RATE_SIZE];
byte rateSpot = 0;
long lastBeat = 0;
int32_t beatsPerMinute;
int8_t beatAvg;

// FIXED: Proper SpO2 buffer management
#define MAX_SPO2_SAMPLES 100
uint32_t irBuffer[MAX_SPO2_SAMPLES];
uint32_t redBuffer[MAX_SPO2_SAMPLES];
int32_t bufferLength;
int32_t spo2;
int8_t validSPO2;

float BPM = 0, SpO2 = 0, temperatureC = 0.0;
uint32_t tsLastReport = 0;
bool sensorReady = false;

// FIXED: Better sample management
static uint16_t bufferIndex = 0;  // Changed to uint16_t and better naming
static bool bufferFilled = false;
static unsigned long lastSensorRead = 0;
static unsigned long lastTempRead = 0;
static unsigned long lastDisplayUpdate = 0;

// Bitmap icons (keeping your existing ones)
const unsigned char heart_icon [] PROGMEM = {
  0x66, 0xFF, 0xFF, 0x7E, 0x3C, 0x18
};

const unsigned char heart_beat_icon [] PROGMEM = {
  0x7E, 0xFF, 0xFF, 0xFF, 0x7E, 0x3C
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

const unsigned char warning_icon [] PROGMEM = {
  0x18, 0x3C, 0x7E, 0x18, 0x00, 0x18
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

// Button pins and messages
const int buttonPins[8] = {13, 12, 14, 27, 26, 25, 33, 32};
bool lastButtonState[8] = {HIGH};
unsigned long lastDebounceTime[8] = {0};
const unsigned long debounceDelay = 300;
String lastMessage = "No message yet";
const char* buttonMessages[8] = {
  "Medicine A Dispensed",
  "Medicine B Dispensed",
  "Vitals Checked",
  "Water Reminder",
  "Doctor Alert Sent",
  "Time to Rest",
  "Nurse Assistance Needed",
  "Emergency Alert Triggered"
};

// ESP-NOW callback
void onReceive(const esp_now_recv_info_t *info, const uint8_t *incomingData, int len) {
  Serial.println("ESP-NOW Message Received.");
  if (len == 1 && incomingData[0] == 1) {
    digitalWrite(VIBRATION_PIN, HIGH);
    delay(10000);
    digitalWrite(VIBRATION_PIN, LOW);
  }
}

// HTML Page (keeping your existing function)
String SendHTML(float BPM, float SpO2, float temperatureC_raw, String lastMessage) {
  float temperatureC = temperatureC_raw + 1;

  bool bpmOK = BPM >= 60 && BPM <= 120;
  bool spo2OK = SpO2 >= 95;
  bool tempOK = temperatureC >= 35 && temperatureC <= 38;

  String outOfRange = "";

  if (!bpmOK && BPM > 0) outOfRange += "Heart Rate, ";  // Only show alert if we have a reading
  if (!spo2OK && SpO2 > 0) outOfRange += "SpO₂, ";     // Only show alert if we have a reading
  if (!tempOK) outOfRange += "Temperature, ";

  if (outOfRange.endsWith(", ")) {
    outOfRange = outOfRange.substring(0, outOfRange.length() - 2);
  }

  String html = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
  <meta charset="UTF-8">
  <title>Health Monitor</title>
  <meta name="viewport" content="width=device-width, initial-scale=1.0">
  <meta http-equiv="refresh" content="3">
  <style>
    body{font-family:Arial;background:#f0f0f0;margin:0;padding:20px;}
    .card{background:white;padding:20px;border-radius:8px;box-shadow:0 2px 4px rgba(0,0,0,0.1);max-width:400px;margin:0 auto;}
    h1{color:#008080;margin-bottom:15px;text-align:center;}
    p{font-size:18px;margin:8px 0;}
    .alert{color:white;background:#e74c3c;padding:8px;border-radius:4px;margin-top:10px;font-weight:bold;}
    .status{color:#27ae60;font-weight:bold;}
  </style>
</head>
<body>
  <div class="card">
    <h1>Health Monitor</h1>
)rawliteral";

  if (sensorReady) {
    if (BPM > 0) {
      html += "<p><b>Heart Rate:</b> " + String((int)BPM) + " BPM</p>";
    } else {
      html += "<p><b>Heart Rate:</b> <span style='color:orange;'>Reading...</span></p>";
    }
    
    if (SpO2 > 0) {
      html += "<p><b>SpO₂:</b> " + String((int)SpO2) + " %</p>";
    } else {
      html += "<p><b>SpO₂:</b> <span style='color:orange;'>Reading...</span></p>";
    }
  } else {
    html += "<p style='color:red;'>MAX30102 Not Detected</p>";
  }

  html += "<p><b>Temperature:</b> " + String(temperatureC, 1) + " °C</p>";

  if (outOfRange.length() > 0) {
    html += "<div class='alert'>⚠️ Alert: " + outOfRange + " out of range!</div>";
  } else if (sensorReady && BPM > 0 && SpO2 > 0) {
    html += "<p class='status'>✓ All readings normal</p>";
  }

  html += "<p><b>Last Message:</b> " + lastMessage + "</p>";
  html += "<p><small>Auto-refresh every 3 seconds</small></p>";
  html += "</div></body></html>";

  return html;
}

void updateDisplay() {
  display.clearDisplay();
  display.setTextColor(SH110X_WHITE);
  
  display.setCursor(0, 0);
  display.setTextSize(1);
  display.print("HYGEIA");
  
  display.drawBitmap(42, 0, heart_icon, 6, 6, SH110X_WHITE);
  
  display.setCursor(120, 0);
  if (WiFi.softAPgetStationNum() > 0) {
    display.print("*");
  } else {
    display.print("o");
  }
  
  display.drawFastHLine(0, 9, SCREEN_WIDTH, SH110X_WHITE);
  
  bool bpmOK = (BPM >= 60 && BPM <= 120) || BPM == 0;  // Don't show error if no reading yet
  bool spo2OK = (SpO2 >= 95) || SpO2 == 0;             // Don't show error if no reading yet
  float adjTemp = temperatureC;
  bool tempOK = adjTemp >= 35 && adjTemp <= 38;
  
  display.setCursor(0, 12);
  display.drawBitmap(0, 12, pulse_icon, 6, 6, SH110X_WHITE);
  display.setCursor(8, 12);
  if (sensorReady && BPM > 0) {
    display.print(String((int)BPM));
  } else {
    display.print("--");
  }
  
  display.setCursor(45, 12);
  display.drawBitmap(45, 12, lungs_icon, 6, 6, SH110X_WHITE);
  display.setCursor(53, 12);
  if (sensorReady && SpO2 > 0) {
    display.print(String((int)SpO2) + "%");
  } else {
    display.print("--%");
  }
  
  display.setCursor(85, 12);
  display.drawBitmap(85, 12, temp_icon, 6, 6, SH110X_WHITE);
  display.setCursor(93, 12);
  display.print(String(adjTemp, 1) + "C");
  
  display.drawFastHLine(0, 22, SCREEN_WIDTH, SH110X_WHITE);
  
  display.setCursor(0, 25);
  if ((!bpmOK && BPM > 0) || (!spo2OK && SpO2 > 0) || !tempOK) {
    display.print("ALERT: CHECK VITALS");
  } else if (sensorReady && BPM > 0 && SpO2 > 0) {
    display.print("STATUS: NORMAL");
  } else {
    display.print("READING SENSORS...");
  }
  
  display.drawFastHLine(0, 35, SCREEN_WIDTH, SH110X_WHITE);
  
  display.setCursor(0, 38);
  display.setTextSize(1);
  display.print("MSG:");
  display.setCursor(0, 48);
  if (lastMessage != "No message yet" && lastMessage.length() > 0) {
    String shortMsg = lastMessage;
    if (shortMsg.length() > 21) {
      shortMsg = shortMsg.substring(0, 18) + "...";
    }
    display.print(shortMsg);
  } else {
    display.print("No recent messages");
  }
  
  display.display();
}

void showSplashScreen() {
  display.clearDisplay();
  display.setTextColor(SH110X_WHITE);
  
  display.setCursor(20, 10);
  display.setTextSize(2);
  display.print("HYGEIA");
  
  display.setCursor(10, 30);
  display.setTextSize(1);
  display.print("Health Monitor");
  
  display.setCursor(25, 45);
  display.setTextSize(1);
  display.print("Starting...");
  
  display.display();
  delay(2000);
}

void checkButtons() {
  for (int i = 0; i < 8; i++) {
    bool currentState = digitalRead(buttonPins[i]);
    if (currentState == LOW && lastButtonState[i] == HIGH && (millis() - lastDebounceTime[i] > debounceDelay)) {
      lastDebounceTime[i] = millis();
      lastMessage = buttonMessages[i];
      Serial.print("Button "); Serial.print(i + 1); Serial.print(": ");
      Serial.println(lastMessage);

      // Start LED blink
      digitalWrite(LED_PIN, HIGH);
      ledBlinking = true;
      ledBlinkStart = millis();
    }
    lastButtonState[i] = currentState;
  }
}

void handle_OnConnect() {
  server.send(200, "text/html", SendHTML(BPM, SpO2, temperatureC, lastMessage));
}

void handle_NotFound() {
  server.send(404, "text/plain", "Not found");
}

// FIXED: Proper sensor reading function
void readSensorData() {
  if (!sensorReady) return;
  
  // Check if data is available
  if (!particleSensor.available()) return;
  
  // Read new sample
  uint32_t irValue = particleSensor.getIR();
  uint32_t redValue = particleSensor.getRed();
  particleSensor.nextSample();
  
  // Store in buffer
  irBuffer[bufferIndex] = irValue;
  redBuffer[bufferIndex] = redValue;
  bufferIndex++;
  
  // Check if buffer is full
  if (bufferIndex >= MAX_SPO2_SAMPLES) {
    bufferIndex = 0;
    bufferFilled = true;
  }
  
  // Only calculate when buffer is filled
  if (bufferFilled) {
    // Check for finger detection (higher threshold)
    uint32_t avgIR = 0;
    for (int i = 0; i < MAX_SPO2_SAMPLES; i++) {
      avgIR += irBuffer[i];
    }
    avgIR /= MAX_SPO2_SAMPLES;
    
    if (avgIR < 50000) {
      // No finger detected
      BPM = 0;
      SpO2 = 0;
      Serial.println("No finger detected");
    } else {
      // Calculate SpO2 and heart rate
      maxim_heart_rate_and_oxygen_saturation(irBuffer, MAX_SPO2_SAMPLES, redBuffer, &spo2, &validSPO2, &beatsPerMinute, &beatAvg);
      
      // Update SpO2 if valid
      if (validSPO2 == 1 && spo2 > 0 && spo2 <= 100) {
        SpO2 = spo2;
        Serial.print("SpO2: "); Serial.println(SpO2);
      }
      
      // Update heart rate with averaging
      if (beatAvg == 1 && beatsPerMinute > 0 && beatsPerMinute < 200) {
        rates[rateSpot++] = (byte)beatsPerMinute;
        rateSpot %= RATE_SIZE;
        
        // Calculate average
        long total = 0;
        for (byte i = 0; i < RATE_SIZE; i++) {
          total += rates[i];
        }
        BPM = total / RATE_SIZE;
        Serial.print("BPM: "); Serial.println(BPM);
      }
    }
  }
}

void setup() {
  Serial.begin(115200);
  pinMode(VIBRATION_PIN, OUTPUT);
  digitalWrite(VIBRATION_PIN, LOW);
  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, LOW);

  Wire.begin();
  sensors.begin();

  // Initialize OLED
  if (!display.begin(i2c_Address, true)) {
    Serial.println(F("SH1106 allocation failed"));
    for (;;);
  }
  showSplashScreen();

  // MAX30102 Init with better settings
  Serial.println("Initializing MAX30102...");
  if (particleSensor.begin(Wire, I2C_SPEED_FAST) == false) {
    Serial.println("MAX30102 was not found. Please check wiring/power.");
    sensorReady = false;
  } else {
    Serial.println("MAX30102 found");
    sensorReady = true;
    
    // FIXED: Better sensor configuration
    particleSensor.setup(); // Use default settings first
    
    // Then customize
    particleSensor.setPulseAmplitudeRed(0x0A);    // Turn Red LED to low to indicate sensor is running
    particleSensor.setPulseAmplitudeGreen(0);     // Turn off Green LED
    particleSensor.setPulseAmplitudeIR(0x0A);     // Turn IR LED to low
    
    // Initialize arrays
    for (int i = 0; i < MAX_SPO2_SAMPLES; i++) {
      irBuffer[i] = 0;
      redBuffer[i] = 0;
    }
    
    for (int i = 0; i < RATE_SIZE; i++) {
      rates[i] = 0;
    }
    
    Serial.println("MAX30102 configured successfully");
  }

  // Button pins
  for (int i = 0; i < 8; i++) {
    pinMode(buttonPins[i], INPUT_PULLUP);
  }

  // Wi-Fi Access Point
  WiFi.mode(WIFI_AP);
  WiFi.softAP(ssid);
  Serial.print("AP IP: ");
  Serial.println(WiFi.softAPIP());

  server.on("/", handle_OnConnect);
  server.onNotFound(handle_NotFound);
  server.begin();

  // Show MAC
  uint8_t mac[6];
  esp_wifi_get_mac(WIFI_IF_AP, mac);
  Serial.print("Receiver MAC: ");
  for (int i = 0; i < 6; i++) {
    if (mac[i] < 16) Serial.print("0");
    Serial.print(mac[i], HEX);
    if (i < 5) Serial.print(":");
  }
  Serial.println();

  // ESP-NOW Setup
  if (esp_now_init() != ESP_OK) {
    Serial.println("ESP-NOW INIT FAILED");
    while (true) delay(1000);
  }
  esp_now_register_recv_cb(onReceive);
  
  Serial.println("Setup complete. Waiting for finger placement...");
}

void loop() {
  server.handleClient();
  
  // FIXED: Use continuous non-blocking sensor reading
  if (sensorReady) {
    // Check if data is available
    if (particleSensor.available()) {
      // Read new sample
      uint32_t irValue = particleSensor.getIR();
      uint32_t redValue = particleSensor.getRed();
      particleSensor.nextSample();
      
      // Store in buffer (using a static variable to track position)
      static uint16_t bufferIndex = 0;
      static bool bufferFilled = false;
      
      irBuffer[bufferIndex] = irValue;
      redBuffer[bufferIndex] = redValue;
      bufferIndex++;
      
      // Check if buffer is full
      if (bufferIndex >= MAX_SPO2_SAMPLES) {
        bufferIndex = 0;
        bufferFilled = true;
      }
      
      // Only calculate when buffer is filled
      if (bufferFilled) {
        // Check for finger detection
        if (irValue < 50000) {
          // No finger detected
          BPM = 0;
          SpO2 = 0;
        } else {
          // Calculate SpO2 and heart rate
          maxim_heart_rate_and_oxygen_saturation(irBuffer, MAX_SPO2_SAMPLES, redBuffer, &spo2, &validSPO2, &beatsPerMinute, &beatAvg);
          
          // Update SpO2 if valid
          if (validSPO2 == 1 && spo2 > 0 && spo2 <= 100) {
            SpO2 = spo2;
          }
          
          // Update heart rate with averaging
          if (beatAvg == 1 && beatsPerMinute > 0 && beatsPerMinute < 200) {
            rates[rateSpot++] = (byte)beatsPerMinute;
            rateSpot %= RATE_SIZE;
            
            // Calculate average
            long total = 0;
            for (byte i = 0; i < RATE_SIZE; i++) {
              total += rates[i];
            }
            BPM = total / RATE_SIZE;
          }
        }
      }
    }
  }

  static bool alertActive = false;

  if (millis() - tsLastReport > REPORTING_PERIOD_MS) {
    sensors.requestTemperatures();
    temperatureC = sensors.getTempCByIndex(0);
    
    // Check for alerts
    float adjTemp = temperatureC + 1; // +1 adjustment
    bool bpmOK = (BPM >= 60 && BPM <= 120) || BPM == 0;  // Don't alert if no reading
    bool spo2OK = (SpO2 >= 95) || SpO2 == 0;             // Don't alert if no reading
    bool tempOK = adjTemp >= 35 && adjTemp <= 38;

    tsLastReport = millis();

    alertActive = !((bpmOK || BPM == 0) && (spo2OK || SpO2 == 0) && tempOK);

    // Update the OLED display with new values
    updateDisplay();

    // Enhanced but compact Serial Monitor output
    static unsigned long lastSerialUpdate = 0;
    if (millis() - lastSerialUpdate > 3000) { // Every 3 seconds
      Serial.println("=== HYGEIA ===");
      
      Serial.print("BPM: ");
      if (!bpmOK && BPM > 0) Serial.print("[!] ");
      Serial.print(sensorReady ? String((int)BPM) : "N/A");
      
      Serial.print(" | SpO2: ");
      if (!spo2OK && SpO2 > 0) Serial.print("[!] ");
      Serial.print(sensorReady ? String((int)SpO2) + "%" : "N/A");
      
      Serial.print(" | Temp: ");
      if (!tempOK) Serial.print("[!] ");
      Serial.println(String(adjTemp, 1) + "°C");
      
      Serial.print("IR Value: "); Serial.println(particleSensor.getIR());
      
      if (alertActive) {
        Serial.println("*** ALERT: Parameters out of range! ***");
      }
      Serial.println("==============");
      lastSerialUpdate = millis();
    }
  }

  // Check button presses
  for (int i = 0; i < 8; i++) {
    bool currentState = digitalRead(buttonPins[i]);
    if (currentState == LOW && lastButtonState[i] == HIGH && (millis() - lastDebounceTime[i] > debounceDelay)) {
      lastDebounceTime[i] = millis();
      lastMessage = buttonMessages[i];
      Serial.print("Button "); Serial.print(i + 1); Serial.print(": ");
      Serial.println(lastMessage);

      // Start LED blink
      digitalWrite(LED_PIN, HIGH);
      ledBlinking = true;
      ledBlinkStart = millis();
    }
    lastButtonState[i] = currentState;
  }

  // Also trigger LED if alert is active (but don't restart timer if already blinking)
  if (alertActive && !ledBlinking) {
    digitalWrite(LED_PIN, HIGH);
    ledBlinking = true;
    ledBlinkStart = millis();
  }

  // Turn off LED after duration
  if (ledBlinking && millis() - ledBlinkStart >= ledBlinkDuration) {
    digitalWrite(LED_PIN, LOW);
    ledBlinking = false;
  }

  // Small delay to prevent overwhelming the sensor
  delay(25);
}