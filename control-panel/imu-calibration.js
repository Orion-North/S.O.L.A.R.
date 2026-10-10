export async function zeroImu(request, progress = () => {}, pause = ms => new Promise(resolve => setTimeout(resolve, ms))) {
  const start = await request('/imu/calibrate', {}, { method: 'POST' });
  if (!start.ok) throw new Error(await start.text() || `Zero rejected (${start.status})`);
  for (let i = 0; i < 40; i++) {
    await pause(250);
    const response = await request('/imu', {}, { cache: 'no-store' });
    if (response.status === 429) continue;
    if (!response.ok) throw new Error(`Calibration status unavailable (${response.status}); check IMU telemetry before retrying`);
    const data = await response.json();
    progress(data);
    if (data.calibration_state === 'complete' && data.calibrated) return data;
    if (data.calibration_state === 'failed') throw new Error(data.calibration_message || 'Calibration failed; previous zero retained');
  }
  throw new Error('Calibration confirmation timed out; check IMU telemetry before retrying');
}

export async function resetImuZero(request) {
  const response = await request('/imu/calibration/reset', {}, { method: 'POST' });
  if (!response.ok) throw new Error(await response.text() || `Reset rejected (${response.status})`);
  return response.json();
}

export function imuZeroLabel(data) {
  if (!data) return 'IMU NO DATA';
  if (data.calibration_state === 'collecting') return `SAMPLING ${data.calibration_samples || 0}/100 · HOLD STILL`;
  if (data.calibration_state === 'saving') return 'ZERO WRITE PENDING';
  if (data.calibration_state === 'failed') return data.calibration_message || 'Calibration failed';
  return data.calibrated ? 'ZERO STORED' : 'ZERO UNSET';
}
