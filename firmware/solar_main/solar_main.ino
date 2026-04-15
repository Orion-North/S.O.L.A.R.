#include <WiFi.h>
#include <WebServer.h>
#include <ArduinoOTA.h>
#include <Wire.h>
#include <Adafruit_PWMServoDriver.h>
#include "esp_camera.h"
#include <esp_http_server.h>
#include "soc/soc.h"
#include "soc/rtc_cntl_reg.h"
#include <Preferences.h>

// CAMERA PINS FOR AI-THINKER ESP32-CAM
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

const char* ssid = "SOLAR_AP";
const char* password = "password123";

WebServer server(80);

#define I2C_SDA 14
#define I2C_SCL 15
Adafruit_PWMServoDriver pwm = Adafruit_PWMServoDriver();

#define SERVOMIN  110 
#define SERVOMAX  640 

Preferences prefs;

// Dynamic configuration instead of static consts
float offsets[16] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};

String sysDebug = "OS Boot OK\n";

// --- MOTOR MAPPING (Dynamic Sets) ---
int FL_SET = 1; 
int FR_SET = 2; 
int BL_SET = 4; 
int BR_SET = 3; 

int FL_HIP, FL_KNEE, FL_FOOT;
int FR_HIP, FR_KNEE, FR_FOOT;
int BL_HIP, BL_KNEE, BL_FOOT;
int BR_HIP, BR_KNEE, BR_FOOT;

const int SET_PINS[4][3] = {
  {0, 1, 2},   // Set 1
  {3, 4, 5},   // Set 2
  {6, 7, 8},   // Set 3
  {9, 10, 11}  // Set 4
};

void applyLegSets() {
  if (FL_SET < 1 || FL_SET > 4) FL_SET = 1;
  if (FR_SET < 1 || FR_SET > 4) FR_SET = 2;
  if (BL_SET < 1 || BL_SET > 4) BL_SET = 4;
  if (BR_SET < 1 || BR_SET > 4) BR_SET = 3;

  FL_HIP = SET_PINS[FL_SET - 1][0]; FL_KNEE = SET_PINS[FL_SET - 1][1]; FL_FOOT = SET_PINS[FL_SET - 1][2];
  FR_HIP = SET_PINS[FR_SET - 1][0]; FR_KNEE = SET_PINS[FR_SET - 1][1]; FR_FOOT = SET_PINS[FR_SET - 1][2];
  BL_HIP = SET_PINS[BL_SET - 1][0]; BL_KNEE = SET_PINS[BL_SET - 1][1]; BL_FOOT = SET_PINS[BL_SET - 1][2];
  BR_HIP = SET_PINS[BR_SET - 1][0]; BR_KNEE = SET_PINS[BR_SET - 1][1]; BR_FOOT = SET_PINS[BR_SET - 1][2];
}

int flashPin = 4; // AI-Thinker onboard high-power LED

int breathDepth = 35;   
int transitionResolution = 50;    
int stepDelay = 15;     

float lastPos[16] = {90,90,90,90,90,90,90,90,90,90,90,90,90,90,90,90};

bool runningTestSeq = false;
unsigned long lastSeqTime = 0;
bool calibrationMode = false;
bool isIdling = false;
bool torqueEnabled = true;

// STREAM SERVER DELETED TO IMPROVE LATENCY

// NEW: A "Smart Wait" that keeps the server alive
void smartWait(int ms) {
  unsigned long start = millis();
  while (millis() - start < ms) {
    ArduinoOTA.handle();
    server.handleClient();
    delay(1); // Tiny yield to prevent watchdog resets
  }
}

void writeServo(int motorId, float targetAngle) {
  float finalAngle = targetAngle + offsets[motorId];
  finalAngle = constrain(finalAngle, 0, 180); 
  int pulse = map(finalAngle, 0, 180, SERVOMIN, SERVOMAX);
  pwm.setPWM(motorId, 0, pulse);
  lastPos[motorId] = targetAngle;
}

void smoothTransition(float targets[16]) {
  for (int s = 1; s <= transitionResolution; s++) {
    ArduinoOTA.handle();
    server.handleClient();

    float t = (float)s / (float)transitionResolution;
    float multiplier = (1.0 - cos(t * PI)) / 2.0; 

    for (int i = 0; i <= 15; i++) {
      if(targets[i] == -1) continue; // Skip unassigned
      float start = lastPos[i];
      float end = targets[i];
      float intermediateAngle = start + (end - start) * multiplier;
      float finalAngle = intermediateAngle + offsets[i];
      int pulse = map(constrain(finalAngle, 0, 180), 0, 180, SERVOMIN, SERVOMAX);
      pwm.setPWM(i, 0, pulse);
    }
    delay(stepDelay); 
  }
  for (int i = 0; i <= 15; i++) {
    if(targets[i] != -1) lastPos[i] = targets[i];
  }
}

void fastTransition(float targets[16]) {
  int fastRes = 10;
  int fastDelay = 3; 
  for (int s = 1; s <= fastRes; s++) {
    ArduinoOTA.handle();
    server.handleClient();

    float t = (float)s / (float)fastRes;
    float multiplier = (1.0 - cos(t * PI)) / 2.0; 

    for (int i = 0; i <= 15; i++) {
      if(targets[i] == -1) continue; 
      float start = lastPos[i];
      float end = targets[i];
      float intermediateAngle = start + (end - start) * multiplier;
      float finalAngle = intermediateAngle + offsets[i];
      int pulse = map(constrain(finalAngle, 0, 180), 0, 180, SERVOMIN, SERVOMAX);
      pwm.setPWM(i, 0, pulse);
    }
    delay(fastDelay); 
  }
  for (int i = 0; i <= 15; i++) {
    if(targets[i] != -1) lastPos[i] = targets[i];
  }
}

void loadSettings() {
  prefs.begin("solar_cfg", false);

  // Reset offsets to 0 for a clean recalibration
  if (!prefs.getBool("recal_zero", false)) {
    for(int i=0; i<16; i++) {
        prefs.putFloat(("o" + String(i)).c_str(), 0.0);
    }
    prefs.putBool("recal_zero", true);
  }

  FL_SET = prefs.getInt("FL_SET", FL_SET);
  FR_SET = prefs.getInt("FR_SET", FR_SET);
  BL_SET = prefs.getInt("BL_SET", BL_SET);
  BR_SET = prefs.getInt("BR_SET", BR_SET);

  for(int i=0; i<16; i++) {
    String key = "o" + String(i);
    offsets[i] = prefs.getFloat(key.c_str(), offsets[i]);
  }
  applyLegSets();
}

#include <esp_wifi.h>

void setup() {
  WRITE_PERI_REG(RTC_CNTL_BROWN_OUT_REG, 0); // Disable brownout detector
  Serial.begin(115200);

  // Connection Stability Improvements
  WiFi.setTxPower(WIFI_POWER_17dBm); // Downclock TX power to fix sensor brownout
  WiFi.softAP(ssid, password, 6, 0, 1); // Channel 6, hidden=0, max_clients=1 
  esp_wifi_set_ps(WIFI_PS_NONE); // Disable Wi-Fi power saving to prevent packet drop

  pinMode(flashPin, OUTPUT);
  digitalWrite(flashPin, LOW);

  // CAMERA INIT
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
  config.xclk_freq_hz = 10000000; // Halved to 10MHz to improve logic reliability
  config.pixel_format = PIXFORMAT_JPEG;
  config.grab_mode = CAMERA_GRAB_WHEN_EMPTY; // Prevents crashing on PSRAM allocation fail
  
  if(psramFound()){
    config.frame_size = FRAMESIZE_QVGA; // Reduced from VGA to QVGA to radically un-choke Wi-Fi
    config.jpeg_quality = 20; 
    config.fb_count = 2;
    Serial.println("PSRAM found, utilizing QVGA.");
  } else {
    config.frame_size = FRAMESIZE_QVGA; 
    config.jpeg_quality = 24;
    config.fb_count = 1;
    Serial.println("No PSRAM found. Utilizing QVGA fallback.");
  }
  
  sysDebug += "PSRAM State: " + String(psramFound() ? "TRUE" : "FALSE") + "\n";
  
  esp_err_t err = esp_camera_init(&config);
  if (err != ESP_OK) {
    Serial.printf("Camera init failed with error 0x%x", err);
    sysDebug += "Camera Init: FAILED (Err 0x" + String(err, 16) + ")\n";
  } else {
    sysDebug += "Camera Init: SUCCESS\n";
    
    // Explicitly un-brick the hardware sensor exposure states
    sensor_t * s = esp_camera_sensor_get();
    if (s) {
      s->set_exposure_ctrl(s, 1);    
      s->set_awb_gain(s, 1);       
      s->set_gain_ctrl(s, 1);      
      s->set_brightness(s, 1);     // Increase brightness slightly
      sysDebug += "Sensor Calibrated: DMA Linked\n";
    }

    sysDebug += "Camera HTTPD: On-Demand Stream Ready\n";
  }

  server.on("/", []() { server.send(200, "text/plain", "Robot Online."); });
  server.on("/ping", []() { 
    server.sendHeader("Access-Control-Allow-Origin", "*");
    server.send(200, "text/plain", "PONG"); 
  });
  
  server.on("/capture", []() {
    server.sendHeader("Access-Control-Allow-Origin", "*");
    camera_fb_t * fb = esp_camera_fb_get();
    if (!fb) {
      server.send(500, "text/plain", "Capture Failed");
      return;
    }
    
    server.setContentLength(fb->len);
    server.send(200, "image/jpeg", "");
    WiFiClient client = server.client();
    client.write(fb->buf, fb->len);
    esp_camera_fb_return(fb);
  });
  server.on("/debug", []() { 
    server.sendHeader("Access-Control-Allow-Origin", "*");
    server.send(200, "text/plain", sysDebug); 
  });
  server.on("/flash", []() {
    server.sendHeader("Access-Control-Allow-Origin", "*");
    String state = server.arg("state");
    digitalWrite(flashPin, state == "1" ? HIGH : LOW);
    server.send(200, "text/plain", "FLASH " + state);
  });

  server.on("/testseq", []() {
    server.sendHeader("Access-Control-Allow-Origin", "*");
    runningTestSeq = true;
    server.send(200, "text/plain", "TEST SEQ START");
  });

  server.on("/settings/get", []() {
    server.sendHeader("Access-Control-Allow-Origin", "*");
    String js = "{";
    js += "\"FL_SET\":" + String(FL_SET) + ", \"FR_SET\":" + String(FR_SET) + ",";
    js += "\"BL_SET\":" + String(BL_SET) + ", \"BR_SET\":" + String(BR_SET) + ",";
    js += "\"offsets\":[";
    for(int i=0; i<16; i++) { js += String(offsets[i]) + (i<15 ? "," : ""); }
    js += "]}";
    server.send(200, "application/json", js);
  });

  server.on("/settings/set", []() {
    server.sendHeader("Access-Control-Allow-Origin", "*");
    String sets[] = {"FL_SET", "FR_SET", "BL_SET", "BR_SET"};
    int* setPtrs[] = {&FL_SET, &FR_SET, &BL_SET, &BR_SET};
    
    for(int i=0; i<4; i++) {
        if(server.hasArg(sets[i])) {
            *setPtrs[i] = server.arg(sets[i]).toInt();
            prefs.putInt(sets[i].c_str(), *setPtrs[i]);
        }
    }
    for(int i=0; i<16; i++) {
      String key = "o" + String(i);
      if(server.hasArg(key)) {
        offsets[i] = server.arg(key).toFloat();
        prefs.putFloat(key.c_str(), offsets[i]);
      }
    }
    applyLegSets();
    server.send(200, "text/plain", "SAVED TO NVS");
  });

  server.on("/test", []() {
    server.sendHeader("Access-Control-Allow-Origin", "*");
    if(server.hasArg("motor") && server.hasArg("angle")) {
      int m = server.arg("motor").toInt();
      float a = server.arg("angle").toFloat();
      writeServo(m, a);
    }
    server.send(200, "text/plain", "TEST ACK");
  });

  server.on("/seq", []() {
    server.sendHeader("Access-Control-Allow-Origin", "*");
    if(!server.hasArg("t")) {
       server.send(400, "text/plain", "Missing t");
       return;
    }
    String tStr = server.arg("t");
    bool fast = server.hasArg("fast") ? server.arg("fast").toInt() == 1 : false;

    float targets[16];
    int idx = 0;
    int startIdx = 0;
    for(int i=0; i<=tStr.length(); i++) {
       if(i == tStr.length() || tStr.charAt(i) == ',') {
          if(idx < 16) {
             targets[idx] = tStr.substring(startIdx, i).toFloat();
             idx++;
          }
          startIdx = i+1;
       }
    }
    for(int i=idx; i<16; i++) targets[i] = -1;

    if(fast) fastTransition(targets);
    else smoothTransition(targets);

    lastSeqTime = millis();
    server.send(200, "text/plain", "SEQ ACK");
  });

  server.on("/torque", []() {
    server.sendHeader("Access-Control-Allow-Origin", "*");
    if (server.hasArg("state")) {
        torqueEnabled = server.arg("state").toInt() == 1;
        if (!torqueEnabled) {
            for(int i=0; i<16; i++) {
                pwm.setPWM(i, 0, 0); // Release torque
            }
        } else {
            // Re-engage current targets
            for(int i=0; i<16; i++) {
                writeServo(i, lastPos[i] == -1 ? 90 : lastPos[i]);
            }
        }
    }
    server.send(200, "text/plain", torqueEnabled ? "TORQUE ENGAGED" : "TORQUE DISABLED");
  });

  server.on("/calib", []() {
    server.sendHeader("Access-Control-Allow-Origin", "*");
    if (server.hasArg("state")) {
        calibrationMode = server.arg("state").toInt() == 1;
        if (calibrationMode) {
            float t[16];
            for(int i=0; i<16; i++) t[i] = 90;
            smoothTransition(t);
        }
    }
    server.send(200, "text/plain", calibrationMode ? "CALIB ON" : "CALIB OFF");
  });

  ArduinoOTA.setHostname("SOLAR_ESP32");
  ArduinoOTA.begin(); 
  
  server.begin();

  loadSettings();

  Wire.begin(I2C_SDA, I2C_SCL);
  Wire.beginTransmission(0x40);
  if (Wire.endTransmission() == 0) {
    sysDebug += "I2C SCAN: PCA9685 FOUND AT 0x40\n";
  } else {
    sysDebug += "I2C SCAN: PCA9685 MISSING ON PINS 14/15!\n";
  }

  pwm.begin();
  pwm.setPWMFreq(50);
  for(int i = 0; i <= 15; i++) writeServo(i, 90);
}

void runTestSequence() {
  float targets[16];
  
  // 1. Move each set sequentially
  for(int setNum = 1; setNum <= 4; setNum++) {
    for(int i=0; i<=15; i++) targets[i] = -1;
    int hipId = SET_PINS[setNum - 1][0];
    int kneeId = SET_PINS[setNum - 1][1];
    int footId = SET_PINS[setNum - 1][2];
    targets[hipId] = 120; // HIP
    targets[kneeId] = 120; // KNEE
    targets[footId] = 120; // FOOT
    smoothTransition(targets);
    smartWait(500);

    targets[hipId] = 90;
    targets[kneeId] = 90;
    targets[footId] = 90;
    smoothTransition(targets);
    smartWait(500);
  }

  // 2. Move all sets at once to signal the end
  for(int i=0; i<=15; i++) targets[i] = -1;
  for(int setNum = 1; setNum <= 4; setNum++) {
    int hipId = SET_PINS[setNum - 1][0];
    int kneeId = SET_PINS[setNum - 1][1];
    int footId = SET_PINS[setNum - 1][2];
    targets[hipId] = 120;
    targets[kneeId] = 120;
    targets[footId] = 120;
  }
  smoothTransition(targets);
  smartWait(800);

  for(int i=0; i<=15; i++) targets[i] = -1;
  for(int setNum = 1; setNum <= 4; setNum++) {
    int hipId = SET_PINS[setNum - 1][0];
    int kneeId = SET_PINS[setNum - 1][1];
    int footId = SET_PINS[setNum - 1][2];
    targets[hipId] = 90;
    targets[kneeId] = 90;
    targets[footId] = 90;
  }
  smoothTransition(targets);
  smartWait(800);
}


void loop() {
  ArduinoOTA.handle();
  server.handleClient();

  if (runningTestSeq) {
    runTestSequence();
    runningTestSeq = false;
    return;
  }

  if (calibrationMode) return;

  // If computer drops Wi-Fi or no commands received for 2 seconds, just stand up and idle
  if (WiFi.softAPgetStationNum() == 0 || millis() - lastSeqTime > 2000) {
      if (!isIdling && torqueEnabled) {
          float targets[16];
          for(int i=0; i<=15; i++) targets[i] = 90;
          smoothTransition(targets);
          isIdling = true;
      }
  } else {
      isIdling = false;
  }


}
