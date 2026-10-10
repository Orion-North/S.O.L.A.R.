#pragma once
#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include "bounded_text_log.h"

class DebugLog {
 public:
  bool begin() {
    mutex_ = xSemaphoreCreateMutex();
    return mutex_ != nullptr;
  }
  void operator+=(const char* text) { append(text, strlen(text)); }
  void operator+=(const String& text) { append(text.c_str(), text.length()); }
  String snapshot() {
    String result;
    // Reserve before locking; no heap allocation while holding the log mutex.
    if (!mutex_) return String("Debug log unavailable\n");
    if (!result.reserve(CAPACITY)) return String("Debug snapshot unavailable\n");
    if (xSemaphoreTake(mutex_, portMAX_DELAY) != pdTRUE) return result;
    result.concat(log_.data(), log_.size());
    xSemaphoreGive(mutex_);
    return result;
  }
 private:
  static constexpr size_t CAPACITY = 2048;
  BoundedTextLog<CAPACITY> log_;
  SemaphoreHandle_t mutex_ = nullptr;
  void append(const char* text, size_t length) {
    if (!mutex_ || xSemaphoreTake(mutex_, portMAX_DELAY) != pdTRUE) return;
    log_.append(text, length);
    xSemaphoreGive(mutex_);
  }
};
