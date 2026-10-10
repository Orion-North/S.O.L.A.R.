#pragma once
#include <math.h>
#include <stdint.h>

struct ImuCalibration {
  uint32_t schema = 1;
  float roll = 0, pitch = 0, gx = 0, gy = 0, gz = 0;
  bool valid() const {
    return schema == 1 && isfinite(roll) && isfinite(pitch) &&
      isfinite(gx) && isfinite(gy) && isfinite(gz) &&
      fabsf(roll) <= 180 && fabsf(pitch) <= 90 &&
      fabsf(gx) < 20 && fabsf(gy) < 20 && fabsf(gz) < 20;
  }
};

inline float wrapImuAngle(float angle) {
  while (angle > 180) angle -= 360;
  while (angle < -180) angle += 360;
  return angle;
}

struct ImuCalibrationWindow {
  uint16_t count = 0;
  float mean[6] = {}, m2[6] = {};
  bool add(float ax, float ay, float az, float gx, float gy, float gz) {
    const float values[] = {ax, ay, az, gx, gy, gz};
    float norm = sqrtf(ax * ax + ay * ay + az * az);
    if (!isfinite(norm) || norm < 0.8f || norm > 1.2f) return false;
    for (int i = 0; i < 6; ++i) {
      if (!isfinite(values[i]) || (i >= 3 && fabsf(values[i]) >= 20)) return false;
    }
    ++count;
    for (int i = 0; i < 6; ++i) {
      float delta = values[i] - mean[i];
      mean[i] += delta / count;
      m2[i] += delta * (values[i] - mean[i]);
    }
    return true;
  }
  bool finish(ImuCalibration& result) const {
    if (count < 100) return false;
    for (int i = 0; i < 6; ++i) {
      if (m2[i] / (count - 1) > (i < 3 ? 0.001f : 1.0f)) return false;
    }
    result.roll = atan2f(mean[1], mean[2]) * 180.0f / 3.14159265358979323846f;
    result.pitch = atan2f(-mean[0], sqrtf(mean[1] * mean[1] + mean[2] * mean[2])) * 180.0f / 3.14159265358979323846f;
    result.gx = mean[3]; result.gy = mean[4]; result.gz = mean[5];
    return result.valid();
  }
};
