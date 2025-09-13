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
const byte RATE_SIZE = 4; //Increase this for more averaging. 4 is good.
byte rates[RATE_SIZE]; //Array of heart rates
byte rateSpot = 0;
long lastBeat = 0; //Time at which the last beat occurred
int32_t beatsPerMinute; // Raw BPM from the algorithm
int8_t beatAvg;         // Validity flag for heart rate

#define MAX_SPO2_SAMPLES 100
uint32_t irBuffer[MAX_SPO2_SAMPLES]; //infrared LED sensor data
uint32_t redBuffer[MAX_SPO2_SAMPLES];  //red LED sensor data
int32_t bufferLength; //data length
int32_t spo2; //SPO2 value
int8_t validSPO2; //indicator to show if the SPO2 calculation is valid

float BPM = 0, SpO2 = 0, temperatureC = 0.0;
uint32_t tsLastReport = 0;
bool sensorReady = false;

// Add these global variables at the top of your code (after the existing globals)
unsigned long lastAnimationUpdate = 0;
const unsigned long animationInterval = 500; // 500ms animation cycle
bool animationState = false;
int scrollOffset = 0;
unsigned long lastScrollUpdate = 0;
const unsigned long scrollInterval = 150;

// Smaller custom bitmap icons for 1.3" OLED (6x6 pixels each)
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

// HTML Page
String SendHTML(float BPM, float SpO2, float temperatureC_raw) {
  float temperatureC = temperatureC_raw + 1; // +1 adjustment

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

void updateDisplay() {
  display.clearDisplay();
  display.setTextColor(SH110X_WHITE);
  
  // Update animation state
  if (millis() - lastAnimationUpdate >= animationInterval) {
    animationState = !animationState;
    lastAnimationUpdate = millis();
  }
  
  // Compact header with smaller title
  display.setCursor(0, 0);
  display.setTextSize(1);
  display.print("HYGEIA");
  
  // Animate the heart icon 
  if (animationState && sensorReady && BPM > 0) {
    display.drawBitmap(40, 0, heart_beat_icon, 6, 6, SH110X_WHITE);
  } else {
    display.drawBitmap(40, 0, heart_icon, 6, 6, SH110X_WHITE);
  }
  
  // WiFi status indicator
  display.setCursor(120, 0);
  if (WiFi.softAPgetStationNum() > 0) {
    display.print("*"); // Connected
  } else {
    if (animationState) display.print("o"); // Blinking when disconnected
  }
  
  // Separator line
  display.drawFastHLine(0, 10, SCREEN_WIDTH, SH110X_WHITE);
  
  // Row 1: BPM (Left side)
  bool bpmOK = (BPM >= 60 && BPM <= 120) || !sensorReady;
  display.setCursor(0, 14);
  
  // Warning icon for BPM
  if (!bpmOK && sensorReady && animationState) {
    display.drawBitmap(0, 14, warning_icon, 6, 6, SH110X_WHITE);
    display.setCursor(8, 14);
  }
  
  display.drawBitmap((!bpmOK && sensorReady && animationState) ? 8 : 0, 14, pulse_icon, 6, 6, SH110X_WHITE);
  display.setCursor((!bpmOK && sensorReady && animationState) ? 16 : 8, 14);
  display.print("BPM");
  
  display.setCursor((!bpmOK && sensorReady && animationState) ? 16 : 8, 24);
  if (sensorReady) {
    display.setTextSize(1);
    display.print(String((int)BPM));
  } else {
    display.print("---");
  }
  
  // Row 1: SpO2 (Right side)
  bool spo2OK = (SpO2 >= 95) || !sensorReady;
  display.setCursor(70, 14);
  
  // Warning icon for SpO2
  if (!spo2OK && sensorReady && animationState) {
    display.drawBitmap(70, 14, warning_icon, 6, 6, SH110X_WHITE);
    display.setCursor(78, 14);
  }
  
  display.drawBitmap((!spo2OK && sensorReady && animationState) ? 78 : 70, 14, lungs_icon, 6, 6, SH110X_WHITE);
  display.setCursor((!spo2OK && sensorReady && animationState) ? 86 : 78, 14);
  display.print("SpO2");
  
  display.setCursor((!spo2OK && sensorReady && animationState) ? 86 : 78, 24);
  if (sensorReady) {
    display.setTextSize(1);
    display.print(String((int)SpO2) + "%");
  } else {
    display.print("---%");
  }
  
  // Separator line
  display.drawFastHLine(0, 35, SCREEN_WIDTH, SH110X_WHITE);
  
  // Row 2: Temperature (Full width)
  float adjTemp = temperatureC + 1; // +1 adjustment
  bool tempOK = adjTemp >= 35 && adjTemp <= 38;
  display.setCursor(0, 39);
  
  // Warning icon for Temperature
  if (!tempOK && animationState) {
    display.drawBitmap(0, 39, warning_icon, 6, 6, SH110X_WHITE);
    display.setCursor(8, 39);
  }
  
  display.drawBitmap(!tempOK && animationState ? 8 : 0, 39, temp_icon, 6, 6, SH110X_WHITE);
  display.setCursor(!tempOK && animationState ? 16 : 8, 39);
  display.print("TEMP: ");
  display.setTextSize(2);
  display.print(String(adjTemp, 1));
  display.setTextSize(1);
  display.print("C");
  
  // Status bar at bottom
  display.setCursor(0, 55);
  display.setTextSize(1);
  
  if ((!bpmOK && sensorReady) || (!spo2OK && sensorReady) || !tempOK) {
    // Scrolling alert
    if (millis() - lastScrollUpdate >= scrollInterval) {
      scrollOffset++;
      if (scrollOffset > 150) scrollOffset = -30;
      lastScrollUpdate = millis();
    }
    
    display.print("ALERT: Check vitals!");
  } else {
    display.print("Status: Normal");
    
    // Show last message if available
    if (lastMessage != "No message yet") {
      display.setCursor(0, 58);
      display.setTextSize(1);
      // Truncate message if too long
      String shortMsg = lastMessage;
      if (shortMsg.length() > 21) {
        shortMsg = shortMsg.substring(0, 18) + "...";
      }
      display.print(shortMsg);
    }
  }
  
  display.display();
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

  // Initialize OLED
  if (!display.begin(i2c_Address, true)) { // Pass I2C address here
    Serial.println(F("SH1106 allocation failed"));
    for (;;); // Don't proceed, loop forever
  }
  display.clearDisplay();
  display.println("Health Monitor");
  display.display();

  // MAX30100 Init
  Serial.println("Initializing MAX30102...");
  // Initialize sensor
  if (particleSensor.begin(Wire, I2C_SPEED_FAST) == false)
  {
    Serial.println("MAX30102 was not found. Please check wiring/power.");
    sensorReady = false;
  } else {
    Serial.println("MAX30102 SUCCESS");
    sensorReady = true;
    // Recommended settings for MAX30102
    byte ledBrightness = 60; //Options: 0=Off to 255=50mA
    byte sampleAverage = 4; //Options: 1, 2, 4, 8, 16, 32
    byte ledMode = 2; //Options: 1 = Red only, 2 = Red + IR, 3 = Red + IR + Green
    int sampleRate = 100; //Options: 50, 100, 200, 400, 800, 1000, 1600, 3200
    int pulseWidth = 411; //Options: 69, 118, 215, 411
    int adcRange = 4096; //Options: 2048, 4096, 8192, 16384
    particleSensor.setup(ledBrightness, sampleAverage, ledMode, sampleRate, pulseWidth, adcRange);
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
    bufferLength = MAX_SPO2_SAMPLES; //reset the buffer length to start collecting new data

    //read the first 100 samples, and determine the signal range
    for (byte i = 0 ; i < bufferLength ; i++)
    {
      while (particleSensor.available() == false) //do we have new data?
        particleSensor.check(); //Check the sensor for new data

      redBuffer[i] = particleSensor.getFIFORed();
      irBuffer[i] = particleSensor.getFIFOIR();
      particleSensor.nextSample(); //We're finished with this sample so move to next sample
    }
    
    // Check if a finger is present. If the IR reading is very low, it means no finger is detected.
    if (particleSensor.getIR() < 50000) {
      BPM = 0;
      SpO2 = 0;
      // Skip the rest of the sensor logic for this loop iteration
    } else {

    // Calculate heart rate and SpO2 after collecting the samples
    maxim_heart_rate_and_oxygen_saturation(irBuffer, bufferLength, redBuffer, &spo2, &validSPO2, &beatsPerMinute, &beatAvg);

    // Only update our global variables if the algorithm returns valid data
    if (validSPO2 > 0) {
      SpO2 = spo2;
    }

    if (beatAvg > 0) { // The algorithm returns a non-zero value for a valid HR
      // Add the new valid reading to our circular buffer
      rates[rateSpot++] = (byte)beatsPerMinute;
      rateSpot %= RATE_SIZE; // Wrap around the buffer

      // Calculate the average of the readings in the buffer
      int total = 0;
      for (int i = 0; i < RATE_SIZE; i++) {
        total += rates[i];
      }
      // Update the global BPM with the smoothed average
      BPM = total / RATE_SIZE;
    }
    }
  }

  static bool alertActive = false;

  if (millis() - tsLastReport > REPORTING_PERIOD_MS) {
    sensors.requestTemperatures();
    temperatureC = sensors.getTempCByIndex(0);
    
    // Check for alerts
    float adjTemp = temperatureC + 1; // +1 adjustment
    bool bpmOK = BPM >= 60 && BPM <= 120;
    bool spo2OK = SpO2 >= 95;
    bool tempOK = adjTemp >= 35 && adjTemp <= 38;

    tsLastReport = millis();

    alertActive = !(bpmOK && spo2OK && tempOK);  // Alert if any value is not okay

    // Update the OLED display with new values
    updateDisplay();

    // Enhanced but compact Serial Monitor output
    static unsigned long lastSerialUpdate = 0;
    if (millis() - lastSerialUpdate > 1000) { // Every 3 seconds
      Serial.println("=== HYGEIA ===");
      
      Serial.print("BPM: ");
      if (!bpmOK && sensorReady) Serial.print("[!] ");
      Serial.print(sensorReady ? String((int)BPM) : "N/A");
      
      Serial.print(" | SpO2: ");
      if (!spo2OK && sensorReady) Serial.print("[!] ");
      Serial.print(sensorReady ? String((int)SpO2) + "%" : "N/A");
      
      Serial.print(" | Temp: ");
      if (!tempOK) Serial.print("[!] ");
      Serial.println(String(adjTemp, 1) + "°C");
      
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

}
