# Special Points: IoT Healthcare System

This document highlights the key technical features and logic within the **Medicine Supply Unit** (`medsupply.ino`) and the **Smart Glove** (`smartglove.ino`).

---

## System Architecture & Connectivity

The system consists of two main ESP32 devices that communicate with each other and expose web interfaces for user interaction.

```mermaid
graph TD
    subgraph Smart Glove
        A[ESP32 - Smart Glove]
        B[Sensors: MAX30102, DS18B20]
        C[OLED Display]
        D[Vibration Motor]
        E[Buttons]
        F[Wi-Fi AP: "Health Monitor"]
        B -- I2C/OneWire --> A
        A -- SPI --> C
        A -- GPIO --> D
        E -- GPIO --> A
        A -- Serves --> F
    end

    subgraph Medicine Supply Unit
        G[ESP32 - Medicine Dispenser]
        H[Stepper Motor]
        I[Wi-Fi AP: "Time Scheduler"]
        G -- GPIO --> H
        G -- Serves --> I
    end

    J[User/Patient]
    K[Caregiver/Admin]

    A -- ESP-NOW --> G
    G -- ESP-NOW --> A
    J -- Wi-Fi --> F
    K -- Wi-Fi --> I

    style A fill:#cde4ff
    style G fill:#cde4ff
```

### Key Connectivity Points:

1.  **Dual Access Points**: Each ESP32 creates its own Wi-Fi Access Point (AP).
    *   **Smart Glove**: `Health Monitor` for patients to view real-time vitals.
    *   **Medicine Dispenser**: `Time Scheduler` for caregivers to set medicine schedules.

2.  **ESP-NOW for Direct Communication**: Instead of relying on a central router, the two devices communicate directly using ESP-NOW, a low-overhead, connectionless protocol from Espressif.
    *   **Dispenser to Glove**: When a medicine is dispensed, the dispenser sends a signal (`{1}`) to the glove's MAC address, causing it to vibrate as a notification.
    *   **Glove to Dispenser**: This is not implemented but the receiver callback is present on the dispenser, allowing future expansion (e.g., sending an acknowledgment).

---

## 1. Medicine Supply Unit (`medsupply.ino`)

### Special Points & Code Examples

#### a. Non-Blocking Stepper Motor Control

To prevent the web server and other operations from freezing, the stepper motor and LED activation are handled in a non-blocking manner. Instead of using `delay()`, the code uses `millis()` to check if the required time has passed.

*   **Logic**:
    1.  A flag `stepperSequenceActive` is set to `true`.
    2.  The start time `stepperSequenceStart` is recorded.
    3.  The main `loop()` calls `handleStepperSequence()` repeatedly.
    4.  This function checks if `millis() - stepperSequenceStart` has exceeded the `ledOnDuration`.
    5.  Once the duration is met, it turns off the LED and resets the flags.

*   **Code Snippet**:
    ```cpp
    // In loop()
    handleStepperSequence();

    // ...

    void startStepperSequence(bool isManual, int scheduleIndex) {
      if (stepperSequenceActive) return; // Prevent re-triggering

      stepperSequenceActive = true;
      stepperSequenceStart = millis();
      // ... drive motor and turn on LED ...
      digitalWrite(ledPin, HIGH);
    }

    void handleStepperSequence() {
      if (!stepperSequenceActive) return;

      // Check if the LED on-time has elapsed
      if (millis() - stepperSequenceStart >= ledOnDuration) {
        digitalWrite(ledPin, LOW);
        stepperSequenceActive = false;
        // ... reset other flags ...
        Serial.println("Stepper sequence finished.");
      }
    }
    ```

#### b. Dynamic Scheduling and Time Calculation

The unit uses the Network Time Protocol (NTP) to fetch the real-world time. Schedules are set as a delay (in minutes) from the current time.

*   **Calculation**: The trigger time is calculated by converting the user-provided delay in minutes to seconds and adding it to the current Unix timestamp.

*   **Code Snippet**:
    ```cpp
    // In handleSetSchedule()
    time_t now;
    time(&now); // Get current time from NTP

    // ... parsing user input ...
    int delayMin = pair.substring(dash + 1).toInt();
    if (index >= 0 && index < 12 && delayMin > 0) {
      scheduled[index] = true;
      // Calculate and store the future trigger timestamp
      triggerTimes[index] = now + (delayMin * 60);
      originalDelays[index] = delayMin; // Store for UI
    }
    ```

#### c. Stepper Motor Step Calculation

The calculation determines how many steps the motor needs to turn to move from one medicine slot to the next. The dispenser has 12 slots, so each slot is 30 degrees apart (360° / 12).

*   **Calculation**: `totalSteps` is derived from the motor's steps per revolution and the microstepping setting, divided by the number of slots.

*   **Code Snippet**:
    ```cpp
    const int stepsPerRevolution = 20;
    const int microsteps = 10; // Assuming 1/10 microstepping
    // Total steps for a full circle = 20 * 10 = 200
    // Steps for one 30-degree slot = 200 / 12
    const long totalSteps = (stepsPerRevolution * microsteps) / 12;
    ```

---

## 2. Smart Glove (`smartglove.ino`)

### Special Points & Code Examples

#### a. Advanced Sensor Data Processing (MAX30102)

Reading accurate BPM and SpO₂ from a PPG sensor like the MAX30102 is complex. This code uses several techniques to improve reliability.

*   **Finger Detection**: The system only attempts to calculate vitals if a finger is present, determined by the raw IR value exceeding a threshold. This saves processing power and prevents false readings.

    ```cpp
    // In readSensorData()
    uint32_t irValue = particleSensor.getFIFOIR();
    fingerDetected = (irValue > 50000);
    if (!fingerDetected) {
      // Reset all values if no finger is present
      BPM = 0; SpO2 = 0; beatsDetected = false;
      return;
    }
    ```

*   **Beat Detection Algorithm**: It uses the `checkForBeat` function on the IR data to detect the pulse. To filter out noise (e.g., from slight movements), it ignores beats that occur too quickly after the previous one (`delta > 250ms`).

    ```cpp
    if (checkForBeat(dcRemovedIR) == true) {
        long delta = millis() - lastBeat;
        lastBeat = millis();

        // Filter short deltas (noise)
        if (delta > 250) { // Min 240ms (~240 BPM max)
            beatsPerMinute = 60 / (delta / 1000.0);
            // ... add to averaging array ...
        }
    }
    ```

*   **SpO₂ Calculation and Averaging**: The `maxim_heart_rate_and_oxygen_saturation` algorithm is used on buffers of IR and Red light data. The final displayed value is an average of the last 4 valid readings to provide a more stable output.

    ```cpp
    // In readSensorData()
    if (validSPO2 > 0 && spo2 > 70 && spo2 < 101) {
        // ... apply offset and cap at 100 ...
        spo2History[spo2HistoryIndex] = currentSpo2;
        spo2HistoryIndex = (spo2HistoryIndex + 1) % SPO2_AVG_SIZE;
    }
    // ... later ...
    float totalSpo2 = 0;
    // ... sum spo2History array ...
    if (validSamples > 0) SpO2 = totalSpo2 / validSamples;
    ```

#### b. Sensor Value Calibration (Offsets)

The raw sensor readings are adjusted with predefined offsets. This is a simple form of calibration to correct for consistent inaccuracies caused by sensor placement, hardware variations, or environmental factors.

*   **Code Snippet**:
    ```cpp
    // Sensor value offsets
    const float tempOffset = 2.0;
    const int bpmOffset = 40;
    const int spo2Offset = 10;

    // Example usage in the web interface
    String bpmStatus = fingerDetected ? (beatsDetected ? String((int)(BPM + bpmOffset)) : "No Beats") : "No Finger";
    ```

#### c. Multi-Functional OLED Display

The OLED display provides a rich, at-a-glance user interface that goes beyond just showing numbers.

*   **Contextual Status**: It shows "PLACE FINGER", "CALCULATING...", or "NO BEATS DETECTED" based on the sensor state, guiding the user.
*   **Alerts**: It displays a clear "ALERT: CHECK VITALS" message if any reading falls outside the normal range.
*   **Connectivity Status**: It shows a small icon (`*` or `o`) to indicate if a client is connected to its Wi-Fi AP.

*   **Code Snippet**:
    ```cpp
    // In updateDisplay()
    display.setCursor(0, 25);
    if ((!bpmOK && BPM > 0) || (!spo2OK && SpO2 > 0) || !tempOK) {
      display.print("ALERT: CHECK VITALS");
    } else if (fingerDetected && !beatsDetected && (millis() - fingerPlaceTime > 10000)) {
      display.print("NO BEATS DETECTED");
    } else if (fingerDetected) {
      display.print("CALCULATING...");
    } else {
      display.print("PLACE FINGER");
    }
    ```
