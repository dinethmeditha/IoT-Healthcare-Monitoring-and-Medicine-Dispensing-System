#include <WiFi.h>
#include <WebServer.h>
#include <esp_now.h>
#include <esp_wifi.h>
#include <Wire.h>
#include "MAX30100_PulseOximeter.h"
#include <OneWire.h>
#include <DallasTemperature.h>

#define VIBRATION_PIN 5
#define ONE_WIRE_BUS 4  // DS18B20 Data pin
#define REPORTING_PERIOD_MS 1000
#define LED_PIN 2  // LED to blink on button press

float BPM = 0, SpO2 = 0, temperatureC = 0.0;
uint32_t tsLastReport = 0;
bool sensorReady = false;

// LED blink control
bool ledBlinking = false;
unsigned long ledBlinkStart = 0;
const unsigned long ledBlinkDuration = 2000;

// Web server
WebServer server(80);
const char* ssid = "Health Monitor";

// MAX30100 sensor
PulseOximeter pox;

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

void onBeatDetected() {
  Serial.println("Beat Detected!");
}

// HTML Page
String SendHTML(float BPM, float SpO2, float temperatureC_raw) {
  float temperatureC = temperatureC_raw + 3; // +3 adjustment

  bool bpmOK = BPM >= 60 && BPM <= 120;
  bool spo2OK = SpO2 >= 95;
  bool tempOK = temperatureC >= 35 && temperatureC <= 38;

  String outOfRange = "";

  if (!bpmOK) outOfRange += "Heart Rate, ";
  if (!spo2OK) outOfRange += "SpO₂, ";
  if (!tempOK) outOfRange += "Temperature, ";

  // Trim last comma if needed
  if (outOfRange.endsWith(", ")) {
    outOfRange = outOfRange.substring(0, outOfRange.length() - 2);
  }

  String html = R"rawliteral(
<!DOCTYPE html>
<html lang="en">
<head>
  <meta charset="UTF-8">
  <title>Health Monitor</title>
  <meta name="viewport" content="width=device-width, initial-scale=1.0">
  <style>
    body {
      font-family: 'Segoe UI', Tahoma, Geneva, Verdana, sans-serif;
      background: #f2f2f2;
      margin: 0;
      padding: 0;
      display: flex;
      justify-content: center;
      align-items: center;
      height: 100vh;
    }
    .card {
      background: #ffffff;
      padding: 30px 40px;
      border-radius: 16px;
      box-shadow: 0 8px 16px rgba(0, 0, 0, 0.15);
      text-align: center;
      max-width: 400px;
      width: 90%;
    }
    h1 {
      color: #008080;
      margin-bottom: 20px;
    }
    p {
      font-size: 20px;
      margin: 10px 0 5px;
    }
    meter {
      width: 100%;
      height: 20px;
      margin-bottom: 15px;
    }
    .error {
      color: red;
      font-weight: bold;
      margin-top: 10px;
    }
    .alert-box {
      color: white;
      background-color: #e74c3c;
      padding: 10px;
      border-radius: 8px;
      margin-top: 15px;
      font-weight: bold;
    }
    .footer {
      margin-top: 20px;
      font-size: 14px;
      color: #888;
    }
  </style>
  <script>
    setInterval(() => {
      location.reload();
    }, 2000);
  </script>
</head>
<body>
  <div class="card">
    <h1>Health Monitor</h1>
)rawliteral";

  if (sensorReady) {
    html += "<p><b>Heart Rate:</b> " + String((int)BPM) + " BPM</p>";
    html += "<meter min='40' max='180' low='60' high='120' optimum='75' value='" + String((int)BPM) + "'></meter>";

    html += "<p><b>SpO₂:</b> " + String((int)SpO2) + " %</p>";
    html += "<meter min='70' max='100' low='90' high='95' optimum='98' value='" + String((int)SpO2) + "'></meter>";
  } else {
    html += "<p class='error'>MAX30100 Not Detected</p>";
  }

  html += "<p><b>Temperature:</b> " + String(temperatureC, 1) + " °C</p>";
  html += "<meter min='30' max='45' low='35' high='38' optimum='36.5' value='" + String(temperatureC, 1) + "'></meter>";

  if (!bpmOK || !spo2OK || !tempOK) {
    html += "<div class='alert-box'>⚠️ Alert: " + outOfRange + " out of safe range!</div>";
  }

  html += "<p><b>Last Message:</b> " + lastMessage + "</p>";
  html += "<div class='footer'>Auto-refresh every 2 seconds</div>";
  html += "</div></body></html>";

  return html;
}


void handle_OnConnect() {
  server.send(200, "text/html", SendHTML(BPM, SpO2, temperatureC));
}

void handle_NotFound() {
  server.send(404, "text/plain", "Not found");
}

void setup() {
  Serial.begin(115200);
  pinMode(VIBRATION_PIN, OUTPUT);
  digitalWrite(VIBRATION_PIN, LOW);

  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, LOW);

  Wire.begin();
  sensors.begin();

  // MAX30100 Init
  Serial.println("Initializing MAX30100...");
  if (!pox.begin()) {
    Serial.println("MAX30100 FAILED - Check wiring!");
    sensorReady = false;
  } else {
    Serial.println("MAX30100 SUCCESS");
    sensorReady = true;
    pox.setOnBeatDetectedCallback(onBeatDetected);
    pox.setIRLedCurrent(MAX30100_LED_CURR_7_6MA);
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
}

void loop() {
  server.handleClient();

  if (sensorReady) {
    pox.update();
    BPM = pox.getHeartRate();
    SpO2 = pox.getSpO2();
  }

  static bool alertActive = false;

  if (millis() - tsLastReport > REPORTING_PERIOD_MS) {
    sensors.requestTemperatures();
    temperatureC = sensors.getTempCByIndex(0);

    Serial.print("BPM: "); Serial.println(BPM);
    Serial.print("SpO₂: "); Serial.println(SpO2);
    Serial.print("Temp: "); Serial.println(temperatureC);
    Serial.println("----------");

    tsLastReport = millis();

    // Check for alerts
    float adjTemp = temperatureC + 3; // +3 adjustment
    bool bpmOK = BPM >= 60 && BPM <= 120;
    bool spo2OK = SpO2 >= 95;
    bool tempOK = adjTemp >= 35 && adjTemp <= 38;

    alertActive = !(bpmOK && spo2OK && tempOK);  // Alert if any value is not okay
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

}
