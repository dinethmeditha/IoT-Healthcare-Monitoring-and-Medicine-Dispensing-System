# IoT Healthcare Monitoring and Medicine Dispensing System

## 🏥 Project Overview

This project implements a comprehensive IoT-based healthcare monitoring and automated medicine dispensing system designed for elderly care and patient assistance. The system consists of two main units communicating via ESP-NOW protocol:

1. **Medicine Dispensing Unit (Sender)** - Automated medicine scheduling and dispensing
2. **Health Monitoring Unit (Receiver)** - Vital signs monitoring with wearable glove interface

## ✨ Features

### Medicine Dispensing Unit
- ⏰ **Automated Scheduling**: Set up to 24 different medicine times
- 🔄 **Stepper Motor Control**: Precise medicine dispensing mechanism
- 📱 **Web Interface**: User-friendly scheduling interface
- ⏱️ **Real-time Stopwatch**: Track medication timing
- 🔔 **Visual Indicators**: LED feedback for operations
- 📡 **Wireless Communication**: ESP-NOW for reliable communication

### Health Monitoring Unit
- ❤️ **Heart Rate Monitoring**: Real-time BPM measurement using MAX30100
- 🫁 **SpO₂ Detection**: Blood oxygen saturation monitoring
- 🌡️ **Temperature Sensing**: Body temperature measurement with DS18B20
- 📱 **Emergency Buttons**: 8 programmable emergency switches in wearable glove
- 🚨 **Alert System**: Visual and haptic feedback for health alerts
- 💊 **Vibration Alerts**: Medicine reminder notifications
- 📊 **Web Dashboard**: Real-time health data visualization

## 🛠️ Hardware Components

### Medicine Dispensing Unit (ESP32-1)
- ESP32 Development Board
- Stepper Motor with Driver
- LED indicators
- Power Supply (12V)

### Health Monitoring Unit (ESP32-2)
- ESP32 Development Board
- MAX30100 Pulse Oximeter Sensor
- DS18B20 Temperature Sensor
- RB0221 Micro Vibrating Motor
- 8 Push Button Switches (Emergency buttons)
- LED indicator
- Wearable Glove Assembly

## 📋 Pin Configuration

### Medicine Dispensing Unit
```cpp
#define STEP_PIN 19        // Stepper motor step pin
#define DIR_PIN 18         // Stepper motor direction pin
const int ledPin = 21;     // Status LED
const int msgLedPin = 13;  // Message indicator LED
```

### Health Monitoring Unit
```cpp
#define VIBRATION_PIN 5    // Vibrating motor control
#define ONE_WIRE_BUS 4     // DS18B20 temperature sensor
#define LED_PIN 2          // Status LED
// Button pins: 13, 12, 14, 27, 26, 25, 33, 32
```

## 🔧 Circuit Diagram

The system uses two ESP32 microcontrollers connected as shown in the hardware implementation image. Key connections:

- **MAX30100**: Connected via I2C (SDA/SCL)
- **DS18B20**: OneWire protocol on pin 4
- **Stepper Motor**: Controlled via driver connected to pins 18 & 19
- **Emergency Buttons**: Connected to digital pins with pull-up resistors
- **Vibrating Motor**: PWM controlled on pin 5

## 💻 Software Architecture

### Medicine Dispensing Unit Code Explanation

```cpp
// Wi-Fi Access Point Setup
const char* ssid = "Time Scheduler";
const char* password = "12345678";
```
Creates a Wi-Fi hotspot for web interface access.

```cpp
// Scheduling Arrays
bool scheduled[24] = {false};    // Track scheduled medicines
bool triggered[24] = {false};    // Track completed doses
bool expired[24] = {false};      // Track expired schedules
time_t triggerTimes[24] = {0};   // Store trigger timestamps
```
Manages up to 24 different medicine schedules.

```cpp
void checkAndRunSchedule() {
  time_t now;
  time(&now);
  
  for (int i = 0; i < 24; i++) {
    if (scheduled[i] && !triggered[i]) {
      if (now >= triggerTimes[i]) {
        isRunning[i] = true;
        runStepperLoop();        // Dispense medicine
        triggered[i] = true;     // Mark as completed
        isRunning[i] = false;
      }
    }
  }
}
```
Core scheduling logic that checks current time against scheduled medicine times.

```cpp
void runStepperLoop() {
  // Send vibration signal to health monitor
  uint8_t msg[] = {1};
  esp_now_send(receiverMAC, msg, sizeof(msg));
  
  // Run stepper motor for precise dispensing
  for (long i = 0; i < totalSteps; i++) {
    digitalWrite(STEP_PIN, HIGH);
    delayMicroseconds(delayPerStep);
    digitalWrite(STEP_PIN, LOW);
    delayMicroseconds(delayPerStep);
  }
  
  // Visual confirmation
  digitalWrite(ledPin, HIGH);
  delay(3000);
  digitalWrite(ledPin, LOW);
}
```
Medicine dispensing mechanism with wireless notification.

### Health Monitoring Unit Code Explanation

```cpp
// Sensor Initialization
PulseOximeter pox;              // MAX30100 sensor
OneWire oneWire(ONE_WIRE_BUS);
DallasTemperature sensors(&oneWire);
```
Initialize health monitoring sensors.

```cpp
void loop() {
  // Continuous sensor reading
  if (sensorReady) {
    pox.update();
    BPM = pox.getHeartRate();    // Get heart rate
    SpO2 = pox.getSpO2();        // Get oxygen saturation
  }
  
  // Temperature reading
  sensors.requestTemperatures();
  temperatureC = sensors.getTempCByIndex(0);
  
  // Health alert system
  bool bpmOK = BPM >= 60 && BPM <= 120;
  bool spo2OK = SpO2 >= 95;
  bool tempOK = adjTemp >= 35 && adjTemp <= 38;
  
  alertActive = !(bpmOK && spo2OK && tempOK);
}
```
Continuous health monitoring with alert generation.

```cpp
// Emergency button handling
for (int i = 0; i < 8; i++) {
  bool currentState = digitalRead(buttonPins[i]);
  if (currentState == LOW && lastButtonState[i] == HIGH) {
    lastMessage = buttonMessages[i];  // Update last message
    digitalWrite(LED_PIN, HIGH);      // Visual feedback
    ledBlinking = true;
  }
}
```
Eight emergency buttons for different patient needs.

## 🌐 Web Interface Features

### Medicine Scheduler Interface
- **Stopwatch Display**: Shows elapsed time for medicine schedule
- **Manual Trigger**: Emergency medicine dispensing
- **24-Hour Scheduling**: Set multiple medicine times
- **Status Indicators**: Real-time status for each scheduled medicine
- **Auto-refresh**: Updates every 30 seconds

### Health Monitor Dashboard
- **Real-time Vitals**: Heart rate, SpO₂, temperature display
- **Visual Meters**: Color-coded health indicators
- **Alert System**: Automatic alerts for out-of-range values
- **Emergency Messages**: Display last emergency button pressed
- **Auto-refresh**: Updates every 2 seconds

## 📡 Communication Protocol

The system uses **ESP-NOW** for reliable, low-latency communication:

```cpp
// Medicine dispenser sends signal to health monitor
uint8_t msg[] = {1};
esp_now_send(receiverMAC, msg, sizeof(msg));

// Health monitor receives and triggers vibration
void onReceive(const esp_now_recv_info_t *info, const uint8_t *incomingData, int len) {
  if (len == 1 && incomingData[0] == 1) {
    digitalWrite(VIBRATION_PIN, HIGH);  // 10-second vibration
    delay(10000);
    digitalWrite(VIBRATION_PIN, LOW);
  }
}
```

## 🚀 Installation & Setup

### Prerequisites
```cpp
// Required Libraries
#include <WiFi.h>
#include <WebServer.h>
#include <esp_now.h>
#include "MAX30100_PulseOximeter.h"
#include <OneWire.h>
#include <DallasTemperature.h>
```

### Hardware Setup
1. Connect all components according to the circuit diagram
2. Upload respective code to each ESP32
3. Configure MAC addresses for ESP-NOW communication
4. Set up Wi-Fi credentials

### Network Configuration
- **Medicine Unit**: Creates "Time Scheduler" hotspot (password: 12345678)
- **Health Monitor**: Creates "Health Monitor" hotspot
- Access web interfaces via ESP32 IP addresses

## 📊 Health Monitoring Ranges

| Parameter | Safe Range | Alert Condition |
|-----------|------------|-----------------|
| Heart Rate | 60-120 BPM | Outside range |
| SpO₂ | ≥95% | Below 95% |
| Temperature | 35-38°C | Outside range |

## 🔮 Future Enhancements

- **Enhanced Sensors**: Additional vital sign monitoring
- **Advanced Alerts**: SMS/Email notifications
- **Data Logging**: Long-term health trend analysis
- **Mobile App**: Dedicated smartphone application
- **Water Dispenser**: Automatic water dispensing with medicine
- **Voice Commands**: Voice-activated controls
- **Cloud Integration**: Remote monitoring capabilities

## 🎯 Use Cases

- **Elderly Care**: Automated medicine management for seniors
- **Chronic Patients**: Regular medication and health monitoring
- **Post-Surgery Care**: Recovery monitoring and medication compliance
- **Assisted Living**: Enhanced independence for patients with disabilities
- **Remote Healthcare**: Continuous monitoring for remote patients

## 🔧 Troubleshooting

### Common Issues
1. **ESP-NOW Connection Failed**: Check MAC addresses and ensure both devices are powered
2. **Sensor Not Detected**: Verify I2C connections for MAX30100
3. **Web Interface Not Loading**: Check Wi-Fi connection and IP address
4. **Stepper Motor Not Moving**: Verify power supply and driver connections

### Debug Information
- Serial monitor provides detailed logging
- LED indicators show system status
- Web interfaces display real-time system state

## 📄 License

This project is open-source and available under the MIT License. Feel free to modify and distribute according to your needs.

## 🤝 Contributing

Contributions are welcome! Please feel free to submit pull requests or open issues for bugs and feature requests.

---

**Project Status**: ✅ Fully Functional Prototype
**Last Updated**: 2025
**Version**: 2.0
