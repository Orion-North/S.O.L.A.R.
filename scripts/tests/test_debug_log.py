"""Build a native harness for the actual log headers with host mutex/String adapters."""
from pathlib import Path

include = Path('output/debug-log-test-include')
(include / 'freertos').mkdir(parents=True, exist_ok=True)
(include / 'Arduino.h').write_text(r'''
#pragma once
#include <string>
inline bool failReserve = false;
class String {
  std::string value;
 public:
  String() = default;
  String(const char* text) : value(text) {}
  bool reserve(size_t size) { if (failReserve) return false; value.reserve(size); return true; }
  void concat(const char* text, size_t length) { value.append(text, length); }
  const char* c_str() const { return value.c_str(); }
  size_t length() const { return value.size(); }
};
''')
(include / 'freertos/FreeRTOS.h').write_text(r'''
#pragma once
#include <mutex>
using SemaphoreHandle_t = std::mutex*;
constexpr int pdTRUE = 1;
constexpr int portMAX_DELAY = -1;
''')
(include / 'freertos/semphr.h').write_text(r'''
#pragma once
#include "FreeRTOS.h"
inline bool failMutex = false;
inline SemaphoreHandle_t xSemaphoreCreateMutex() { return failMutex ? nullptr : new std::mutex; }
inline int xSemaphoreTake(SemaphoreHandle_t mutex, int) { mutex->lock(); return pdTRUE; }
inline void xSemaphoreGive(SemaphoreHandle_t mutex) { mutex->unlock(); }
''')
Path('output/debug_log_test.cpp').write_text(r'''
#include "../firmware/solar_main/debug_log.h"
#include <cassert>
#include <cstdio>
#include <random>
#include <thread>
#include <vector>

int main() {
  BoundedTextLog<17> log;
  std::string expected;
  std::mt19937 random(42);
  for (int i = 0; i < 5000; ++i) {
    std::string message(random() % 80, char('A' + random() % 26));
    log.append(message.data(), message.size());
    expected += message;
    if (expected.size() > 17) expected.erase(0, expected.size() - 17);
    assert(log.size() == expected.size());
    assert(std::string(log.data(), log.size()) == expected);
    assert(log.data()[log.size()] == '\0');
  }
  puts("PASS: newest bytes preserved across empty, oversized and repeated appends");

  DebugLog failed;
  failMutex = true;
  assert(!failed.begin());
  failMutex = false;
  failed += "safe before initialization";
  assert(std::string(failed.snapshot().c_str()) == "Debug log unavailable\n");
  DebugLog shared;
  assert(shared.begin());
  shared += String("boot\n");
  assert(std::string(shared.snapshot().c_str()) == "boot\n");
  failReserve = true;
  assert(std::string(shared.snapshot().c_str()) == "Debug snapshot unavailable\n");
  failReserve = false;
  // Fill exactly one capacity with aligned 16-byte records before concurrency.
  std::string seed(2048, 'Z');
  for (size_t i = 15; i < seed.size(); i += 16) seed[i] = '\n';
  shared += String(seed.c_str());
  auto verify = [&] {
    String snapshot = shared.snapshot();
    assert(snapshot.length() == 2048);
    const char* bytes = snapshot.c_str();
    for (size_t i = 0; i < snapshot.length(); i += 16) {
      for (size_t j = 1; j < 15; ++j) assert(bytes[i + j] == bytes[i]);
      assert(bytes[i + 15] == '\n');
    }
  };
  std::vector<std::thread> threads;
  for (int id = 0; id < 4; ++id) threads.emplace_back([&, id] {
    std::string record(15, char('A' + id));
    record += '\n';
    for (int i = 0; i < 10000; ++i) {
      shared += String(record.c_str());
      if (i % 31 == 0) verify();
    }
  });
  for (auto& thread : threads) thread.join();
  verify();
  puts("PASS: concurrent appends/snapshots preserve whole records and the 2 KB limit");
  puts("PASS: mutex and snapshot allocation failures remain safe");
}
''')
Path('output/run_debug_log_test.cmd').write_text(
    '@echo off\n'
    'call "C:\\Program Files (x86)\\Microsoft Visual Studio\\2022\\BuildTools\\VC\\Auxiliary\\Build\\vcvars64.bat" >nul\n'
    'if errorlevel 1 exit /b 1\n'
    'cl /nologo /std:c++17 /EHsc /W4 /Ioutput\\debug-log-test-include output\\debug_log_test.cpp /Fooutput\\debug_log_test.obj /Feoutput\\debug_log_test.exe\n'
    'if errorlevel 1 exit /b 1\n'
    'output\\debug_log_test.exe\n'
)
print('Generated native bounded-log and concurrent snapshot tests')
