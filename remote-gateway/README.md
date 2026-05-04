# SOLAR Remote Gateway

This optional gateway keeps the ESP32 firmware local and exposes the same robot API through one authenticated remote endpoint.

## Local run

```powershell
$env:ROBOT_BASE_URL="http://solar.local"
$env:ROBOT_API_TOKEN="robot-api-token-if-enabled"
$env:GATEWAY_TOKEN="remote-app-access-code"
npm start
```

Then open `http://localhost:8787` or publish that gateway through a secure tunnel/VPS/reverse proxy. The Android-installable control panel can use the gateway host as its target.

## Environment

- `ROBOT_BASE_URL`: local robot URL, usually `http://solar.local` or `http://192.168.4.1`
- `ROBOT_API_TOKEN`: token configured in `firmware/solar_main/secrets.h`, if command-token checks are enabled
- `GATEWAY_TOKEN`: access code required by the mobile app for remote control
- `PORT`: gateway port, default `8787`
- `MOBILE_APP_DIR`: static app directory, default `../mobile-app`
- `CONTROL_PANEL_DIR`: legacy override for serving the technical panel instead
