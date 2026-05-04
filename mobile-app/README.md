# SOLAR Mobile App

This is the consumer-facing Android interface. It is intentionally separate from `/control-panel`, which remains the technical operator and calibration panel.

Serve this folder through `/remote-gateway` for remote use. On Android Chrome, open the gateway URL and install the app from the browser menu.

## Production notes

- Serve the gateway over HTTPS.
- Set `GATEWAY_TOKEN`; the app cannot control the robot until the access code passes `/auth/check`.
- Leave `REQUIRE_GATEWAY_TOKEN` enabled in production.
- Put the gateway behind a trusted tunnel, VPS, or reverse proxy that can reach the robot locally.
