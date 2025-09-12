import urllib.request
import numpy as np
import cv2
import mediapipe as mp
import time
 
# IMPORTANT: Update this URL with the one from your ESP32-CAM's Serial Monitor (likely ending in :81/stream)
ESP32_CAM_URL = 'http://192.168.1.21:81/stream'
# Load face detector
face_cascade = cv2.CascadeClassifier(cv2.data.haarcascades + 'haarcascade_frontalface_default.xml')

# Setup MediaPipe
mp_hands = mp.solutions.hands
mp_drawing = mp.solutions.drawing_utils
hands = mp_hands.Hands(max_num_hands=2, min_detection_confidence=0.5)

try:
    stream = cv2.VideoCapture(ESP32_CAM_URL)
    if not stream.isOpened():
        print(f"Error: Could not open video stream at {ESP32_CAM_URL}")
        print("Please check the URL and ensure the ESP32-CAM is streaming.")
    else:
        print("Successfully connected to camera stream. Press 'q' to quit.")
        while True:
            ret, frame = stream.read()
            if not ret or frame is None:
                print("Failed to grab frame, retrying...")
                time.sleep(0.5) # Wait a bit before retrying
                continue

            frame = cv2.flip(frame, 1)
            rgb_frame = cv2.cvtColor(frame, cv2.COLOR_BGR2RGB)

            # --- Face Detection (on grayscale for performance) ---
            gray = cv2.cvtColor(frame, cv2.COLOR_BGR2GRAY)
            faces = face_cascade.detectMultiScale(gray, 1.3, 5)
            for (x, y, w, h) in faces:
                cv2.rectangle(frame, (x, y), (x + w, y + h), (255, 0, 0), 2)
                cv2.putText(frame, 'Face', (x, y - 10), cv2.FONT_HERSHEY_SIMPLEX, 0.9, (255, 0, 0), 2)

            # --- Hand Detection (on RGB frame) ---
            # To improve performance, make the image smaller before processing
            rgb_frame.flags.writeable = False # Mark as non-writeable
            results = hands.process(rgb_frame)
            rgb_frame.flags.writeable = True # Mark as writeable again for drawing

            if results.multi_hand_landmarks:
                for hand_landmarks in results.multi_hand_landmarks:
                    # Draw bounding box
                    h_shape, w_shape, _ = frame.shape
                    x_min, y_min = w_shape, h_shape
                    x_max, y_max = 0, 0
                    for lm in hand_landmarks.landmark:
                        x, y = int(lm.x * w_shape), int(lm.y * h_shape)
                        x_min, y_min = min(x_min, x), min(y_min, y)
                        x_max, y_max = max(x_max, x), max(y_max, y)
                    
                    cv2.rectangle(frame, (x_min, y_min), (x_max, y_max), (0, 255, 0), 2)
                    cv2.putText(frame, 'Hand', (x_min, y_min - 10), cv2.FONT_HERSHEY_SIMPLEX, 0.9, (0, 255, 0), 2)
                    
                    # Draw landmarks
                    mp_drawing.draw_landmarks(frame, hand_landmarks, mp_hands.HAND_CONNECTIONS)

            cv2.imshow('Hand and Face Detection', frame)

            if cv2.waitKey(1) & 0xFF == ord('q'):
                break
        stream.release()

except Exception as e:
    print(f"An unexpected error occurred: {e}")
finally:
    cv2.destroyAllWindows()
    print("Window closed and resources released.")