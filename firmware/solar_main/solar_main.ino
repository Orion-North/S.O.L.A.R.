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
#include <ESPmDNS.h>
#include <Update.h>
#include <utility>

#if __has_include("secrets.h")
#include "secrets.h"
#endif

#ifndef SOLAR_HOME_SSID
#define SOLAR_HOME_SSID ""
#endif
#ifndef SOLAR_HOME_PASSWORD
#define SOLAR_HOME_PASSWORD ""
#endif
#ifndef SOLAR_FALLBACK_SSID
#define SOLAR_FALLBACK_SSID "SOLAR_AP"
#endif
#ifndef SOLAR_FALLBACK_PASSWORD
#define SOLAR_FALLBACK_PASSWORD "change-this-ap-password"
#endif
#ifndef SOLAR_OTA_USER
#define SOLAR_OTA_USER "solar"
#endif
#ifndef SOLAR_OTA_PASSWORD
#define SOLAR_OTA_PASSWORD "change-this-ota-password"
#endif
#ifndef SOLAR_API_TOKEN
#define SOLAR_API_TOKEN "change-this-api-token"
#endif

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

const char* homeSsid = SOLAR_HOME_SSID;
const char* homePassword = SOLAR_HOME_PASSWORD;
const char* fallbackSsid = SOLAR_FALLBACK_SSID;
const char* fallbackPassword = SOLAR_FALLBACK_PASSWORD;
const char* otaUser = SOLAR_OTA_USER;
const char* otaPassword = SOLAR_OTA_PASSWORD;
const char* apiToken = SOLAR_API_TOKEN;

WebServer server(80);
SemaphoreHandle_t stateMutex = NULL;

#define I2C_SDA 14
#define I2C_SCL 15
Adafruit_PWMServoDriver pwm = Adafruit_PWMServoDriver();

#define SERVOMIN  110 
#define SERVOMAX  640 

Preferences prefs;

// Dynamic configuration instead of static consts
float offsets[16] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
float currentAngle[16];
float targetAngle[16];

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
bool flashOverride = false;
bool flashOverrideState = false;

// --- MOTION STATE VARIABLES ---
float cmd_vx = 0.0;
float cmd_vy = 0.0;
float cmd_wz = 0.0;
float cmd_speed = 1.0;
float cmd_stride = 30.0;
float cmd_lift = 30.0;
String cmd_mode = "stand";
unsigned long last_cmd_time = 0;
unsigned long last_camera_time = 0;
unsigned long startup_time = 0;

float gait_phase = 0.0;
float emote_phase = 0.0;

bool calibrationMode = false;
bool torqueEnabled = true;
bool usingFallbackAp = false;
bool runningTestSeq = false;
bool emergencyStop = false;
unsigned long lastAliveBlink = 0;
bool aliveBlinkState = false;

// SIGNS matching the JS frontend implementation
float sign_H[4] = {1.0, -1.0, 1.0, -1.0}; // FL, FR, BL, BR
float sign_K[4] = {1.0, -1.0, 1.0, -1.0};
float sign_F[4] = {1.0, -1.0, 1.0, -1.0};

void lockState() {
  if (stateMutex != NULL) xSemaphoreTake(stateMutex, portMAX_DELAY);
}

void unlockState() {
  if (stateMutex != NULL) xSemaphoreGive(stateMutex);
}

float boundedArg(const String& name, float fallback, float minVal, float maxVal) {
  if (!server.hasArg(name)) return fallback;
  return constrain(server.arg(name).toFloat(), minVal, maxVal);
}

bool isAllowedMode(const String& mode) {
  return mode == "stand" || mode == "idle" || mode == "manual" || mode == "walk" ||
         mode == "sit" || mode == "stretch" || mode == "wag" || mode == "dance" ||
         mode == "flip";
}

void engageEmergencyStop() {
  lockState();
  emergencyStop = true;
  torqueEnabled = false;
  calibrationMode = false;
  cmd_mode = "stand";
  cmd_vx = 0;
  cmd_vy = 0;
  cmd_wz = 0;
  cmd_speed = 1.0;
  last_cmd_time = millis();
  for(int i=0; i<16; i++) {
    targetAngle[i] = currentAngle[i];
  }
  unlockState();

  for(int i=0; i<16; i++) {
    pwm.setPWM(i, 0, 0);
  }
}

void sendCors() {
  server.sendHeader("Access-Control-Allow-Origin", "*");
}

bool isApiAuthorized() {
  return strlen(apiToken) == 0 || (server.hasArg("token") && server.arg("token") == apiToken);
}

bool requireApiAuth() {
  sendCors();
  if (isApiAuthorized()) return true;
  server.send(403, "text/plain", "Forbidden");
  return false;
}

void connectWiFi() {
  WiFi.mode(WIFI_AP_STA);
  WiFi.setTxPower(WIFI_POWER_17dBm);
  WiFi.softAP(fallbackSsid, fallbackPassword, 6, 0, 1);
  usingFallbackAp = true;
  sysDebug += "Fallback AP Started: " + WiFi.softAPIP().toString() + "\n";

  if (strlen(homeSsid) > 0) {
    WiFi.begin(homeSsid, homePassword);
  }

  unsigned long start = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - start < 3000) {
    ArduinoOTA.handle();
    server.handleClient();
    updateAliveLight();
    delay(100);
  }

  if (WiFi.status() == WL_CONNECTED) {
    usingFallbackAp = false;
    MDNS.begin("solar");
    Serial.print("Connected to home Wi-Fi. IP: ");
    Serial.println(WiFi.localIP());
    sysDebug += "Wi-Fi Mode: HOME\n";
    sysDebug += "IP: " + WiFi.localIP().toString() + "\n";
    return;
  }

  Serial.print("Home Wi-Fi failed. Fallback AP IP: ");
  Serial.println(WiFi.softAPIP());
  sysDebug += "Wi-Fi Mode: FALLBACK_AP\n";
  sysDebug += "IP: " + WiFi.softAPIP().toString() + "\n";
}

void setLegTarget(int legIndex, float h, float k, float f) {
  int hip, knee, foot;
  if (legIndex == 0) { hip = FL_HIP; knee = FL_KNEE; foot = FL_FOOT; }
  else if (legIndex == 1) { hip = FR_HIP; knee = FR_KNEE; foot = FR_FOOT; }
  else if (legIndex == 2) { hip = BL_HIP; knee = BL_KNEE; foot = BL_FOOT; }
  else { hip = BR_HIP; knee = BR_KNEE; foot = BR_FOOT; }
  
  lockState();
  targetAngle[hip] = h;
  targetAngle[knee] = k;
  targetAngle[foot] = f;
  unlockState();
}

void blinkFlash(int count, int onMs, int offMs) {
  for (int i=0; i<count; i++) {
    digitalWrite(flashPin, HIGH);
    delay(onMs);
    digitalWrite(flashPin, LOW);
    delay(offMs);
  }
}

void updateAliveLight() {
  lockState();
  bool overrideEnabled = flashOverride;
  bool overrideState = flashOverrideState;
  unlockState();

  if (overrideEnabled) {
    digitalWrite(flashPin, overrideState ? HIGH : LOW);
    return;
  }

  unsigned long now = millis();
  unsigned long interval = WiFi.status() == WL_CONNECTED ? 1200 : 300;
  if (now - lastAliveBlink >= interval) {
    lastAliveBlink = now;
    aliveBlinkState = !aliveBlinkState;
    digitalWrite(flashPin, aliveBlinkState ? HIGH : LOW);
  }
}

void writeServo(int motorId, float angle) {
  float finalAngle = angle + offsets[motorId];
  finalAngle = constrain(finalAngle, 0, 180); 
  int pulse = map(finalAngle, 0, 180, SERVOMIN, SERVOMAX);
  pwm.setPWM(motorId, 0, pulse);
}

void smartWait(int ms) {
  unsigned long start = millis();
  while (millis() - start < ms) {
    ArduinoOTA.handle();
    server.handleClient();
    delay(1);
  }
}

void setAllTargets(float angle) {
  lockState();
  for(int i=0; i<16; i++) {
    targetAngle[i] = angle;
  }
  unlockState();
}

void runTestSequence() {
  lockState();
  cmd_mode = "manual";
  calibrationMode = true;
  unlockState();

  for(int setNum = 1; setNum <= 4; setNum++) {
    int hipId = SET_PINS[setNum - 1][0];
    int kneeId = SET_PINS[setNum - 1][1];
    int footId = SET_PINS[setNum - 1][2];

    lockState();
    targetAngle[hipId] = 120;
    targetAngle[kneeId] = 120;
    targetAngle[footId] = 120;
    unlockState();
    smartWait(500);

    lockState();
    targetAngle[hipId] = 90;
    targetAngle[kneeId] = 90;
    targetAngle[footId] = 90;
    unlockState();
    smartWait(500);
  }

  lockState();
  for(int setNum = 1; setNum <= 4; setNum++) {
    int hipId = SET_PINS[setNum - 1][0];
    int kneeId = SET_PINS[setNum - 1][1];
    int footId = SET_PINS[setNum - 1][2];
    targetAngle[hipId] = 120;
    targetAngle[kneeId] = 120;
    targetAngle[footId] = 120;
  }
  unlockState();
  smartWait(800);

  setAllTargets(90);
  smartWait(800);
  lockState();
  calibrationMode = false;
  cmd_mode = "stand";
  unlockState();
}

bool isOtaAuthorized() {
  return server.authenticate(otaUser, otaPassword);
}

void sendOtaPage() {
  if (!isOtaAuthorized()) {
    server.requestAuthentication();
    return;
  }

  String page = "<!doctype html><html><head><meta name='viewport' content='width=device-width,initial-scale=1'>";
  page += "<title>SOLAR OTA</title></head><body>";
  page += "<h1>SOLAR OTA Update</h1>";
  page += "<form method='POST' action='/ota' enctype='multipart/form-data'>";
  page += "<input type='file' name='firmware' accept='.bin'>";
  page += "<button type='submit'>Upload Firmware</button>";
  page += "</form></body></html>";
  server.send(200, "text/html", page);
}

void handleOtaUpload() {
  if (!isOtaAuthorized()) return;

  HTTPUpload& upload = server.upload();
  if (upload.status == UPLOAD_FILE_START) {
    lockState();
    cmd_mode = "stand";
    cmd_vx = 0;
    cmd_vy = 0;
    cmd_wz = 0;
    unlockState();
    Serial.printf("OTA upload start: %s\n", upload.filename.c_str());
    if (!Update.begin(UPDATE_SIZE_UNKNOWN)) {
      Update.printError(Serial);
    }
  } else if (upload.status == UPLOAD_FILE_WRITE) {
    if (Update.write(upload.buf, upload.currentSize) != upload.currentSize) {
      Update.printError(Serial);
    }
  } else if (upload.status == UPLOAD_FILE_END) {
    if (Update.end(true)) {
      Serial.printf("OTA upload complete: %u bytes\n", upload.totalSize);
    } else {
      Update.printError(Serial);
    }
  }
}

void loadSettings() {
  prefs.begin("solar_cfg", false);
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

void gaitTask(void *pvParameters) {
  const TickType_t xDelay = 20 / portTICK_PERIOD_MS; // ~50Hz
  unsigned long last_tick = millis();

  while(1) {
    unsigned long now = millis();
    float dt = (now - last_tick) / 1000.0;
    last_tick = now;

    String mode;
    float local_vx, local_wz, local_speed, local_stride, local_lift;
    bool localCalibrationMode, localTorqueEnabled, localEmergencyStop;

    lockState();
    // Safety watchdog: fallback to idle if no commands received
    if (now - last_cmd_time > 2000 && cmd_mode != "idle" && cmd_mode != "stand" && !calibrationMode) {
        cmd_mode = "idle";
        cmd_vx = 0; cmd_vy = 0; cmd_wz = 0;
    }
    mode = cmd_mode;
    local_vx = cmd_vx;
    local_wz = cmd_wz;
    local_speed = cmd_speed;
    local_stride = cmd_stride;
    local_lift = cmd_lift;
    localCalibrationMode = calibrationMode;
    localTorqueEnabled = torqueEnabled;
    localEmergencyStop = emergencyStop;
    unlockState();

    if (localEmergencyStop) {
       // Motor outputs are disabled by engageEmergencyStop().
    } else if (localCalibrationMode) {
       // Hold current manual targets so /test and calibration tuning can move servos.
    } else if (!localTorqueEnabled) {
       // Do nothing
    } else {
        if (mode == "stand") {
            for(int i=0; i<4; i++) setLegTarget(i, 90, 90, 90);
            emote_phase = 0;
        } 
        else if (mode == "idle") {
            // Alive Idle Loop Math
            emote_phase += dt * 0.5; // slow breathing
            float breath = sin(emote_phase * PI * 2) * 10.0; // +/- 10 degrees
            for(int i=0; i<4; i++) {
               setLegTarget(i, 90, 90 + breath * sign_K[i], 90 + (breath*0.6) * sign_F[i]);
            }
        }
        else if (mode == "sit") {
            setLegTarget(0, 90, 90, 90);
            setLegTarget(1, 90, 90, 90);
            setLegTarget(2, 90, 90 + 60 * sign_K[2], 90 + 60 * sign_F[2]);
            setLegTarget(3, 90, 90 + 60 * sign_K[3], 90 + 60 * sign_F[3]);
        }
        else if (mode == "stretch") {
            setLegTarget(0, 90, 90 - 45 * sign_K[0], 90 + 45 * sign_F[0]);
            setLegTarget(1, 90, 90 - 45 * sign_K[1], 90 + 45 * sign_F[1]);
            setLegTarget(2, 90, 90, 90);
            setLegTarget(3, 90, 90, 90);
        }
        else if (mode == "wag") {
            emote_phase += dt * 5.0; // fast wag
            float wag = sin(emote_phase * PI * 2) * 30.0;
            for(int i=0; i<4; i++) setLegTarget(i, 90, 90, 90);
            setLegTarget(2, 90 + wag * sign_H[2], 90, 90);
            setLegTarget(3, 90 - wag * sign_H[3], 90, 90);
        }
        else if (mode == "dance") {
            emote_phase += dt * 2.0; 
            float dance = sin(emote_phase * PI * 2) * 30.0;
            for(int i=0; i<4; i++) {
                setLegTarget(i, 90, 90 + abs(dance) * sign_K[i], 90 + abs(dance) * sign_F[i]);
            }
        }
        else if (mode == "flip") {
            emote_phase += dt; // 1 cycle per second
            if (emote_phase < 1.0) {
                // Sweep legs up
                setLegTarget(0, 90, 90, 90);
                setLegTarget(1, 90, 90, 90);
                setLegTarget(2, 180, 180, 180);
                setLegTarget(3, 0, 0, 0);
            } else if (emote_phase < 2.0) {
                // Push
                setLegTarget(0, 90, 180, 90);
                setLegTarget(1, 90, 0, 90);
                setLegTarget(2, 180, 180, 180);
                setLegTarget(3, 0, 0, 0);
            } else if (emote_phase < 3.0) {
                for(int i=0; i<4; i++) setLegTarget(i, 90, 90, 90);
            } else {
                lockState();
                cmd_mode = "stand"; // return to stand
                unlockState();
                emote_phase = 0;
            }
        }
        else if (mode == "walk") {
            gait_phase += dt * local_speed * 2.0; // 2 steps per sec at 1.0
            if (gait_phase > 1.0) gait_phase -= 1.0;

            float st_amp = local_stride * abs(local_vx); // Stride amplitude
            float lift_amp = local_lift; // Lift amplitude
            float turn_amp = 20.0 * local_wz;

            // Simple 2-beat Diagonal Trot
            // Pair 1: FL (0) and BR (3)
            // Pair 2: FR (1) and BL (2)
            
            float p1 = gait_phase;
            float p2 = gait_phase + 0.5;
            if (p2 > 1.0) p2 -= 1.0;

            auto calcLeg = [](float p, float v_dir, float t_dir, float s_amp, float l_amp) {
                float swing = 0; float lift = 0;
                // Swing profile (half cycle lift, half cycle push)
                if (p < 0.5) {
                    // Swing phase
                    swing = cos(p * 2.0 * PI) * -(s_amp * v_dir + t_dir);
                    lift = sin(p * 2.0 * PI) * l_amp;
                } else {
                    // Stance phase
                    swing = cos(p * 2.0 * PI) * -(s_amp * v_dir + t_dir);
                    lift = 0;
                }
                return std::make_pair(swing, lift);
            };

            // FL
            auto fl = calcLeg(p1, (local_vx>=0?1:-1), turn_amp, st_amp, lift_amp);
            setLegTarget(0, 90 + fl.first * sign_H[0], 90 + fl.second * sign_K[0], 90 + fl.second * sign_F[0]);
            
            // BR
            auto br = calcLeg(p1, (local_vx>=0?1:-1), -turn_amp, st_amp, lift_amp);
            setLegTarget(3, 90 + br.first * sign_H[3], 90 + br.second * sign_K[3], 90 + br.second * sign_F[3]);

            // FR
            auto fr = calcLeg(p2, (local_vx>=0?1:-1), -turn_amp, st_amp, lift_amp);
            setLegTarget(1, 90 + fr.first * sign_H[1], 90 + fr.second * sign_K[1], 90 + fr.second * sign_F[1]);

            // BL
            auto bl = calcLeg(p2, (local_vx>=0?1:-1), turn_amp, st_amp, lift_amp);
            setLegTarget(2, 90 + bl.first * sign_H[2], 90 + bl.second * sign_K[2], 90 + bl.second * sign_F[2]);
        }
    }

    // Interpolate current toward target
    float max_deg_per_sec = (mode == "walk") ? 400.0 : 150.0;
    if (mode == "flip") max_deg_per_sec = 300.0;
    float max_step = max_deg_per_sec * dt;

    if (localTorqueEnabled) {
        for(int i=0; i<16; i++) {
            lockState();
            float diff = targetAngle[i] - currentAngle[i];
            if (abs(diff) <= max_step) {
                currentAngle[i] = targetAngle[i];
            } else {
                currentAngle[i] += (diff > 0) ? max_step : -max_step;
            }
            float angleToWrite = currentAngle[i];
            unlockState();
            writeServo(i, angleToWrite);
        }
    }

    vTaskDelay(xDelay);
  }
}

void setup() {
  WRITE_PERI_REG(RTC_CNTL_BROWN_OUT_REG, 0); 
  Serial.begin(115200);
  startup_time = millis();
  stateMutex = xSemaphoreCreateMutex();

  pinMode(flashPin, OUTPUT);
  digitalWrite(flashPin, LOW);
  blinkFlash(3, 120, 120);

  loadSettings();

  Wire.begin(I2C_SDA, I2C_SCL);
  Wire.beginTransmission(0x40);
  if (Wire.endTransmission() == 0) {
    sysDebug += "I2C SCAN: PCA9685 FOUND AT 0x40\n";
  } else {
    sysDebug += "I2C SCAN: PCA9685 MISSING ON PINS 14/15\n";
  }

  pwm.begin();
  pwm.setPWMFreq(50);
  for(int i = 0; i <= 15; i++) {
     currentAngle[i] = 90;
     targetAngle[i] = 90;
     writeServo(i, 90);
  }

  connectWiFi();
  esp_wifi_set_ps(WIFI_PS_NONE); 

  camera_config_t config;
  config.ledc_channel = LEDC_CHANNEL_0;
  config.ledc_timer = LEDC_TIMER_0;
  config.pin_d0 = Y2_GPIO_NUM; config.pin_d1 = Y3_GPIO_NUM; config.pin_d2 = Y4_GPIO_NUM;
  config.pin_d3 = Y5_GPIO_NUM; config.pin_d4 = Y6_GPIO_NUM; config.pin_d5 = Y7_GPIO_NUM;
  config.pin_d6 = Y8_GPIO_NUM; config.pin_d7 = Y9_GPIO_NUM; config.pin_xclk = XCLK_GPIO_NUM;
  config.pin_pclk = PCLK_GPIO_NUM; config.pin_vsync = VSYNC_GPIO_NUM; config.pin_href = HREF_GPIO_NUM;
  config.pin_sscb_sda = SIOD_GPIO_NUM; config.pin_sscb_scl = SIOC_GPIO_NUM;
  config.pin_pwdn = PWDN_GPIO_NUM; config.pin_reset = RESET_GPIO_NUM;
  config.xclk_freq_hz = 10000000; 
  config.pixel_format = PIXFORMAT_JPEG;
  config.grab_mode = CAMERA_GRAB_WHEN_EMPTY; 
  
  if(psramFound()){
    config.frame_size = FRAMESIZE_QQVGA; // Downgraded to QQVGA for stability
    config.jpeg_quality = 25; 
    config.fb_count = 1; // Double buffering uses too much DMA mapping
  } else {
    config.frame_size = FRAMESIZE_QQVGA; 
    config.jpeg_quality = 25;
    config.fb_count = 1;
  }
  
  esp_err_t cameraErr = esp_camera_init(&config);
  if (cameraErr != ESP_OK) {
    Serial.printf("Camera init failed with error 0x%x\n", cameraErr);
    sysDebug += "Camera Init: FAILED 0x" + String(cameraErr, 16) + "\n";
  } else {
    sysDebug += "Camera Init: SUCCESS\n";
  }

  sensor_t * s = esp_camera_sensor_get();
  if (s) {
      s->set_exposure_ctrl(s, 1);    
      s->set_awb_gain(s, 1);       
      s->set_gain_ctrl(s, 1);      
      s->set_brightness(s, 1);     
  }

  // Define HTTP Handlers
  server.on("/", []() { server.send(200, "text/plain", "Robot Online."); });
  server.on("/ota", HTTP_GET, sendOtaPage);
  server.on("/ota", HTTP_POST, []() {
    if (!isOtaAuthorized()) {
      server.requestAuthentication();
      return;
    }

    bool ok = !Update.hasError();
    server.send(ok ? 200 : 500, "text/plain", ok ? "Update complete. Rebooting..." : "Update failed.");
    if (ok) {
      delay(500);
      ESP.restart();
    }
  }, handleOtaUpload);

  server.on("/ping", []() { 
    if (!requireApiAuth()) return;
    lockState();
    last_cmd_time = millis(); // heartbeat updates timeout
    unlockState();
    server.send(200, "text/plain", "PONG"); 
  });
  
  server.on("/capture", []() {
    if (!requireApiAuth()) return;
    
    // FPS Limiter
    unsigned long now = millis();
    lockState();
    String mode = cmd_mode;
    unlockState();
    int fps_limit = (mode == "walk") ? 4 : 10;
    int ms_per_frame = 1000 / fps_limit;
    if (now - last_camera_time < ms_per_frame) {
        server.send(429, "text/plain", "Rate Limited");
        return;
    }
    last_camera_time = now;

    camera_fb_t * fb = esp_camera_fb_get();
    if (!fb) {
      server.send(500, "text/plain", "Capture Failed");
      return;
    }
    
    server.setContentLength(fb->len);
    server.send(200, "image/jpeg", "");
    server.client().write(fb->buf, fb->len);
    esp_camera_fb_return(fb);
  });

  server.on("/debug", []() {
    if (!requireApiAuth()) return;
    lockState();
    String dbg = sysDebug;
    dbg += "Mode: " + cmd_mode + "\n";
    dbg += "Torque: " + String(torqueEnabled ? "ON" : "OFF") + "\n";
    dbg += "Calibration: " + String(calibrationMode ? "ON" : "OFF") + "\n";
    dbg += "Flash Override: " + String(flashOverride ? "ON" : "OFF") + "\n";
    unlockState();
    dbg += "Free Heap: " + String(ESP.getFreeHeap()) + "\n";
    server.send(200, "text/plain", dbg);
  });

  server.on("/cmd", []() {
      if (!requireApiAuth()) return;
      lockState();
      if (emergencyStop) {
          unlockState();
          server.send(423, "text/plain", "Emergency stop active");
          return;
      }
      cmd_vx = boundedArg("vx", cmd_vx, -1.0, 1.0);
      cmd_vy = boundedArg("vy", cmd_vy, -1.0, 1.0);
      cmd_wz = boundedArg("wz", cmd_wz, -1.0, 1.0);
      cmd_speed = boundedArg("speed", cmd_speed, 0.1, 3.0);
      cmd_stride = boundedArg("stride", cmd_stride, 0.0, 60.0);
      cmd_lift = boundedArg("lift", cmd_lift, 0.0, 60.0);
      if (server.hasArg("mode")) {
          String newMode = server.arg("mode");
          if(!isAllowedMode(newMode)) {
              unlockState();
              server.send(400, "text/plain", "Invalid mode");
              return;
          }
          if(newMode != cmd_mode) {
              cmd_mode = newMode;
              emote_phase = 0; // reset phase on transition
              gait_phase = 0;
          }
      }
      last_cmd_time = millis();
      unlockState();
      server.send(200, "text/plain", "CMD ACK");
  });

  server.on("/status", []() {
      if (!requireApiAuth()) return;
      lockState();
      String mode = cmd_mode;
      unsigned long lastCmdAgo = millis() - last_cmd_time;
      float stride = cmd_stride;
      float lift = cmd_lift;
      bool estop = emergencyStop;
      bool torque = torqueEnabled;
      unlockState();
      String json = "{";
      json += "\"mode\":\"" + mode + "\",";
      json += "\"uptime_ms\":" + String(millis() - startup_time) + ",";
      json += "\"free_heap\":" + String(ESP.getFreeHeap()) + ",";
      json += "\"last_cmd_ms_ago\":" + String(lastCmdAgo) + ",";
      json += "\"gait_hz\":50,";
      json += "\"camera_fps_limit\":" + String((mode == "walk") ? 4 : 10) + ",";
      json += "\"stride\":" + String(stride) + ",";
      json += "\"lift\":" + String(lift) + ",";
      json += "\"emergency_stop\":" + String(estop ? "true" : "false") + ",";
      json += "\"torque_enabled\":" + String(torque ? "true" : "false") + ",";
      json += "\"wifi_clients\":" + String(WiFi.softAPgetStationNum()) + ",";
      json += "\"wifi_mode\":\"" + String(WiFi.status() == WL_CONNECTED ? "home_wifi_ap_fallback" : "fallback_ap") + "\",";
      json += "\"ip\":\"" + String(WiFi.status() == WL_CONNECTED ? WiFi.localIP().toString() : WiFi.softAPIP().toString()) + "\",";
      json += "\"ap_ip\":\"" + WiFi.softAPIP().toString() + "\"";
      json += "}";
      server.send(200, "application/json", json);
  });

  server.on("/flash", []() {
    if (!requireApiAuth()) return;
    String state = server.arg("state");
    lockState();
    flashOverride = true;
    flashOverrideState = state == "1";
    unlockState();
    digitalWrite(flashPin, flashOverrideState ? HIGH : LOW);
    server.send(200, "text/plain", "FLASH " + state);
  });

  server.on("/flash/auto", []() {
    if (!requireApiAuth()) return;
    lockState();
    flashOverride = false;
    unlockState();
    server.send(200, "text/plain", "FLASH AUTO");
  });

  server.on("/estop", []() {
    if (!requireApiAuth()) return;
    engageEmergencyStop();
    server.send(200, "text/plain", "EMERGENCY STOP");
  });

  server.on("/testseq", []() {
    if (!requireApiAuth()) return;
    lockState();
    if (emergencyStop) {
      unlockState();
      server.send(423, "text/plain", "Emergency stop active");
      return;
    }
    runningTestSeq = true;
    unlockState();
    server.send(200, "text/plain", "TEST SEQ START");
  });

  server.on("/settings/get", []() {
    if (!requireApiAuth()) return;
    lockState();
    String js = "{";
    js += "\"FL_SET\":" + String(FL_SET) + ", \"FR_SET\":" + String(FR_SET) + ",";
    js += "\"BL_SET\":" + String(BL_SET) + ", \"BR_SET\":" + String(BR_SET) + ",";
    js += "\"offsets\":[";
    for(int i=0; i<16; i++) { js += String(offsets[i]) + (i<15 ? "," : ""); }
    js += "]}";
    unlockState();
    server.send(200, "application/json", js);
  });

  server.on("/settings/set", []() {
    if (!requireApiAuth()) return;
    String sets[] = {"FL_SET", "FR_SET", "BL_SET", "BR_SET"};
    int* setPtrs[] = {&FL_SET, &FR_SET, &BL_SET, &BR_SET};
    lockState();
    for(int i=0; i<4; i++) {
        if(server.hasArg(sets[i])) {
            *setPtrs[i] = constrain(server.arg(sets[i]).toInt(), 1, 4);
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
    unlockState();
    server.send(200, "text/plain", "SAVED TO NVS");
  });

  server.on("/seq", []() {
    if (!requireApiAuth()) return;
    if(!server.hasArg("t")) {
       server.send(400, "text/plain", "Missing t");
       return;
    }

    lockState();
    if (emergencyStop) {
       unlockState();
       server.send(423, "text/plain", "Emergency stop active");
       return;
    }
    cmd_mode = "manual";
    String tStr = server.arg("t");
    int idx = 0;
    int startIdx = 0;
    for(int i=0; i<=tStr.length() && idx<16; i++) {
       if(i == tStr.length() || tStr.charAt(i) == ',') {
          float angle = tStr.substring(startIdx, i).toFloat();
          if(angle >= 0) targetAngle[idx] = constrain(angle, 0, 180);
          idx++;
          startIdx = i+1;
       }
    }

    last_cmd_time = millis();
    unlockState();
    server.send(200, "text/plain", "SEQ ACK");
  });

  server.on("/torque", []() {
    if (!requireApiAuth()) return;
    bool enabled;
    bool disableTorque = false;
    bool resetTargets = false;
    lockState();
    enabled = torqueEnabled;
    if (server.hasArg("state")) {
        torqueEnabled = server.arg("state").toInt() == 1;
        enabled = torqueEnabled;
        if (torqueEnabled) {
            emergencyStop = false;
            cmd_mode = "stand";
            cmd_vx = 0;
            cmd_vy = 0;
            cmd_wz = 0;
            resetTargets = true;
        }
        if (!torqueEnabled) {
            disableTorque = true;
        }
    }
    unlockState();
    if (resetTargets) setAllTargets(90);
    if (disableTorque) {
      for(int i=0; i<16; i++) pwm.setPWM(i, 0, 0); 
    }
    server.send(200, "text/plain", enabled ? "TORQUE ENGAGED" : "TORQUE DISABLED");
  });

  server.on("/calib", []() {
    if (!requireApiAuth()) return;
    bool enabled = calibrationMode;
    bool resetTargets = false;
    lockState();
    if (emergencyStop) {
        unlockState();
        server.send(423, "text/plain", "Emergency stop active");
        return;
    }
    if (server.hasArg("state")) {
        calibrationMode = server.arg("state").toInt() == 1;
        enabled = calibrationMode;
        if (calibrationMode) {
            cmd_mode = "manual";
            resetTargets = true;
        } else {
            cmd_mode = "stand";
        }
    }
    unlockState();
    if (resetTargets) setAllTargets(90);
    server.send(200, "text/plain", enabled ? "CALIB ON" : "CALIB OFF");
  });
  
  server.on("/test", []() {
    if (!requireApiAuth()) return;
    lockState();
    if (emergencyStop) {
      unlockState();
      server.send(423, "text/plain", "Emergency stop active");
      return;
    }
    if(server.hasArg("motor") && server.hasArg("angle")) {
      int m = server.arg("motor").toInt();
      float a = server.arg("angle").toFloat();
      if (m >= 0 && m < 16) {
        cmd_mode = "manual";
        targetAngle[m] = constrain(a, 0, 180);
        last_cmd_time = millis();
      }
    }
    unlockState();
    server.send(200, "text/plain", "TEST ACK");
  });

  ArduinoOTA.setHostname("SOLAR_ESP32");
  ArduinoOTA.setPassword(otaPassword);
  ArduinoOTA.onStart([]() {
    lockState();
    cmd_mode = "stand";
    cmd_vx = 0;
    cmd_vy = 0;
    cmd_wz = 0;
    unlockState();
    Serial.println("ArduinoOTA start");
  });
  ArduinoOTA.onEnd([]() {
    Serial.println("ArduinoOTA end");
  });
  ArduinoOTA.onError([](ota_error_t error) {
    Serial.printf("ArduinoOTA error[%u]\n", error);
  });
  ArduinoOTA.begin(); 
  
  server.begin();

  // Create Gait Task on Core 1 (App Core)
  xTaskCreatePinnedToCore(
    gaitTask, "GaitTask", 4096, NULL, 2, NULL, 1);
}

void loop() {
  ArduinoOTA.handle();
  server.handleClient();
  updateAliveLight();

  bool shouldRunTestSeq = false;
  lockState();
  if (runningTestSeq) {
    runningTestSeq = false;
    shouldRunTestSeq = true;
  }
  unlockState();

  if (shouldRunTestSeq) runTestSequence();

  delay(2); // Small yield
}
