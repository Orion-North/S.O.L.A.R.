from pathlib import Path
import re

source = Path('firmware/solar_main/solar_main.ino').read_text()

def block(text, marker):
    start = text.index(marker)
    opening = text.index('{', start)
    depth = 0
    for end in range(opening, len(text)):
        if text[end] == '{': depth += 1
        if text[end] == '}': depth -= 1
        if depth == 0:
            return text[start:end + 1]
    raise AssertionError(marker)

functions = '\n'.join(block(source, marker) for marker in [
    'int motorToPwmChannel(', 'bool setPwmSafe(',
    'void writeServoPulse(uint8_t channel, uint16_t pulse, bool force, bool exact)',
    'void writeServo(int motorId, float angle, bool exact)',
])
interpolation = block(source, '    if (localTorqueEnabled) {')
mapping = re.search(r'const int MOTOR_TO_PWM_CHANNEL\[16\] = \{.*?\};', source, re.S)[0]
defines = '\n'.join(re.findall(r'^#define SERVO(?:MIN|MAX).*$', source, re.M))
prefix = r'''
#include <cassert>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <algorithm>
#include <cstdlib>
using std::abs;
template<class T, class U, class V> T constrain(T v, U low, V high) {
  return v < low ? static_cast<T>(low) : v > high ? static_cast<T>(high) : v;
}
long map(long x, long low, long high, long outputLow, long outputHigh) {
  return (x - low) * (outputHigh - outputLow) / (high - low) + outputLow;
}
bool emergencyStop = false, torqueEnabled = true, localTorqueEnabled = true;
bool servoOutputEnabled[16], localServoOutputEnabled[16];
float offsets[16], currentAngle[16], targetAngle[16];
int lastServoPulse[16], stateLocks = 0, i2cLocks = 0;
const int SERVO_PULSE_DEADBAND = 2;
void lockState() { ++stateLocks; }
void unlockState() { --stateLocks; assert(stateLocks >= 0); }
void lockI2C() { ++i2cLocks; }
void unlockI2C() { --i2cLocks; assert(i2cLocks >= 0); }
struct FakePwm {
  int writes = 0, failuresRemaining = 0, delivered[16] = {};
  int setPWM(int channel, int on, int off) {
    assert(stateLocks > 0 && i2cLocks > 0);
    ++writes;
    if (failuresRemaining) { --failuresRemaining; return 1; }
    delivered[channel] = off; return 0;
  }
} pwm;
'''
tests = r'''
void reset() {
  assert(stateLocks == 0 && i2cLocks == 0);
  emergencyStop = false; torqueEnabled = localTorqueEnabled = true;
  pwm = FakePwm{};
  for (int i = 0; i < 16; ++i) {
    servoOutputEnabled[i] = localServoOutputEnabled[i] = true;
    offsets[i] = 0; currentAngle[i] = targetAngle[i] = 90;
    lastServoPulse[i] = -1;
  }
}
int main() {
  reset();
  lastServoPulse[0] = 374;
  writeServoPulse(0, 375, false, false);
  assert(pwm.writes == 0); // Preserve the in-motion deadband.
  writeServoPulse(0, 375, false, true);
  assert(pwm.writes == 1 && pwm.delivered[0] == 375);
  writeServoPulse(0, 375, false, true);
  assert(pwm.writes == 1); // Settled outputs do not flood I2C.
  puts("PASS: final one-count reset is sent; duplicate settled writes skipped");

  reset();
  lastServoPulse[0] = 374; pwm.failuresRemaining = 1;
  writeServoPulse(0, 375, false, true);
  assert(lastServoPulse[0] == -1);
  writeServoPulse(0, 375, false, true);
  assert(pwm.writes == 2 && lastServoPulse[0] == 375);
  puts("PASS: failed final reset is retried");

  reset(); emergencyStop = true;
  writeServoPulse(0, 375, false, true);
  assert(pwm.writes == 0);
  writeServoPulse(0, 0, true, false);
  assert(pwm.writes == 1 && pwm.delivered[0] == 0);
  reset(); torqueEnabled = false;
  writeServoPulse(0, 375, false, true);
  assert(pwm.writes == 0);
  puts("PASS: stop and torque-off block reset movement; output disable remains allowed");

  reset();
  for (int i = 0; i < 16; ++i) {
    currentAngle[i] = (i % 2) ? 155 : 25;
    offsets[i] = (i % 3) - 1.0f;
  }
  for (int tick = 0; tick < 50; ++tick) runInterpolationTick(3.0f);
  for (int i = 0; i < 16; ++i) {
    int expected = map(90 + offsets[i], 0, 180, SERVOMIN, SERVOMAX);
    assert(currentAngle[i] == targetAngle[i]);
    assert(pwm.delivered[motorToPwmChannel(i)] == expected);
  }
  int written = pwm.writes;
  runInterpolationTick(3.0f);
  assert(pwm.writes == written);
  puts("PASS: all motors return from emote extremes to calibrated neutral");

  reset();
  for (int i = 0; i < 16; ++i) lastServoPulse[i] = 374;
  pwm.failuresRemaining = 16;
  runInterpolationTick(3.0f);
  runInterpolationTick(3.0f);
  for (int channel = 0; channel < 16; ++channel)
    assert(pwm.delivered[channel] == 375);
  assert(stateLocks == 0 && i2cLocks == 0);
  puts("PASS: settled pose recovers from a complete failed-write cycle");
}
'''
code = prefix + defines + '\n' + mapping + '\n' + functions
code += '\nvoid runInterpolationTick(float max_step) {\n' + interpolation + '\n}\n'
Path('output/servo_reset_test.cpp').write_text(code + tests)
Path('output/run_servo_reset_test.cmd').write_text(
    '@echo off\n'
    'call "C:\\Program Files (x86)\\Microsoft Visual Studio\\2022\\BuildTools\\VC\\Auxiliary\\Build\\vcvars64.bat" >nul\n'
    'if errorlevel 1 exit /b 1\n'
    'cl /nologo /EHsc /W4 output\\servo_reset_test.cpp /Fooutput\\servo_reset_test.obj /Feoutput\\servo_reset_test.exe\n'
    'if errorlevel 1 exit /b 1\n'
    'output\\servo_reset_test.exe\n'
)
print('Generated regression harness using the actual firmware write and interpolation functions')
