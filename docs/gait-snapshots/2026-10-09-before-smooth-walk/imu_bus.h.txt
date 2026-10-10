#pragma once
#include <Wire.h>
#include <SoftWire.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <driver/gpio.h>

// Open-drain outputs release the line when written HIGH. Set pin direction
// once instead of repeatedly changing it for every software I2C edge.
inline void imuSdaLow(const SoftWire* bus) { digitalWrite(bus->getSda(), LOW); }
inline void imuSdaHigh(const SoftWire* bus) { digitalWrite(bus->getSda(), HIGH); }
inline void imuSclLow(const SoftWire* bus) { digitalWrite(bus->getScl(), LOW); }
inline void imuSclHigh(const SoftWire* bus) { digitalWrite(bus->getScl(), HIGH); }

inline void beginDedicatedImuBus(SoftWire& bus) {
  bus.begin();
  digitalWrite(bus.getSda(), HIGH);
  digitalWrite(bus.getScl(), HIGH);
  pinMode(bus.getSda(), OUTPUT_OPEN_DRAIN);
  pinMode(bus.getScl(), OUTPUT_OPEN_DRAIN);
  gpio_pullup_en(static_cast<gpio_num_t>(bus.getSda()));
  gpio_pullup_en(static_cast<gpio_num_t>(bus.getScl()));
  bus.setSetSdaLow(imuSdaLow);
  bus.setSetSdaHigh(imuSdaHigh);
  bus.setSetSclLow(imuSclLow);
  bus.setSetSclHigh(imuSclHigh);
}

// The bundled camera driver owns hardware I2C1. Keep the dedicated IMU
// pins on software I2C; the shared servo bus continues to use hardware I2C0.
class ImuBus {
 public:
  ImuBus(TwoWire& bus, SemaphoreHandle_t& mutex)
      : hardware_(&bus), software_(nullptr), mutex_(mutex) {}
  ImuBus(SoftWire& bus, SemaphoreHandle_t& mutex)
      : hardware_(nullptr), software_(&bus), mutex_(mutex) {}
  void lock() { if (mutex_) xSemaphoreTake(mutex_, portMAX_DELAY); }
  void unlock() { if (mutex_) xSemaphoreGive(mutex_); }
  void beginTransmission(uint8_t address) {
    if (hardware_) hardware_->beginTransmission(address);
    else software_->beginTransmission(address);
  }
  size_t write(uint8_t value) {
    return hardware_ ? hardware_->write(value) : software_->write(value);
  }
  uint8_t endTransmission(bool stop = true) {
    return hardware_ ? hardware_->endTransmission(stop) : software_->endTransmission(stop);
  }
  size_t requestFrom(int address, int length) {
    return hardware_ ? hardware_->requestFrom(address, length)
                     : software_->requestFrom(uint8_t(address), uint8_t(length));
  }
  int read() { return hardware_ ? hardware_->read() : software_->read(); }
 private:
  TwoWire* hardware_;
  SoftWire* software_;
  SemaphoreHandle_t& mutex_;
};
