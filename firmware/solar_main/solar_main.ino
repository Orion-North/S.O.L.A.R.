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
#define SOLAR_OTA_PASSWORD ""
#endif
#ifndef SOLAR_API_TOKEN
#define SOLAR_API_TOKEN ""
#endif
#ifndef SOLAR_PANEL_ADC_PIN
#define SOLAR_PANEL_ADC_PIN -1
#endif
#ifndef SOLAR_PANEL_VOLTAGE_DIVIDER
#define SOLAR_PANEL_VOLTAGE_DIVIDER 2.0f
#endif
#ifndef SOLAR_PANEL_ADC_ATTENUATION
#define SOLAR_PANEL_ADC_ATTENUATION ADC_11db
#endif
#ifndef SOLAR_PANEL_ADC_SAMPLES
#define SOLAR_PANEL_ADC_SAMPLES 8
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
const char* AUTH_HEADER_KEYS[] = {"x-solar-token"};

WebServer server(80);
SemaphoreHandle_t stateMutex = NULL;
SemaphoreHandle_t i2cMutex = NULL;

#define I2C_SDA 14
#define I2C_SCL 15
#define I2C_IMU_SDA 13
#define I2C_IMU_SCL 2
Adafruit_PWMServoDriver pwm = Adafruit_PWMServoDriver();

#define SERVOMIN  110 
#define SERVOMAX  640 

Preferences prefs;

// Dynamic configuration instead of static consts
const int OFFSET_SCHEMA_VERSION = 3;
const int LEG_SET_SCHEMA_VERSION = 2;
const float DEFAULT_OFFSETS[16] = {
  0, 0, 0,     // FL
  0, 0, 0,     // FR
  0, 0, 0,     // BR
  0, 0, 0,     // BL
  0, 0, 0, 0
};
float offsets[16] = {
  0, 0, 0,
  0, 0, 0,
  0, 0, 0,
  0, 0, 0,
  0, 0, 0, 0
};
float currentAngle[16];
float targetAngle[16];

const char* FIRMWARE_VERSION = "rl-solar-voltage-2026-06-02";
String sysDebug = String("OS Boot OK\nFW: ") + FIRMWARE_VERSION + "\n";

// --- ADAFRUIT 10-DOF IMU (L3GD20H + LSM303D/LSM303DLHC + BMP180) ---
const uint8_t LSM303D_ADDR_PRIMARY = 0x1D;
const uint8_t LSM303D_ADDR_SECONDARY = 0x1E;
const uint8_t LSM303DLHC_ACCEL_ADDR = 0x19;
const uint8_t LSM303DLHC_MAG_ADDR = 0x1E;
const uint8_t L3GD20H_ADDR_PRIMARY = 0x6B;
const uint8_t L3GD20H_ADDR_SECONDARY = 0x6A;
const uint8_t MPU6050_ADDR_PRIMARY = 0x68;
const uint8_t MPU6050_ADDR_SECONDARY = 0x69;
const uint8_t BMP180_ADDR = 0x77;
const uint16_t IMU_SAMPLE_INTERVAL_MS = 20; // 50 Hz local sampling, exposed on request only.
const uint8_t IMU_BINARY_VERSION = 1;
const uint8_t OBS_BINARY_VERSION = 1;

struct ImuTelemetry {
  bool accelReady = false;
  bool gyroReady = false;
  bool magReady = false;
  bool bmpReady = false;
  uint32_t seq = 0;
  unsigned long sampledAt = 0;
  float ax = 0;
  float ay = 0;
  float az = 0;
  float gx = 0;
  float gy = 0;
  float gz = 0;
  float mx = 0;
  float my = 0;
  float mz = 0;
  float roll = 0;
  float pitch = 0;
  float heading = 0;
};

ImuTelemetry imuTelemetry;
TwoWire* imuWire = &Wire1;
uint8_t lsm303dAddr = 0;
uint8_t lsm303dlhcAccelAddr = 0;
uint8_t lsm303dlhcMagAddr = 0;
uint8_t l3gd20hAddr = 0;
uint8_t mpu6050Addr = 0;
bool bmp180Ready = false;

struct __attribute__((packed)) ImuBinaryFrame {
  uint8_t version;
  uint8_t flags;
  uint16_t rateHz;
  uint32_t seq;
  uint32_t sampleMs;
  uint32_t ageMs;
  float ax;
  float ay;
  float az;
  float gx;
  float gy;
  float gz;
  float mx;
  float my;
  float mz;
  float roll;
  float pitch;
  float heading;
};

struct __attribute__((packed)) ObsBinaryFrame {
  uint8_t version;
  uint8_t flags;
  uint8_t mode;
  uint8_t reserved;
  uint32_t uptimeMs;
  uint32_t lastCmdAgoMs;
  float solarPanelVoltage;
  uint32_t imuSeq;
  uint32_t imuSampleMs;
  uint32_t imuAgeMs;
  float ax;
  float ay;
  float az;
  float gx;
  float gy;
  float gz;
  float mx;
  float my;
  float mz;
  float roll;
  float pitch;
  float heading;
};

// --- MOTOR MAPPING (Dynamic Sets) ---
int FL_SET = 1; 
int FR_SET = 2; 
int BL_SET = 3;
int BR_SET = 4;

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
  if (BL_SET < 1 || BL_SET > 4) BL_SET = 3;
  if (BR_SET < 1 || BR_SET > 4) BR_SET = 4;

  FL_HIP = SET_PINS[FL_SET - 1][0]; FL_KNEE = SET_PINS[FL_SET - 1][1]; FL_FOOT = SET_PINS[FL_SET - 1][2];
  FR_HIP = SET_PINS[FR_SET - 1][0]; FR_KNEE = SET_PINS[FR_SET - 1][1]; FR_FOOT = SET_PINS[FR_SET - 1][2];
  BL_HIP = SET_PINS[BL_SET - 1][0]; BL_KNEE = SET_PINS[BL_SET - 1][1]; BL_FOOT = SET_PINS[BL_SET - 1][2];
  BR_HIP = SET_PINS[BR_SET - 1][0]; BR_KNEE = SET_PINS[BR_SET - 1][1]; BR_FOOT = SET_PINS[BR_SET - 1][2];
}

const int STATUS_LED_PIN = 33; // AI-Thinker small onboard red LED, active-low
const bool STATUS_LED_ACTIVE_LOW = true;
bool statusLedOverride = false;
bool statusLedOverrideState = false;

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
bool torqueEnabled = false;
bool servoOutputEnabled[16] = {
  false, false, false, false,
  false, false, false, false,
  false, false, false, false,
  false, false, false, false
};
bool usingFallbackAp = false;
bool runningTestSeq = false;
bool emergencyStop = false;
bool cameraReady = false;
unsigned long lastAliveBlink = 0;
bool aliveBlinkState = false;
unsigned long lastSolarPanelSampleMs = 0;
int cachedSolarPanelAdcMv = -1;
float cachedSolarPanelVoltage = -1.0f;
const uint16_t SOLAR_PANEL_SAMPLE_INTERVAL_MS = 250;
const int SERVO_PULSE_DEADBAND = 2;
int lastServoPulse[16];

void writeServoPulse(uint8_t channel, uint16_t pulse, bool force = false);

// SIGNS matching the JS frontend implementation
float sign_H[4] = {1.0, -1.0, 1.0, -1.0}; // FL, FR, BL, BR
float sign_K[4] = {1.0, -1.0, 1.0, -1.0};
float sign_F[4] = {1.0, -1.0, 1.0, -1.0};

const float RL_ACTION_SCALE_DEG = 0.18f * RAD_TO_DEG;
const float RL_DEFAULT_HIP_RAD[4] = {-0.77f, 0.77f, 0.77f, -0.77f}; // FL, FR, BL, BR.
const float RL_DEFAULT_THIGH_RAD = 1.03f;
const float RL_DEFAULT_CALF_RAD = -1.0472f;

void lockState() {
  if (stateMutex != NULL) xSemaphoreTake(stateMutex, portMAX_DELAY);
}

void unlockState() {
  if (stateMutex != NULL) xSemaphoreGive(stateMutex);
}

void writeStatusLed(bool on) {
  digitalWrite(STATUS_LED_PIN, STATUS_LED_ACTIVE_LOW ? !on : on);
}

float boundedArg(const String& name, float fallback, float minVal, float maxVal) {
  if (!server.hasArg(name)) return fallback;
  return constrain(server.arg(name).toFloat(), minVal, maxVal);
}

bool isAllowedMode(const String& mode) {
  return mode == "stand" || mode == "idle" || mode == "manual" || mode == "walk" ||
         mode == "sit" || mode == "stretch" || mode == "wag" || mode == "dance" ||
         mode == "flip" || mode == "rl";
}

bool solarPanelAdcEnabled() {
  return SOLAR_PANEL_ADC_PIN >= 0;
}

int readSolarPanelAdcMv() {
  if (!solarPanelAdcEnabled()) return -1;
  const int sampleCount = max(1, SOLAR_PANEL_ADC_SAMPLES);
  long totalMv = 0;
  for (int i = 0; i < sampleCount; i++) {
    totalMv += analogReadMilliVolts(SOLAR_PANEL_ADC_PIN);
    delayMicroseconds(200);
  }
  return totalMv / sampleCount;
}

float solarPanelVoltageFromAdcMv(int adcMv) {
  if (adcMv < 0) return -1.0f;
  return (adcMv / 1000.0f) * SOLAR_PANEL_VOLTAGE_DIVIDER;
}

void refreshSolarPanelCache(bool force = false) {
  if (!solarPanelAdcEnabled()) {
    cachedSolarPanelAdcMv = -1;
    cachedSolarPanelVoltage = -1.0f;
    return;
  }

  unsigned long now = millis();
  if (!force && lastSolarPanelSampleMs != 0 && now - lastSolarPanelSampleMs < SOLAR_PANEL_SAMPLE_INTERVAL_MS) {
    return;
  }

  cachedSolarPanelAdcMv = readSolarPanelAdcMv();
  cachedSolarPanelVoltage = solarPanelVoltageFromAdcMv(cachedSolarPanelAdcMv);
  lastSolarPanelSampleMs = now;
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
    servoOutputEnabled[i] = false;
  }
  unlockState();

  for(int i=0; i<16; i++) {
    writeServoPulse(i, 0, true);
  }
}

void sendCors() {
  server.sendHeader("Access-Control-Allow-Origin", "*");
  server.sendHeader("Access-Control-Allow-Headers", "x-solar-token,content-type");
  server.sendHeader("Access-Control-Allow-Methods", "GET,POST,OPTIONS");
}

void lockI2C() {
  if (i2cMutex != NULL) xSemaphoreTake(i2cMutex, portMAX_DELAY);
}

void unlockI2C() {
  if (i2cMutex != NULL) xSemaphoreGive(i2cMutex);
}

void setPwmSafe(uint8_t channel, uint16_t on, uint16_t off) {
  lockI2C();
  pwm.setPWM(channel, on, off);
  unlockI2C();
}

void writeServoPulse(uint8_t channel, uint16_t pulse, bool force) {
  if (channel >= 16) return;
  if (!force && lastServoPulse[channel] >= 0 && abs(lastServoPulse[channel] - (int)pulse) < SERVO_PULSE_DEADBAND) {
    return;
  }

  setPwmSafe(channel, 0, pulse);
  lastServoPulse[channel] = pulse;
}

bool i2cWrite8(uint8_t addr, uint8_t reg, uint8_t value) {
  lockI2C();
  imuWire->beginTransmission(addr);
  imuWire->write(reg);
  imuWire->write(value);
  bool ok = imuWire->endTransmission() == 0;
  unlockI2C();
  return ok;
}

bool i2cReadBytes(uint8_t addr, uint8_t reg, uint8_t *buf, size_t len) {
  lockI2C();
  imuWire->beginTransmission(addr);
  imuWire->write(reg);
  if (imuWire->endTransmission(false) != 0) {
    unlockI2C();
    return false;
  }

  size_t readLen = imuWire->requestFrom((int)addr, (int)len);
  if (readLen != len) {
    unlockI2C();
    return false;
  }

  for (size_t i = 0; i < len; i++) {
    buf[i] = imuWire->read();
  }
  unlockI2C();
  return true;
}

bool i2cRead8(uint8_t addr, uint8_t reg, uint8_t &value) {
  return i2cReadBytes(addr, reg, &value, 1);
}

int16_t le16(const uint8_t *buf) {
  return (int16_t)((uint16_t)buf[1] << 8 | buf[0]);
}

int16_t be16(const uint8_t *buf) {
  return (int16_t)((uint16_t)buf[0] << 8 | buf[1]);
}

bool detectI2C(TwoWire& bus, uint8_t addr) {
  lockI2C();
  bus.beginTransmission(addr);
  bool ok = bus.endTransmission() == 0;
  unlockI2C();
  return ok;
}

bool detectedRead8(uint8_t addr, uint8_t reg, uint8_t &value) {
  return detectI2C(*imuWire, addr) && i2cRead8(addr, reg, value);
}

String scanI2CBus(TwoWire& bus, const char* label) {
  String result = "I2C SCAN ";
  result += label;
  result += ":";
  bool found = false;

  for (uint8_t addr = 1; addr < 127; addr++) {
    lockI2C();
    bus.beginTransmission(addr);
    uint8_t error = bus.endTransmission();
    unlockI2C();

    if (error == 0) {
      found = true;
      result += " 0x";
      if (addr < 16) result += "0";
      result += String(addr, HEX);
    }
  }

  if (!found) result += " none";
  result += "\n";
  result.toUpperCase();
  return result;
}

bool busHasImuDevice(TwoWire& bus) {
  return detectI2C(bus, LSM303D_ADDR_PRIMARY) ||
         detectI2C(bus, LSM303D_ADDR_SECONDARY) ||
         detectI2C(bus, LSM303DLHC_ACCEL_ADDR) ||
         detectI2C(bus, L3GD20H_ADDR_PRIMARY) ||
         detectI2C(bus, L3GD20H_ADDR_SECONDARY) ||
         detectI2C(bus, MPU6050_ADDR_PRIMARY) ||
         detectI2C(bus, MPU6050_ADDR_SECONDARY) ||
         detectI2C(bus, BMP180_ADDR);
}

int imuMotionScore(TwoWire& bus) {
  int score = 0;
  if (detectI2C(bus, MPU6050_ADDR_PRIMARY) || detectI2C(bus, MPU6050_ADDR_SECONDARY)) score += 4;
  if (detectI2C(bus, L3GD20H_ADDR_PRIMARY) || detectI2C(bus, L3GD20H_ADDR_SECONDARY)) score += 3;
  if (detectI2C(bus, LSM303DLHC_ACCEL_ADDR) || detectI2C(bus, LSM303D_ADDR_PRIMARY)) score += 2;
  if (detectI2C(bus, LSM303DLHC_MAG_ADDR)) score += 1;
  return score;
}

String imuBusName() {
  return imuWire == &Wire1 ? "pins_13_2" : "shared_pins_14_15";
}

String i2cDiagnosticsJson() {
  String json = "{";
  json += "\"imu_bus\":\"" + imuBusName() + "\",";
  json += "\"imu_sda_pin\":" + String(I2C_IMU_SDA) + ",";
  json += "\"imu_scl_pin\":" + String(I2C_IMU_SCL) + ",";
  json += "\"imu_sda_level\":" + String(digitalRead(I2C_IMU_SDA)) + ",";
  json += "\"imu_scl_level\":" + String(digitalRead(I2C_IMU_SCL)) + ",";
  json += "\"shared_sda_pin\":" + String(I2C_SDA) + ",";
  json += "\"shared_scl_pin\":" + String(I2C_SCL) + ",";
  json += "\"shared_sda_level\":" + String(digitalRead(I2C_SDA)) + ",";
  json += "\"shared_scl_level\":" + String(digitalRead(I2C_SCL)) + ",";
  json += "\"imu_pins_scan\":\"" + scanI2CBus(Wire1, "IMU PINS 13/2") + "\",";
  json += "\"shared_pins_scan\":\"" + scanI2CBus(Wire, "SHARED PINS 14/15") + "\",";
  json += "\"lsm303d_addr\":" + String(lsm303dAddr) + ",";
  json += "\"lsm303dlhc_accel_addr\":" + String(lsm303dlhcAccelAddr) + ",";
  json += "\"lsm303dlhc_mag_addr\":" + String(lsm303dlhcMagAddr) + ",";
  json += "\"l3gd20h_addr\":" + String(l3gd20hAddr) + ",";
  json += "\"mpu6050_addr\":" + String(mpu6050Addr) + ",";
  json += "\"bmp180_ready\":" + String(bmp180Ready ? "true" : "false");
  json += "}";
  json.replace("\n", "\\n");
  return json;
}

void initImuSensors() {
  uint8_t who = 0;

  sysDebug += scanI2CBus(Wire1, "IMU PINS 13/2");
  sysDebug += scanI2CBus(Wire, "SHARED PINS 14/15");

  int wire1MotionScore = imuMotionScore(Wire1);
  int wireMotionScore = imuMotionScore(Wire);
  if (wire1MotionScore > 0 || wireMotionScore > 0) {
    imuWire = wire1MotionScore >= wireMotionScore ? &Wire1 : &Wire;
    sysDebug += "IMU BUS: ";
    sysDebug += imuBusName();
    sysDebug += " MOTION_SCORE WIRE1=" + String(wire1MotionScore) + " WIRE=" + String(wireMotionScore) + "\n";
  } else if (busHasImuDevice(Wire1)) {
    imuWire = &Wire1;
    sysDebug += "IMU BUS: PINS 13/2 BMP_ONLY\n";
  } else if (busHasImuDevice(Wire)) {
    imuWire = &Wire;
    sysDebug += "IMU BUS: SHARED PINS 14/15 BMP_ONLY\n";
  } else {
    imuWire = &Wire1;
    sysDebug += "IMU BUS: NO IMU ADDRESSES FOUND\n";
  }

  who = 0;
  lsm303dAddr = 0;
  lsm303dlhcAccelAddr = 0;
  lsm303dlhcMagAddr = 0;
  l3gd20hAddr = 0;
  mpu6050Addr = 0;
  bmp180Ready = false;
  lockState();
  imuTelemetry.accelReady = false;
  imuTelemetry.gyroReady = false;
  imuTelemetry.magReady = false;
  imuTelemetry.bmpReady = false;
  unlockState();

  if (detectedRead8(MPU6050_ADDR_PRIMARY, 0x75, who) && (who == 0x68 || who == 0x70 || who == 0x71 || who == 0x73 || who == 0x75)) {
    mpu6050Addr = MPU6050_ADDR_PRIMARY;
  } else if (detectedRead8(MPU6050_ADDR_SECONDARY, 0x75, who) && (who == 0x68 || who == 0x70 || who == 0x71 || who == 0x73 || who == 0x75)) {
    mpu6050Addr = MPU6050_ADDR_SECONDARY;
  } else if (detectI2C(*imuWire, MPU6050_ADDR_PRIMARY)) {
    mpu6050Addr = MPU6050_ADDR_PRIMARY;
  } else if (detectI2C(*imuWire, MPU6050_ADDR_SECONDARY)) {
    mpu6050Addr = MPU6050_ADDR_SECONDARY;
  }

  if (mpu6050Addr != 0) {
    i2cWrite8(mpu6050Addr, 0x6B, 0x00); // Wake from sleep.
    delay(10);
    i2cWrite8(mpu6050Addr, 0x1A, 0x03); // DLPF around 44 Hz.
    i2cWrite8(mpu6050Addr, 0x1B, 0x08); // Gyro +/-500 dps.
    i2cWrite8(mpu6050Addr, 0x1C, 0x00); // Accel +/-2 g.
    lockState();
    imuTelemetry.accelReady = true;
    imuTelemetry.gyroReady = true;
    unlockState();
    sysDebug += "IMU: MPU6050/MPU9X50 FOUND AT 0x" + String(mpu6050Addr, HEX) + "\n";
  }

  if (detectedRead8(LSM303D_ADDR_PRIMARY, 0x0F, who) && who == 0x49) {
    lsm303dAddr = LSM303D_ADDR_PRIMARY;
  } else if (detectedRead8(LSM303D_ADDR_SECONDARY, 0x0F, who) && who == 0x49) {
    lsm303dAddr = LSM303D_ADDR_SECONDARY;
  }

  if (lsm303dAddr != 0) {
    i2cWrite8(lsm303dAddr, 0x20, 0x57); // Accel: 50 Hz, X/Y/Z enabled.
    i2cWrite8(lsm303dAddr, 0x21, 0x00); // Accel: +/-2 g.
    i2cWrite8(lsm303dAddr, 0x24, 0x64); // Mag: high resolution, 50 Hz.
    i2cWrite8(lsm303dAddr, 0x25, 0x20); // Mag: +/-4 gauss.
    i2cWrite8(lsm303dAddr, 0x26, 0x00); // Mag: continuous conversion.
    lockState();
    imuTelemetry.accelReady = true;
    imuTelemetry.magReady = true;
    unlockState();
    sysDebug += "IMU: LSM303D FOUND AT 0x" + String(lsm303dAddr, HEX) + "\n";
  } else {
    bool accelFound = detectI2C(*imuWire, LSM303DLHC_ACCEL_ADDR);
    bool magFound = detectI2C(*imuWire, LSM303DLHC_MAG_ADDR);

    if (accelFound) {
      lsm303dlhcAccelAddr = LSM303DLHC_ACCEL_ADDR;
      i2cWrite8(lsm303dlhcAccelAddr, 0x20, 0x57); // Accel: 100 Hz, X/Y/Z enabled.
      i2cWrite8(lsm303dlhcAccelAddr, 0x23, 0x08); // Accel: high-resolution, +/-2 g.
      lockState();
      imuTelemetry.accelReady = true;
      unlockState();
      sysDebug += "IMU: LSM303DLHC ACCEL FOUND AT 0x19\n";
    } else {
      sysDebug += "IMU: LSM303D/LSM303DLHC ACCEL MISSING\n";
    }

    if (magFound) {
      lsm303dlhcMagAddr = LSM303DLHC_MAG_ADDR;
      i2cWrite8(lsm303dlhcMagAddr, 0x00, 0x14); // Mag: 15 Hz.
      i2cWrite8(lsm303dlhcMagAddr, 0x01, 0x20); // Mag: +/-1.3 gauss.
      i2cWrite8(lsm303dlhcMagAddr, 0x02, 0x00); // Mag: continuous conversion.
      lockState();
      imuTelemetry.magReady = true;
      unlockState();
      sysDebug += "IMU: LSM303DLHC MAG FOUND AT 0x1E\n";
    } else {
      sysDebug += "IMU: LSM303D/LSM303DLHC MAG MISSING\n";
    }
  }

  who = 0;
  if (detectedRead8(L3GD20H_ADDR_PRIMARY, 0x0F, who) && (who == 0xD7 || who == 0xD4)) {
    l3gd20hAddr = L3GD20H_ADDR_PRIMARY;
  } else if (detectedRead8(L3GD20H_ADDR_SECONDARY, 0x0F, who) && (who == 0xD7 || who == 0xD4)) {
    l3gd20hAddr = L3GD20H_ADDR_SECONDARY;
  }

  if (l3gd20hAddr != 0) {
    i2cWrite8(l3gd20hAddr, 0x20, 0x0F); // Normal power, 95 Hz, X/Y/Z enabled.
    i2cWrite8(l3gd20hAddr, 0x23, 0x10); // +/-500 dps for useful robot motion range.
    lockState();
    imuTelemetry.gyroReady = true;
    unlockState();
    sysDebug += "IMU: L3GD20H FOUND AT 0x" + String(l3gd20hAddr, HEX) + "\n";
  } else {
    sysDebug += "IMU: L3GD20H MISSING\n";
  }

  if (detectedRead8(BMP180_ADDR, 0xD0, who) && who == 0x55) {
    bmp180Ready = true;
    lockState();
    imuTelemetry.bmpReady = true;
    unlockState();
    sysDebug += "IMU: BMP180 FOUND AT 0x77\n";
  } else {
    sysDebug += "IMU: BMP180 MISSING OR NOT PRIORITIZED\n";
  }
}

void sampleImu() {
  ImuTelemetry next;
  bool accelOk = false;
  bool magOk = false;
  bool gyroOk = false;
  uint8_t raw[6];

  lockState();
  next = imuTelemetry;
  unlockState();

  next.bmpReady = bmp180Ready;

  if (mpu6050Addr != 0 && i2cReadBytes(mpu6050Addr, 0x3B, raw, 6)) {
    int16_t axRaw = be16(&raw[0]);
    int16_t ayRaw = be16(&raw[2]);
    int16_t azRaw = be16(&raw[4]);
    next.ax = axRaw / 16384.0f; // g at +/-2 g.
    next.ay = ayRaw / 16384.0f;
    next.az = azRaw / 16384.0f;
    accelOk = true;
  } else if (lsm303dAddr != 0 && i2cReadBytes(lsm303dAddr, 0x28 | 0x80, raw, 6)) {
    int16_t axRaw = le16(&raw[0]);
    int16_t ayRaw = le16(&raw[2]);
    int16_t azRaw = le16(&raw[4]);
    next.ax = axRaw * 0.000061f; // g at +/-2 g.
    next.ay = ayRaw * 0.000061f;
    next.az = azRaw * 0.000061f;
    accelOk = true;
  } else if (lsm303dlhcAccelAddr != 0 && i2cReadBytes(lsm303dlhcAccelAddr, 0x28 | 0x80, raw, 6)) {
    int16_t axRaw = le16(&raw[0]) >> 4;
    int16_t ayRaw = le16(&raw[2]) >> 4;
    int16_t azRaw = le16(&raw[4]) >> 4;
    next.ax = axRaw * 0.001f; // g at +/-2 g.
    next.ay = ayRaw * 0.001f;
    next.az = azRaw * 0.001f;
    accelOk = true;
  }

  if (lsm303dAddr != 0 && i2cReadBytes(lsm303dAddr, 0x08 | 0x80, raw, 6)) {
    int16_t mxRaw = le16(&raw[0]);
    int16_t myRaw = le16(&raw[2]);
    int16_t mzRaw = le16(&raw[4]);
    next.mx = mxRaw * 0.016f; // microtesla at +/-4 gauss.
    next.my = myRaw * 0.016f;
    next.mz = mzRaw * 0.016f;
    magOk = true;
  } else if (lsm303dlhcMagAddr != 0 && i2cReadBytes(lsm303dlhcMagAddr, 0x03, raw, 6)) {
    int16_t mxRaw = be16(&raw[0]);
    int16_t mzRaw = be16(&raw[2]);
    int16_t myRaw = be16(&raw[4]);
    next.mx = mxRaw * 0.092f; // microtesla at +/-1.3 gauss.
    next.my = myRaw * 0.092f;
    next.mz = mzRaw * 0.102f;
    magOk = true;
  }

  if (mpu6050Addr != 0 && i2cReadBytes(mpu6050Addr, 0x43, raw, 6)) {
    int16_t gxRaw = be16(&raw[0]);
    int16_t gyRaw = be16(&raw[2]);
    int16_t gzRaw = be16(&raw[4]);
    next.gx = gxRaw / 65.5f; // dps at +/-500 dps.
    next.gy = gyRaw / 65.5f;
    next.gz = gzRaw / 65.5f;
    gyroOk = true;
  } else if (l3gd20hAddr != 0 && i2cReadBytes(l3gd20hAddr, 0x28 | 0x80, raw, 6)) {
    int16_t gxRaw = le16(&raw[0]);
    int16_t gyRaw = le16(&raw[2]);
    int16_t gzRaw = le16(&raw[4]);
    next.gx = gxRaw * 0.0175f; // dps at +/-500 dps.
    next.gy = gyRaw * 0.0175f;
    next.gz = gzRaw * 0.0175f;
    gyroOk = true;
  }

  if (accelOk) {
    float rollRad = atan2(next.ay, next.az);
    float pitchRad = atan2(-next.ax, sqrt(next.ay * next.ay + next.az * next.az));
    next.roll = rollRad * RAD_TO_DEG;
    next.pitch = pitchRad * RAD_TO_DEG;

    if (magOk) {
      float xh = next.mx * cos(pitchRad) + next.mz * sin(pitchRad);
      float yh = next.mx * sin(rollRad) * sin(pitchRad) +
                 next.my * cos(rollRad) -
                 next.mz * sin(rollRad) * cos(pitchRad);
      next.heading = atan2(-yh, xh) * RAD_TO_DEG;
      if (next.heading < 0) next.heading += 360.0f;
    }
  }

  next.accelReady = (mpu6050Addr != 0 || lsm303dAddr != 0 || lsm303dlhcAccelAddr != 0) && accelOk;
  next.magReady = (lsm303dAddr != 0 || lsm303dlhcMagAddr != 0) && magOk;
  next.gyroReady = (mpu6050Addr != 0 || l3gd20hAddr != 0) && gyroOk;
  next.sampledAt = millis();
  next.seq++;

  lockState();
  imuTelemetry = next;
  unlockState();
}

String imuJson(const ImuTelemetry &imu) {
  String json = "{";
  json += "\"seq\":" + String(imu.seq) + ",";
  json += "\"sample_ms\":" + String(imu.sampledAt) + ",";
  json += "\"age_ms\":" + String(millis() - imu.sampledAt) + ",";
  json += "\"rate_hz\":50,";
  json += "\"accel_ready\":" + String(imu.accelReady ? "true" : "false") + ",";
  json += "\"gyro_ready\":" + String(imu.gyroReady ? "true" : "false") + ",";
  json += "\"mag_ready\":" + String(imu.magReady ? "true" : "false") + ",";
  json += "\"bmp180_ready\":" + String(imu.bmpReady ? "true" : "false") + ",";
  json += "\"accel_g\":[" + String(imu.ax, 4) + "," + String(imu.ay, 4) + "," + String(imu.az, 4) + "],";
  json += "\"gyro_dps\":[" + String(imu.gx, 2) + "," + String(imu.gy, 2) + "," + String(imu.gz, 2) + "],";
  json += "\"mag_ut\":[" + String(imu.mx, 2) + "," + String(imu.my, 2) + "," + String(imu.mz, 2) + "],";
  json += "\"roll_deg\":" + String(imu.roll, 1) + ",";
  json += "\"pitch_deg\":" + String(imu.pitch, 1) + ",";
  json += "\"heading_deg\":" + String(imu.heading, 1);
  json += "}";
  return json;
}

void sendImuBinary(const ImuTelemetry &imu) {
  ImuBinaryFrame frame;
  frame.version = IMU_BINARY_VERSION;
  frame.flags = (imu.accelReady ? 0x01 : 0) |
                (imu.gyroReady ? 0x02 : 0) |
                (imu.magReady ? 0x04 : 0) |
                (imu.bmpReady ? 0x08 : 0);
  frame.rateHz = 50;
  frame.seq = imu.seq;
  frame.sampleMs = imu.sampledAt;
  frame.ageMs = millis() - imu.sampledAt;
  frame.ax = imu.ax;
  frame.ay = imu.ay;
  frame.az = imu.az;
  frame.gx = imu.gx;
  frame.gy = imu.gy;
  frame.gz = imu.gz;
  frame.mx = imu.mx;
  frame.my = imu.my;
  frame.mz = imu.mz;
  frame.roll = imu.roll;
  frame.pitch = imu.pitch;
  frame.heading = imu.heading;

  WiFiClient client = server.client();
  client.printf(
    "HTTP/1.1 200 OK\r\n"
    "Content-Type: application/octet-stream\r\n"
    "Content-Length: %u\r\n"
    "Access-Control-Allow-Origin: *\r\n"
    "Cache-Control: no-store\r\n"
    "Connection: close\r\n"
    "\r\n",
    sizeof(frame)
  );
  client.write((const uint8_t *)&frame, sizeof(frame));
}

uint8_t modeCode(const String& mode) {
  if (mode == "stand") return 1;
  if (mode == "idle") return 2;
  if (mode == "manual") return 3;
  if (mode == "walk") return 4;
  if (mode == "sit") return 5;
  if (mode == "stretch") return 6;
  if (mode == "wag") return 7;
  if (mode == "dance") return 8;
  if (mode == "flip") return 9;
  if (mode == "rl") return 10;
  return 0;
}

void sendObsBinary(const String& mode, unsigned long lastCmdAgo, bool estop, bool torque, bool solarConfigured, float solarVoltage, const ImuTelemetry &imu) {
  ObsBinaryFrame frame;
  frame.version = OBS_BINARY_VERSION;
  frame.flags = (estop ? 0x01 : 0) |
                (torque ? 0x02 : 0) |
                (solarConfigured ? 0x04 : 0) |
                (imu.accelReady ? 0x08 : 0) |
                (imu.gyroReady ? 0x10 : 0) |
                (imu.magReady ? 0x20 : 0) |
                (imu.bmpReady ? 0x40 : 0);
  frame.mode = modeCode(mode);
  frame.reserved = 0;
  frame.uptimeMs = millis() - startup_time;
  frame.lastCmdAgoMs = lastCmdAgo;
  frame.solarPanelVoltage = solarConfigured ? solarVoltage : -1.0f;
  frame.imuSeq = imu.seq;
  frame.imuSampleMs = imu.sampledAt;
  frame.imuAgeMs = millis() - imu.sampledAt;
  frame.ax = imu.ax;
  frame.ay = imu.ay;
  frame.az = imu.az;
  frame.gx = imu.gx;
  frame.gy = imu.gy;
  frame.gz = imu.gz;
  frame.mx = imu.mx;
  frame.my = imu.my;
  frame.mz = imu.mz;
  frame.roll = imu.roll;
  frame.pitch = imu.pitch;
  frame.heading = imu.heading;

  WiFiClient client = server.client();
  client.printf(
    "HTTP/1.1 200 OK\r\n"
    "Content-Type: application/octet-stream\r\n"
    "Content-Length: %u\r\n"
    "Access-Control-Allow-Origin: *\r\n"
    "Cache-Control: no-store\r\n"
    "Connection: close\r\n"
    "\r\n",
    sizeof(frame)
  );
  client.write((const uint8_t *)&frame, sizeof(frame));
}

const char* wifiStatusName(wl_status_t status) {
  switch (status) {
    case WL_IDLE_STATUS: return "IDLE";
    case WL_NO_SSID_AVAIL: return "NO_SSID";
    case WL_SCAN_COMPLETED: return "SCAN_COMPLETED";
    case WL_CONNECTED: return "CONNECTED";
    case WL_CONNECT_FAILED: return "CONNECT_FAILED";
    case WL_CONNECTION_LOST: return "CONNECTION_LOST";
    case WL_DISCONNECTED: return "DISCONNECTED";
    default: return "UNKNOWN";
  }
}

bool isApiAuthorized() {
  if (strlen(apiToken) == 0) return true;
  if (server.hasArg("token") && server.arg("token") == apiToken) return true;
  return server.header("x-solar-token") == apiToken;
}

bool requireApiAuth() {
  sendCors();
  if (server.method() == HTTP_OPTIONS) {
    server.send(204);
    return false;
  }
  if (isApiAuthorized()) return true;
  server.send(403, "text/plain", "Forbidden");
  return false;
}

void connectWiFi() {
  WiFi.mode(WIFI_AP_STA);
  WiFi.persistent(false);
  WiFi.setSleep(false);
  WiFi.setTxPower(WIFI_POWER_17dBm);
  WiFi.softAP(fallbackSsid, fallbackPassword, 6, 0, 1);
  usingFallbackAp = true;
  sysDebug += "Fallback AP Started: " + WiFi.softAPIP().toString() + "\n";

  if (strlen(homeSsid) > 0) {
    Serial.print("Connecting to home Wi-Fi SSID length: ");
    Serial.println(strlen(homeSsid));
    sysDebug += "Home Wi-Fi: CONNECTING\n";
    WiFi.begin(homeSsid, homePassword);
  } else {
    Serial.println("Home Wi-Fi not configured. Staying on fallback AP.");
    sysDebug += "Home Wi-Fi: NOT CONFIGURED\n";
  }

  unsigned long start = millis();
  wl_status_t lastStatus = WiFi.status();
  while (strlen(homeSsid) > 0 && WiFi.status() != WL_CONNECTED && millis() - start < 15000) {
    wl_status_t status = WiFi.status();
    if (status != lastStatus) {
      lastStatus = status;
      Serial.print("Wi-Fi status: ");
      Serial.println(wifiStatusName(status));
    }
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

  Serial.print("Home Wi-Fi failed. Status: ");
  Serial.println(wifiStatusName(WiFi.status()));
  Serial.print("Home Wi-Fi failed. Fallback AP IP: ");
  Serial.println(WiFi.softAPIP());
  sysDebug += "Wi-Fi Mode: FALLBACK_AP\n";
  sysDebug += "Home Wi-Fi Status: " + String(wifiStatusName(WiFi.status())) + "\n";
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

bool parseRlActions(float actions[12]) {
  if (server.hasArg("a")) {
    String payload = server.arg("a");
    int startIdx = 0;
    for (int i = 0; i < 12; i++) {
      int commaIdx = payload.indexOf(',', startIdx);
      String token = commaIdx >= 0 ? payload.substring(startIdx, commaIdx) : payload.substring(startIdx);
      token.trim();
      if (token.length() == 0) return false;
      actions[i] = constrain(token.toFloat(), -1.0f, 1.0f);
      if (commaIdx < 0) return i == 11;
      startIdx = commaIdx + 1;
    }
    return true;
  }

  for (int i = 0; i < 12; i++) {
    String key = "a" + String(i);
    if (!server.hasArg(key)) return false;
    actions[i] = constrain(server.arg(key).toFloat(), -1.0f, 1.0f);
  }
  return true;
}

void applyRlActions(const float actions[12], float outputScale) {
  outputScale = constrain(outputScale, 0.0f, 1.0f);

  for (int leg = 0; leg < 4; leg++) {
    int base = leg * 3;
    float hipDelta = RL_DEFAULT_HIP_RAD[leg] * RAD_TO_DEG + actions[base] * RL_ACTION_SCALE_DEG * outputScale;
    float kneeDelta = RL_DEFAULT_THIGH_RAD * RAD_TO_DEG + actions[base + 1] * RL_ACTION_SCALE_DEG * outputScale;
    float footDelta = RL_DEFAULT_CALF_RAD * RAD_TO_DEG + actions[base + 2] * RL_ACTION_SCALE_DEG * outputScale;
    float hip = 90.0f + hipDelta * sign_H[leg];
    float knee = 90.0f + kneeDelta * sign_K[leg];
    float foot = 90.0f + footDelta * sign_F[leg];
    setLegTarget(leg, constrain(hip, 0.0f, 180.0f), constrain(knee, 0.0f, 180.0f), constrain(foot, 0.0f, 180.0f));
  }
}

void blinkStatusLed(int count, int onMs, int offMs) {
  for (int i=0; i<count; i++) {
    writeStatusLed(true);
    delay(onMs);
    writeStatusLed(false);
    delay(offMs);
  }
}

void updateAliveLight() {
  lockState();
  bool overrideEnabled = statusLedOverride;
  bool overrideState = statusLedOverrideState;
  unlockState();

  if (overrideEnabled) {
    writeStatusLed(overrideState);
    return;
  }

  unsigned long now = millis();
  unsigned long interval = WiFi.status() == WL_CONNECTED ? 1200 : 300;
  if (now - lastAliveBlink >= interval) {
    lastAliveBlink = now;
    aliveBlinkState = !aliveBlinkState;
    writeStatusLed(aliveBlinkState);
  }
}

void writeServo(int motorId, float angle) {
  float finalAngle = angle + offsets[motorId];
  finalAngle = constrain(finalAngle, 0, 180); 
  int pulse = map(finalAngle, 0, 180, SERVOMIN, SERVOMAX);
  writeServoPulse(motorId, pulse);
}

void disableServoOutputs() {
  lockState();
  for(int i=0; i<16; i++) {
    servoOutputEnabled[i] = false;
  }
  unlockState();

  for(int i=0; i<16; i++) {
    writeServoPulse(i, 0, true);
  }
}

void enableAllServoOutputs() {
  lockState();
  for(int i=0; i<16; i++) {
    servoOutputEnabled[i] = true;
  }
  unlockState();
}

void enableOnlyServoOutput(int motorId) {
  lockState();
  for(int i=0; i<16; i++) {
    servoOutputEnabled[i] = (i == motorId);
  }
  unlockState();

  for(int i=0; i<16; i++) {
    if (i != motorId) writeServoPulse(i, 0, true);
  }
}

void enableMappedLegServoOutputsLocked() {
  for(int i=0; i<16; i++) {
    servoOutputEnabled[i] = false;
  }
  const int activeServos[12] = {
    FL_HIP, FL_KNEE, FL_FOOT,
    FR_HIP, FR_KNEE, FR_FOOT,
    BL_HIP, BL_KNEE, BL_FOOT,
    BR_HIP, BR_KNEE, BR_FOOT
  };
  for(int i=0; i<12; i++) {
    servoOutputEnabled[activeServos[i]] = true;
  }
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
  torqueEnabled = true;
  for(int i=0; i<16; i++) {
    servoOutputEnabled[i] = false;
  }
  unlockState();

  for(int setNum = 1; setNum <= 4; setNum++) {
    int hipId = SET_PINS[setNum - 1][0];
    int kneeId = SET_PINS[setNum - 1][1];
    int footId = SET_PINS[setNum - 1][2];

    lockState();
    for(int i=0; i<16; i++) {
      servoOutputEnabled[i] = false;
    }
    servoOutputEnabled[hipId] = true;
    servoOutputEnabled[kneeId] = true;
    servoOutputEnabled[footId] = true;
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
  for(int i=0; i<16; i++) {
    servoOutputEnabled[i] = false;
  }
  unlockState();
  for(int i=0; i<16; i++) writeServoPulse(i, 0, true);

  lockState();
  calibrationMode = false;
  torqueEnabled = false;
  cmd_mode = "stand";
  unlockState();
}

bool isOtaAuthorized() {
  if (strlen(otaPassword) == 0) return false;
  return server.authenticate(otaUser, otaPassword);
}

void sendOtaPage() {
  if (strlen(otaPassword) == 0) {
    server.send(403, "text/plain", "OTA disabled: set SOLAR_OTA_PASSWORD in secrets.h");
    return;
  }
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
  int offsetSchema = prefs.getInt("offset_schema", 0);
  if (!prefs.getBool("recal_zero", false) || offsetSchema < OFFSET_SCHEMA_VERSION) {
    for(int i=0; i<16; i++) {
        prefs.putFloat(("o" + String(i)).c_str(), DEFAULT_OFFSETS[i]);
    }
    prefs.putBool("recal_zero", true);
    prefs.putInt("offset_schema", OFFSET_SCHEMA_VERSION);
  }
  FL_SET = prefs.getInt("FL_SET", FL_SET);
  FR_SET = prefs.getInt("FR_SET", FR_SET);
  BL_SET = prefs.getInt("BL_SET", BL_SET);
  BR_SET = prefs.getInt("BR_SET", BR_SET);
  int legSetSchema = prefs.getInt("leg_set_schema", 0);
  if (legSetSchema < LEG_SET_SCHEMA_VERSION) {
    if (FL_SET == 1 && FR_SET == 2 && BL_SET == 4 && BR_SET == 3) {
      BL_SET = 3;
      BR_SET = 4;
      prefs.putInt("BL_SET", BL_SET);
      prefs.putInt("BR_SET", BR_SET);
    }
    prefs.putInt("leg_set_schema", LEG_SET_SCHEMA_VERSION);
  }
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
    bool localServoOutputEnabled[16];

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
    for(int i=0; i<16; i++) {
        localServoOutputEnabled[i] = servoOutputEnabled[i];
    }
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
            for(int i=0; i<4; i++) setLegTarget(i, 90, 90, 90);
            emote_phase = 0;
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
        else if (mode == "rl") {
            // Targets are supplied by /rl; this branch intentionally only lets interpolation run.
        }
    }

    // Interpolate current toward target
    float max_deg_per_sec = (mode == "walk") ? 400.0 : 150.0;
    if (mode == "rl") max_deg_per_sec = 500.0;
    if (mode == "flip") max_deg_per_sec = 300.0;
    float max_step = max_deg_per_sec * dt;

    if (localTorqueEnabled) {
        for(int i=0; i<16; i++) {
            if (!localServoOutputEnabled[i]) continue;
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

void imuTask(void *pvParameters) {
  const TickType_t xDelay = IMU_SAMPLE_INTERVAL_MS / portTICK_PERIOD_MS;

  while(1) {
    if (mpu6050Addr != 0 || lsm303dAddr != 0 || lsm303dlhcAccelAddr != 0 || lsm303dlhcMagAddr != 0 || l3gd20hAddr != 0) {
      sampleImu();
    }
    vTaskDelay(xDelay);
  }
}

void setup() {
  WRITE_PERI_REG(RTC_CNTL_BROWN_OUT_REG, 0); 
  Serial.begin(115200);
  startup_time = millis();
  stateMutex = xSemaphoreCreateMutex();
  i2cMutex = xSemaphoreCreateMutex();
  server.collectHeaders(AUTH_HEADER_KEYS, 1);

  pinMode(STATUS_LED_PIN, OUTPUT);
  writeStatusLed(false);
  blinkStatusLed(3, 120, 120);
  if (solarPanelAdcEnabled()) {
    pinMode(SOLAR_PANEL_ADC_PIN, INPUT);
    analogSetPinAttenuation(SOLAR_PANEL_ADC_PIN, SOLAR_PANEL_ADC_ATTENUATION);
    sysDebug += "Solar ADC: pin " + String(SOLAR_PANEL_ADC_PIN) +
                " divider " + String(SOLAR_PANEL_VOLTAGE_DIVIDER, 3) + "\n";
  } else {
    sysDebug += "Solar ADC: disabled; define SOLAR_PANEL_ADC_PIN to enable\n";
  }

  loadSettings();

  Wire.begin(I2C_SDA, I2C_SCL);
  Wire.setClock(400000);
  Wire1.begin(I2C_IMU_SDA, I2C_IMU_SCL);
  Wire1.setClock(100000);

  if (detectI2C(Wire, 0x40)) {
    sysDebug += "I2C SCAN: PCA9685 FOUND AT 0x40\n";
  } else {
    sysDebug += "I2C SCAN: PCA9685 MISSING ON PINS 14/15\n";
  }
  initImuSensors();

  lockI2C();
  pwm.begin();
  pwm.setPWMFreq(50);
  unlockI2C();
  for(int i = 0; i <= 15; i++) {
     currentAngle[i] = 90;
     targetAngle[i] = 90;
     servoOutputEnabled[i] = false;
     lastServoPulse[i] = -1;
     writeServoPulse(i, 0, true);
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
  config.pin_sccb_sda = SIOD_GPIO_NUM; config.pin_sccb_scl = SIOC_GPIO_NUM;
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
    cameraReady = true;
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
  server.on("/", []() { server.send(200, "text/plain", String("Robot Online. FW: ") + FIRMWARE_VERSION); });
  server.on("/version", []() { server.send(200, "text/plain", FIRMWARE_VERSION); });
  server.on("/ota", HTTP_GET, sendOtaPage);
  server.on("/ota", HTTP_POST, []() {
    if (strlen(otaPassword) == 0) {
      server.send(403, "text/plain", "OTA disabled: set SOLAR_OTA_PASSWORD in secrets.h");
      return;
    }
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

    if (!cameraReady) {
      server.send(503, "text/plain", "Camera Not Ready");
      return;
    }
    
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

    WiFiClient client = server.client();
    client.printf(
      "HTTP/1.1 200 OK\r\n"
      "Content-Type: image/jpeg\r\n"
      "Content-Length: %u\r\n"
      "Access-Control-Allow-Origin: *\r\n"
      "Cache-Control: no-store, no-cache, must-revalidate, max-age=0\r\n"
      "Pragma: no-cache\r\n"
      "Connection: close\r\n"
      "\r\n",
      fb->len
    );
    client.write(fb->buf, fb->len);
    esp_camera_fb_return(fb);
  });

  server.on("/debug", []() {
    if (!requireApiAuth()) return;
    lockState();
    String dbg = sysDebug;
    dbg += "Mode: " + cmd_mode + "\n";
    dbg += "Torque: " + String(torqueEnabled ? "ON" : "OFF") + "\n";
    dbg += "Calibration: " + String(calibrationMode ? "ON" : "OFF") + "\n";
    dbg += "Status LED Override: " + String(statusLedOverride ? "ON" : "OFF") + "\n";
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

  server.on("/rl", []() {
      if (!requireApiAuth()) return;
      float actions[12];
      if (!parseRlActions(actions)) {
          server.send(400, "text/plain", "Expected 12 RL actions as a CSV 'a' argument or a0..a11");
          return;
      }

      float outputScale = boundedArg("scale", 0.35f, 0.0f, 1.0f);

      lockState();
      if (emergencyStop) {
          unlockState();
          server.send(423, "text/plain", "Emergency stop active");
          return;
      }
      if (!torqueEnabled) {
          unlockState();
          server.send(409, "text/plain", "Torque disabled");
          return;
      }
      cmd_mode = "rl";
      cmd_vx = 0;
      cmd_vy = 0;
      cmd_wz = 0;
      calibrationMode = false;
      enableMappedLegServoOutputsLocked();
      last_cmd_time = millis();
      unlockState();

      applyRlActions(actions, outputScale);
      server.send(200, "text/plain", "RL ACK");
  });

  server.on("/obs", []() {
      if (!requireApiAuth()) return;
      ImuTelemetry imu;
      String mode;
      unsigned long lastCmdAgo;
      bool estop;
      bool torque;
      lockState();
      mode = cmd_mode;
      lastCmdAgo = millis() - last_cmd_time;
      estop = emergencyStop;
      torque = torqueEnabled;
      imu = imuTelemetry;
      unlockState();

      bool solarConfigured = solarPanelAdcEnabled();
      refreshSolarPanelCache(false);
      float solarVoltage = cachedSolarPanelVoltage;

      if (server.hasArg("fmt") && server.arg("fmt") == "bin") {
        sendObsBinary(mode, lastCmdAgo, estop, torque, solarConfigured, solarVoltage, imu);
        return;
      }

      String json = "{";
      json += "\"mode\":\"" + mode + "\",";
      json += "\"uptime_ms\":" + String(millis() - startup_time) + ",";
      json += "\"last_cmd_ms_ago\":" + String(lastCmdAgo) + ",";
      json += "\"emergency_stop\":" + String(estop ? "true" : "false") + ",";
      json += "\"torque_enabled\":" + String(torque ? "true" : "false") + ",";
      json += "\"solar_panel_configured\":" + String(solarConfigured ? "true" : "false") + ",";
      json += "\"solar_panel_voltage_v\":";
      json += solarConfigured ? String(solarVoltage, 3) : "null";
      json += ",";
      json += "\"imu_seq\":" + String(imu.seq) + ",";
      json += "\"imu_age_ms\":" + String(millis() - imu.sampledAt);
      json += "}";
      server.send(200, "application/json", json);
  });

  server.on("/status", []() {
      if (!requireApiAuth()) return;
      ImuTelemetry imu;
      lockState();
      String mode = cmd_mode;
      unsigned long lastCmdAgo = millis() - last_cmd_time;
      float stride = cmd_stride;
      float lift = cmd_lift;
      bool estop = emergencyStop;
      bool torque = torqueEnabled;
      imu = imuTelemetry;
      unlockState();
      bool solarConfigured = solarPanelAdcEnabled();
      refreshSolarPanelCache(false);
      int solarAdcMv = cachedSolarPanelAdcMv;
      float solarVoltage = cachedSolarPanelVoltage;

      if (server.hasArg("fast") && server.arg("fast") == "1") {
        String json = "{";
        json += "\"mode\":\"" + mode + "\",";
        json += "\"uptime_ms\":" + String(millis() - startup_time) + ",";
        json += "\"last_cmd_ms_ago\":" + String(lastCmdAgo) + ",";
        json += "\"emergency_stop\":" + String(estop ? "true" : "false") + ",";
        json += "\"torque_enabled\":" + String(torque ? "true" : "false") + ",";
        json += "\"solar_panel_configured\":" + String(solarConfigured ? "true" : "false") + ",";
        json += "\"solar_panel_voltage_v\":";
        json += solarConfigured ? String(solarVoltage, 3) : "null";
        json += ",";
        json += "\"imu_ready\":" + String((imu.accelReady || imu.gyroReady || imu.magReady) ? "true" : "false") + ",";
        json += "\"accel_ready\":" + String(imu.accelReady ? "true" : "false") + ",";
        json += "\"gyro_ready\":" + String(imu.gyroReady ? "true" : "false") + ",";
        json += "\"mag_ready\":" + String(imu.magReady ? "true" : "false") + ",";
        json += "\"imu_seq\":" + String(imu.seq) + ",";
        json += "\"imu_age_ms\":" + String(millis() - imu.sampledAt) + ",";
        json += "\"roll_deg\":" + String(imu.roll, 1) + ",";
        json += "\"pitch_deg\":" + String(imu.pitch, 1) + ",";
        json += "\"heading_deg\":" + String(imu.heading, 1);
        json += "}";
        server.send(200, "application/json", json);
        return;
      }

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
      json += "\"home_wifi_configured\":" + String(strlen(homeSsid) > 0 ? "true" : "false") + ",";
      json += "\"home_wifi_status\":\"" + String(wifiStatusName(WiFi.status())) + "\",";
      json += "\"home_wifi_rssi\":" + String(WiFi.status() == WL_CONNECTED ? WiFi.RSSI() : 0) + ",";
      json += "\"ip\":\"" + String(WiFi.status() == WL_CONNECTED ? WiFi.localIP().toString() : WiFi.softAPIP().toString()) + "\",";
      json += "\"ap_ip\":\"" + WiFi.softAPIP().toString() + "\",";
      json += "\"solar_panel_configured\":" + String(solarConfigured ? "true" : "false") + ",";
      json += "\"solar_panel_adc_pin\":" + String(SOLAR_PANEL_ADC_PIN) + ",";
      json += "\"solar_panel_adc_mv\":";
      json += solarConfigured ? String(solarAdcMv) : "null";
      json += ",";
      json += "\"solar_panel_voltage_v\":";
      json += solarConfigured ? String(solarVoltage, 3) : "null";
      json += ",";
      json += "\"imu_ready\":" + String((imu.accelReady || imu.gyroReady || imu.magReady) ? "true" : "false") + ",";
      json += "\"accel_ready\":" + String(imu.accelReady ? "true" : "false") + ",";
      json += "\"gyro_ready\":" + String(imu.gyroReady ? "true" : "false") + ",";
      json += "\"mag_ready\":" + String(imu.magReady ? "true" : "false") + ",";
      json += "\"bmp180_ready\":" + String(imu.bmpReady ? "true" : "false") + ",";
      json += "\"mpu6050_addr\":" + String(mpu6050Addr) + ",";
      json += "\"l3gd20h_addr\":" + String(l3gd20hAddr) + ",";
      json += "\"accel_addr\":" + String(mpu6050Addr != 0 ? mpu6050Addr : (lsm303dAddr != 0 ? lsm303dAddr : lsm303dlhcAccelAddr)) + ",";
      json += "\"imu_seq\":" + String(imu.seq) + ",";
      json += "\"imu_age_ms\":" + String(millis() - imu.sampledAt) + ",";
      json += "\"gyro_dps\":[" + String(imu.gx, 2) + "," + String(imu.gy, 2) + "," + String(imu.gz, 2) + "],";
      json += "\"accel_g\":[" + String(imu.ax, 4) + "," + String(imu.ay, 4) + "," + String(imu.az, 4) + "],";
      json += "\"roll_deg\":" + String(imu.roll, 1) + ",";
      json += "\"pitch_deg\":" + String(imu.pitch, 1) + ",";
      json += "\"heading_deg\":" + String(imu.heading, 1);
      json += "}";
      server.send(200, "application/json", json);
  });

  server.on("/imu", []() {
      if (!requireApiAuth()) return;
      ImuTelemetry imu;
      lockState();
      imu = imuTelemetry;
      unlockState();
      if (server.hasArg("fmt") && server.arg("fmt") == "bin") {
        sendImuBinary(imu);
        return;
      }
      server.send(200, "application/json", imuJson(imu));
  });

  server.on("/i2c", []() {
      if (!requireApiAuth()) return;
      server.send(200, "application/json", i2cDiagnosticsJson());
  });

  server.on("/flash", []() {
    if (!requireApiAuth()) return;
    String state = server.arg("state");
    lockState();
    statusLedOverride = true;
    statusLedOverrideState = state == "1";
    unlockState();
    writeStatusLed(statusLedOverrideState);
    server.send(200, "text/plain", "FLASH " + state);
  });

  server.on("/flash/auto", []() {
    if (!requireApiAuth()) return;
    lockState();
    statusLedOverride = false;
    unlockState();
    server.send(200, "text/plain", "FLASH AUTO");
  });

  server.on("/estop", []() {
    if (!requireApiAuth()) return;
    engageEmergencyStop();
    server.send(200, "text/plain", "EMERGENCY STOP");
  });

  server.on("/estop/clear", []() {
    if (!requireApiAuth()) return;
    lockState();
    emergencyStop = false;
    torqueEnabled = false;
    calibrationMode = false;
    cmd_mode = "stand";
    cmd_vx = 0;
    cmd_vy = 0;
    cmd_wz = 0;
    last_cmd_time = millis();
    for(int i=0; i<16; i++) {
      servoOutputEnabled[i] = false;
    }
    unlockState();
    for(int i=0; i<16; i++) writeServoPulse(i, 0, true);
    server.send(200, "text/plain", "EMERGENCY STOP CLEARED");
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
        bool requestedTorque = server.arg("state").toInt() == 1;
        if (requestedTorque && emergencyStop) {
            unlockState();
            server.send(423, "text/plain", "Emergency stop active; call /estop/clear first");
            return;
        }
        torqueEnabled = requestedTorque;
        enabled = torqueEnabled;
        if (torqueEnabled) {
            calibrationMode = false;
            cmd_mode = "stand";
            cmd_vx = 0;
            cmd_vy = 0;
            cmd_wz = 0;
            resetTargets = true;
            for(int i=0; i<16; i++) {
              servoOutputEnabled[i] = true;
            }
        }
        if (!torqueEnabled) {
            disableTorque = true;
            for(int i=0; i<16; i++) {
              servoOutputEnabled[i] = false;
            }
        }
    }
    unlockState();
    if (resetTargets) setAllTargets(90);
    if (disableTorque) {
      for(int i=0; i<16; i++) writeServoPulse(i, 0, true);
    }
    server.send(200, "text/plain", enabled ? "TORQUE ENGAGED" : "TORQUE DISABLED");
  });

  server.on("/calib", []() {
    if (!requireApiAuth()) return;
    bool enabled = calibrationMode;
    bool disableOutputs = false;
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
            torqueEnabled = true;
            for(int i=0; i<16; i++) {
              targetAngle[i] = 90;
              currentAngle[i] = 90;
              servoOutputEnabled[i] = false;
            }
            disableOutputs = true;
        } else {
            cmd_mode = "stand";
            torqueEnabled = false;
            for(int i=0; i<16; i++) {
              servoOutputEnabled[i] = false;
            }
            disableOutputs = true;
        }
    }
    unlockState();
    if (disableOutputs) {
      for(int i=0; i<16; i++) writeServoPulse(i, 0, true);
    }
    server.send(200, "text/plain", enabled ? "CALIB ON" : "CALIB OFF");
  });
  
  server.on("/test", []() {
    if (!requireApiAuth()) return;
    int selectedMotor = -1;
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
        torqueEnabled = true;
        targetAngle[m] = constrain(a, 0, 180);
        if (calibrationMode) {
          selectedMotor = m;
        } else {
          servoOutputEnabled[m] = true;
        }
        last_cmd_time = millis();
      }
    }
    unlockState();
    if (selectedMotor >= 0) enableOnlyServoOutput(selectedMotor);
    server.send(200, "text/plain", "TEST ACK");
  });

  ArduinoOTA.setHostname("SOLAR_ESP32");
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
  xTaskCreatePinnedToCore(
    imuTask, "ImuTask", 4096, NULL, 1, NULL, 1);
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
