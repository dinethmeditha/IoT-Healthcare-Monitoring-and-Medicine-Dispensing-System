#include "esp_camera.h"
#include <WiFi.h>
#include <WebServer.h>
#include <WiFiClient.h>

// Replace with your network credentials
const char* ssid = "YOUR_WIFI_SSID";
const char* password = "YOUR_WIFI_PASSWORD";

// AI Thinker ESP32-CAM pin definitions
#define PWDN_GPIO_NUM     32
#define RESET_GPIO_NUM    -1
#define XCLK_GPIO_NUM      0
#define SIOD_GPIO_NUM     26
#define SIOC_GPIO_NUM     27
#define Y9_GPIO_NUM       35
#define Y8_GPIO_NUM       34
#define Y7_GPIO_NUM       39
#define Y6_GPIO_NUM       36
#define Y5_GPIO_NUM       21
#define Y4_GPIO_NUM       19
#define Y3_GPIO_NUM       18
#define Y2_GPIO_NUM        5
#define VSYNC_GPIO_NUM    25
#define HREF_GPIO_NUM     23
#define PCLK_GPIO_NUM     22

WebServer server(80);

// HTML page for camera stream
const char index_html[] PROGMEM = R"rawliteral(
<!DOCTYPE HTML>
<html>
<head>
  <meta charset="utf-8">
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <title>ESP32-CAM Live Stream</title>
  <style>
    body {
      font-family: Arial, sans-serif;
      text-align: center;
      margin: 0;
      padding: 20px;
      background-color: #f0f0f0;
    }
    .container {
      max-width: 800px;
      margin: 0 auto;
      background: white;
      padding: 20px;
      border-radius: 10px;
      box-shadow: 0 4px 6px rgba(0,0,0,0.1);
    }
    h1 {
      color: #333;
      margin-bottom: 20px;
    }
    #stream {
      max-width: 100%;
      height: auto;
      border: 3px solid #333;
      border-radius: 8px;
      margin: 20px 0;
    }
    .controls {
      margin: 20px 0;
    }
    button {
      background: #007bff;
      color: white;
      border: none;
      padding: 10px 20px;
      margin: 5px;
      border-radius: 5px;
      cursor: pointer;
      font-size: 16px;
    }
    button:hover {
      background: #0056b3;
    }
    .info {
      background: #e9ecef;
      padding: 10px;
      border-radius: 5px;
      margin: 10px 0;
    }
  </style>
</head>
<body>
  <div class="container">
    <h1>ESP32-CAM Live Stream</h1>
    <div class="info">
      <p><strong>Stream URL:</strong> <span id="streamUrl"></span></p>
      <p><strong>Status:</strong> <span id="status">Loading...</span></p>
    </div>
    
    <img id="stream" src="/stream" alt="Camera Stream">
    
    <div class="controls">
      <button onclick="toggleStream()">Start/Stop Stream</button>
      <button onclick="capturePhoto()">Capture Photo</button>
      <button onclick="refreshStream()">Refresh</button>
    </div>
    
    <div id="photo-container" style="display: none;">
      <h3>Captured Photo</h3>
      <img id="captured-photo" style="max-width: 100%; border: 2px solid #333; border-radius: 8px;">
    </div>
  </div>

  <script>
    // Set the stream URL
    document.getElementById('streamUrl').textContent = window.location.origin + '/stream';
    
    // Check stream status
    const streamImg = document.getElementById('stream');
    
    streamImg.onload = function() {
      document.getElementById('status').textContent = 'Connected';
      document.getElementById('status').style.color = 'green';
    };
    
    streamImg.onerror = function() {
      document.getElementById('status').textContent = 'Disconnected';
      document.getElementById('status').style.color = 'red';
    };
    
    function toggleStream() {
      const stream = document.getElementById('stream');
      if (stream.style.display === 'none') {
        stream.style.display = 'block';
        stream.src = '/stream?' + Date.now();
      } else {
        stream.style.display = 'none';
      }
    }
    
    function refreshStream() {
      const stream = document.getElementById('stream');
      stream.src = '/stream?' + Date.now();
    }
    
    function capturePhoto() {
      fetch('/capture')
        .then(response => response.blob())
        .then(blob => {
          const url = URL.createObjectURL(blob);
          const capturedPhoto = document.getElementById('captured-photo');
          const photoContainer = document.getElementById('photo-container');
          
          capturedPhoto.src = url;
          photoContainer.style.display = 'block';
          
          // Scroll to photo
          photoContainer.scrollIntoView({ behavior: 'smooth' });
        })
        .catch(error => {
          console.error('Error capturing photo:', error);
          alert('Failed to capture photo');
        });
    }
    
    // Auto-refresh stream every 30 seconds to prevent timeout
    setInterval(refreshStream, 30000);
  </script>
</body>
</html>
)rawliteral";

void handleRoot() {
  server.send(200, "text/html", index_html);
}

void handleStream() {
  WiFiClient client = server.client();
  
  // Set headers for MJPEG stream
  server.setContentLength(CONTENT_LENGTH_UNKNOWN);
  server.send(200, "multipart/x-mixed-replace; boundary=frame");
  
  while (client.connected()) {
    camera_fb_t * fb = esp_camera_fb_get();
    if (!fb) {
      Serial.println("Camera capture failed");
      break;
    }
    
    // Send boundary and headers
    client.print("--frame\r\n");
    client.printf("Content-Type: image/jpeg\r\nContent-Length: %u\r\n\r\n", fb->len);
    
    // Send image data
    client.write(fb->buf, fb->len);
    client.print("\r\n");
    
    esp_camera_fb_return(fb);
    
    // Small delay to prevent overwhelming
    delay(30);
  }
}

void handleCapture() {
  camera_fb_t * fb = esp_camera_fb_get();
  if (!fb) {
    server.send(500, "text/plain", "Camera capture failed");
    return;
  }
  
  server.sendHeader("Content-Disposition", "attachment; filename=capture.jpg");
  server.send_P(200, "image/jpeg", (const char*)fb->buf, fb->len);
  
  esp_camera_fb_return(fb);
}

void handleNotFound() {
  server.send(404, "text/plain", "Page not found");
}

void setup() {
  Serial.begin(115200);
  Serial.println();
  
  // Initialize camera
  camera_config_t config;
  config.ledc_channel = LEDC_CHANNEL_0;
  config.ledc_timer = LEDC_TIMER_0;
  config.pin_d0 = Y2_GPIO_NUM;
  config.pin_d1 = Y3_GPIO_NUM;
  config.pin_d2 = Y4_GPIO_NUM;
  config.pin_d3 = Y5_GPIO_NUM;
  config.pin_d4 = Y6_GPIO_NUM;
  config.pin_d5 = Y7_GPIO_NUM;
  config.pin_d6 = Y8_GPIO_NUM;
  config.pin_d7 = Y9_GPIO_NUM;
  config.pin_xclk = XCLK_GPIO_NUM;
  config.pin_pclk = PCLK_GPIO_NUM;
  config.pin_vsync = VSYNC_GPIO_NUM;
  config.pin_href = HREF_GPIO_NUM;
  config.pin_sscb_sda = SIOD_GPIO_NUM;
  config.pin_sscb_scl = SIOC_GPIO_NUM;
  config.pin_pwdn = PWDN_GPIO_NUM;
  config.pin_reset = RESET_GPIO_NUM;
  config.xclk_freq_hz = 20000000;
  config.pixel_format = PIXFORMAT_JPEG;

  // Frame size and quality settings
  if(psramFound()){
    config.frame_size = FRAMESIZE_UXGA; // Higher resolution if PSRAM available
    config.jpeg_quality = 10;  // Lower number = higher quality
    config.fb_count = 2;
  } else {
    config.frame_size = FRAMESIZE_SVGA;
    config.jpeg_quality = 12;
    config.fb_count = 1;
  }

  // Initialize camera
  esp_err_t err = esp_camera_init(&config);
  if (err != ESP_OK) {
    Serial.printf("Camera init failed with error 0x%x", err);
    return;
  }

  // Initialize WiFi
  WiFi.begin(ssid, password);
  Serial.print("Connecting to WiFi");
  
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  
  Serial.println();
  Serial.println("WiFi connected!");
  Serial.print("Camera Ready! Use 'http://");
  Serial.print(WiFi.localIP());
  Serial.println("' to connect");

  // Set up web server routes
  server.on("/", handleRoot);
  server.on("/stream", handleStream);
  server.on("/capture", handleCapture);
  server.onNotFound(handleNotFound);
  
  // Start server
  server.begin();
  Serial.println("Web server started");
}

void loop() {
  server.handleClient();
}