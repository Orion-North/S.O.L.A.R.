from __future__ import annotations

import argparse
import base64
import json
import math
import struct
import time
from http import HTTPStatus
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from typing import Any
from urllib.parse import parse_qs, urlparse


FIRMWARE_VERSION = "sim-rl-solar-voltage-2026-06-04"
IMU_BINARY_FORMAT = "<BBHIII8f"
OBS_BINARY_FORMAT = "<BBBBIIfIII8f"
MOTOR_CHANNELS = [8, 11, 5, 9, 10, 4, 6, 7, 3, 2, 1, 0, 12, 13, 14, 15]
JPEG_1X1 = base64.b64decode(
    "/9j/4AAQSkZJRgABAQAAAQABAAD/2wBDAP//////////////////////////////////////////////////////////////////////////////////////"
    "2wBDAf//////////////////////////////////////////////////////////////////////////////////////wAARCAABAAEDASIAAhEBAxEB/8QAFQABAQ"
    "AAAAAAAAAAAAAAAAAAAAX/xAAUEAEAAAAAAAAAAAAAAAAAAAAA/9oADAMBAAIQAxAAAAH/xAAUEAEAAAAAAAAAAAAAAAAAAAAA/9oACAEBAAEFAqf/"
    "xAAUEQEAAAAAAAAAAAAAAAAAAAAA/9oACAEDAQE/ASP/xAAUEQEAAAAAAAAAAAAAAAAAAAAA/9oACAECAQE/ASP/xAAUEAEAAAAAAAAAAAAAAAAAAAAA/"
    "9oACAEBAAY/Aqf/xAAUEAEAAAAAAAAAAAAAAAAAAAAA/9oACAEBAAE/ISf/2gAMAwEAAgADAAAAEP/EABQRAQAAAAAAAAAAAAAAAAAAABD/2gAIAQ"
    "MBAT8QH//EABQRAQAAAAAAAAAAAAAAAAAAABD/2gAIAQIBAT8QH//EABQQAQAAAAAAAAAAAAAAAAAAABD/2gAIAQEAAT8QH//Z"
)


def clamp(value: float, low: float, high: float) -> float:
    return max(low, min(high, value))


class SimState:
    def __init__(self) -> None:
        self.started_at = time.monotonic()
        self.last_cmd_at = self.started_at
        self.last_capture_at = 0.0
        self.mode = "stand"
        self.vx = 0.0
        self.vy = 0.0
        self.wz = 0.0
        self.speed = 1.0
        self.stride = 30.0
        self.lift = 30.0
        self.torque_enabled = False
        self.calibration_mode = False
        self.emergency_stop = False
        self.flash = False
        self.imu_seq = 1
        self.offsets = [0.0] * 16
        self.leg_sets = {"FL_SET": 1, "FR_SET": 2, "BL_SET": 3, "BR_SET": 4}

    def uptime_ms(self) -> int:
        return int((time.monotonic() - self.started_at) * 1000)

    def last_cmd_ms_ago(self) -> int:
        return int((time.monotonic() - self.last_cmd_at) * 1000)

    def solar_voltage(self) -> float:
        t = time.monotonic() - self.started_at
        return 1.35 + 0.25 * math.sin(t / 6.0)

    def imu(self) -> dict[str, Any]:
        t = time.monotonic() - self.started_at
        roll = 2.0 * math.sin(t / 2.0)
        pitch = 1.5 * math.cos(t / 2.7)
        self.imu_seq += 1
        return {
            "seq": self.imu_seq,
            "sample_ms": self.uptime_ms(),
            "age_ms": 0,
            "rate_hz": 50,
            "accel_ready": True,
            "gyro_ready": True,
            "accel_g": [0.01 * math.sin(t), 0.01 * math.cos(t), 1.0],
            "gyro_dps": [0.2 * self.vx, 0.2 * self.vy, 8.0 * self.wz],
            "roll_deg": roll,
            "pitch_deg": pitch,
        }


class SolarSimHandler(BaseHTTPRequestHandler):
    protocol_version = "HTTP/1.1"
    server_version = "SolarEsp32Sim/1.0"

    @property
    def state(self) -> SimState:
        return self.server.state  # type: ignore[attr-defined]

    @property
    def api_token(self) -> str:
        return self.server.api_token  # type: ignore[attr-defined]

    def log_message(self, fmt: str, *args: object) -> None:
        if self.server.verbose:  # type: ignore[attr-defined]
            super().log_message(fmt, *args)

    def do_OPTIONS(self) -> None:
        self.send_response(HTTPStatus.NO_CONTENT)
        self.send_cors()
        self.end_headers()

    def do_POST(self) -> None:
        self.do_GET()

    def do_GET(self) -> None:
        parsed = urlparse(self.path)
        params = {key: values[-1] for key, values in parse_qs(parsed.query).items()}
        path = parsed.path

        if path in {"/", "/version"}:
            body = FIRMWARE_VERSION if path == "/version" else f"Robot Online. FW: {FIRMWARE_VERSION}"
            self.send_text(HTTPStatus.OK, body)
            return

        if not self.authorized(params):
            self.send_text(HTTPStatus.FORBIDDEN, "Forbidden")
            return

        routes = {
            "/ping": self.handle_ping,
            "/debug": self.handle_debug,
            "/status": self.handle_status,
            "/obs": self.handle_obs,
            "/imu": self.handle_imu,
            "/capture": self.handle_capture,
            "/cmd": self.handle_cmd,
            "/rl": self.handle_rl,
            "/torque": self.handle_torque,
            "/calib": self.handle_calib,
            "/test": self.handle_test,
            "/testseq": self.handle_testseq,
            "/seq": self.handle_seq,
            "/settings/get": self.handle_settings_get,
            "/settings/set": self.handle_settings_set,
            "/flash": self.handle_flash,
            "/flash/auto": self.handle_flash_auto,
            "/charge-rest": self.handle_charge_rest,
            "/estop": self.handle_estop,
            "/estop/clear": self.handle_estop_clear,
            "/i2c": self.handle_i2c,
        }
        handler = routes.get(path)
        if handler is None:
            self.send_text(HTTPStatus.NOT_FOUND, "Not found")
            return
        handler(params)

    def authorized(self, params: dict[str, str]) -> bool:
        if not self.api_token:
            return True
        return params.get("token") == self.api_token or self.headers.get("x-solar-token") == self.api_token

    def send_cors(self) -> None:
        self.send_header("Access-Control-Allow-Origin", "*")
        self.send_header("Access-Control-Allow-Headers", "x-solar-token,content-type")
        self.send_header("Access-Control-Allow-Methods", "GET,POST,OPTIONS")

    def send_text(self, status: HTTPStatus, body: str) -> None:
        payload = body.encode("utf-8")
        self.send_response(status)
        self.send_cors()
        self.send_header("Content-Type", "text/plain; charset=utf-8")
        self.send_header("Content-Length", str(len(payload)))
        self.send_header("Cache-Control", "no-store")
        self.end_headers()
        self.wfile.write(payload)

    def send_json(self, data: dict[str, Any]) -> None:
        payload = json.dumps(data, separators=(",", ":")).encode("utf-8")
        self.send_response(HTTPStatus.OK)
        self.send_cors()
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(payload)))
        self.send_header("Cache-Control", "no-store")
        self.end_headers()
        self.wfile.write(payload)

    def send_binary(self, payload: bytes, content_type: str = "application/octet-stream") -> None:
        self.send_response(HTTPStatus.OK)
        self.send_cors()
        self.send_header("Content-Type", content_type)
        self.send_header("Content-Length", str(len(payload)))
        self.send_header("Cache-Control", "no-store")
        self.end_headers()
        self.wfile.write(payload)

    def base_status(self) -> dict[str, Any]:
        imu = self.state.imu()
        return {
            "mode": self.state.mode,
            "uptime_ms": self.state.uptime_ms(),
            "free_heap": 244000,
            "last_cmd_ms_ago": self.state.last_cmd_ms_ago(),
            "gait_hz": 50,
            "camera_fps_limit": 4 if self.state.mode == "walk" else 10,
            "stride": self.state.stride,
            "lift": self.state.lift,
            "emergency_stop": self.state.emergency_stop,
            "torque_enabled": self.state.torque_enabled,
            "wifi_clients": 1,
            "wifi_mode": "home_wifi_ap_fallback",
            "home_wifi_configured": True,
            "home_wifi_status": "CONNECTED",
            "home_wifi_rssi": -48,
            "ip": "127.0.0.1",
            "ap_ip": "192.168.4.1",
            "solar_panel_configured": True,
            "solar_panel_adc_pin": 34,
            "solar_panel_adc_mv": int(self.state.solar_voltage() * 1000 / 2.0),
            "solar_panel_voltage_v": round(self.state.solar_voltage(), 3),
            "imu_ready": True,
            "accel_ready": imu["accel_ready"],
            "gyro_ready": imu["gyro_ready"],
            "mpu6050_addr": 104,
            "accel_addr": 104,
            "imu_seq": imu["seq"],
            "imu_age_ms": imu["age_ms"],
            "gyro_dps": imu["gyro_dps"],
            "accel_g": imu["accel_g"],
            "roll_deg": round(imu["roll_deg"], 1),
            "pitch_deg": round(imu["pitch_deg"], 1),
        }

    def handle_ping(self, params: dict[str, str]) -> None:
        self.state.last_cmd_at = time.monotonic()
        self.send_text(HTTPStatus.OK, "PONG")

    def handle_debug(self, params: dict[str, str]) -> None:
        self.send_text(
            HTTPStatus.OK,
            "\n".join(
                [
                    "OS Boot OK",
                    f"FW: {FIRMWARE_VERSION}",
                    f"Mode: {self.state.mode}",
                    f"Torque: {'ON' if self.state.torque_enabled else 'OFF'}",
                    f"Calibration: {'ON' if self.state.calibration_mode else 'OFF'}",
                    "Free Heap: 244000",
                ]
            ),
        )

    def handle_status(self, params: dict[str, str]) -> None:
        if params.get("fast") == "1":
            imu = self.state.imu()
            self.send_json(
                {
                    "mode": self.state.mode,
                    "uptime_ms": self.state.uptime_ms(),
                    "last_cmd_ms_ago": self.state.last_cmd_ms_ago(),
                    "emergency_stop": self.state.emergency_stop,
                    "torque_enabled": self.state.torque_enabled,
                    "solar_panel_configured": True,
                    "solar_panel_voltage_v": round(self.state.solar_voltage(), 3),
                    "imu_ready": True,
                    "accel_ready": imu["accel_ready"],
                    "gyro_ready": imu["gyro_ready"],
                    "imu_seq": imu["seq"],
                    "imu_age_ms": imu["age_ms"],
                    "roll_deg": round(imu["roll_deg"], 1),
                    "pitch_deg": round(imu["pitch_deg"], 1),
                }
            )
            return
        self.send_json(self.base_status())

    def handle_obs(self, params: dict[str, str]) -> None:
        if params.get("fmt") == "bin":
            imu = self.state.imu()
            payload = struct.pack(
                OBS_BINARY_FORMAT,
                2,
                0x04 | 0x08 | 0x10 | (0x01 if self.state.emergency_stop else 0) | (0x02 if self.state.torque_enabled else 0),
                mode_code(self.state.mode),
                0,
                self.state.uptime_ms(),
                self.state.last_cmd_ms_ago(),
                self.state.solar_voltage(),
                imu["seq"],
                imu["sample_ms"],
                imu["age_ms"],
                *imu["accel_g"],
                *imu["gyro_dps"],
                imu["roll_deg"],
                imu["pitch_deg"],
            )
            self.send_binary(payload)
            return
        status = self.base_status()
        self.send_json(
            {
                "mode": status["mode"],
                "uptime_ms": status["uptime_ms"],
                "last_cmd_ms_ago": status["last_cmd_ms_ago"],
                "emergency_stop": status["emergency_stop"],
                "torque_enabled": status["torque_enabled"],
                "solar_panel_configured": status["solar_panel_configured"],
                "solar_panel_voltage_v": status["solar_panel_voltage_v"],
                "imu_seq": status["imu_seq"],
                "imu_age_ms": status["imu_age_ms"],
            }
        )

    def handle_imu(self, params: dict[str, str]) -> None:
        imu = self.state.imu()
        if params.get("fmt") == "bin":
            payload = struct.pack(
                IMU_BINARY_FORMAT,
                2,
                0x01 | 0x02,
                50,
                imu["seq"],
                imu["sample_ms"],
                imu["age_ms"],
                *imu["accel_g"],
                *imu["gyro_dps"],
                imu["roll_deg"],
                imu["pitch_deg"],
            )
            self.send_binary(payload)
            return
        self.send_json(imu)

    def handle_capture(self, params: dict[str, str]) -> None:
        now = time.monotonic()
        fps_limit = 4 if self.state.mode == "walk" else 10
        if now - self.state.last_capture_at < 1.0 / fps_limit:
            self.send_text(HTTPStatus.TOO_MANY_REQUESTS, "Rate Limited")
            return
        self.state.last_capture_at = now
        self.send_binary(JPEG_1X1, "image/jpeg")

    def handle_cmd(self, params: dict[str, str]) -> None:
        if self.state.emergency_stop:
            self.send_text(HTTPStatus.LOCKED, "Emergency stop active")
            return
        mode = params.get("mode")
        if mode is not None:
            if mode not in {"stand", "idle", "manual", "walk", "sit", "stretch", "wag", "dance", "flip", "wave", "rl"}:
                self.send_text(HTTPStatus.BAD_REQUEST, "Invalid mode")
                return
            self.state.mode = mode
        self.state.vx = clamp(float(params.get("vx", self.state.vx)), -1.0, 1.0)
        self.state.vy = clamp(float(params.get("vy", self.state.vy)), -1.0, 1.0)
        self.state.wz = clamp(float(params.get("wz", self.state.wz)), -1.0, 1.0)
        self.state.speed = clamp(float(params.get("speed", self.state.speed)), 0.1, 3.0)
        self.state.stride = clamp(float(params.get("stride", self.state.stride)), 0.0, 60.0)
        self.state.lift = clamp(float(params.get("lift", self.state.lift)), 0.0, 60.0)
        self.state.last_cmd_at = time.monotonic()
        self.send_text(HTTPStatus.OK, "CMD ACK")

    def handle_rl(self, params: dict[str, str]) -> None:
        if self.state.emergency_stop:
            self.send_text(HTTPStatus.LOCKED, "Emergency stop active")
            return
        if not self.state.torque_enabled:
            self.send_text(HTTPStatus.CONFLICT, "Torque disabled")
            return
        actions = params.get("a", "").split(",") if "a" in params else [params.get(f"a{i}", "") for i in range(12)]
        if len(actions) != 12 or any(value == "" for value in actions):
            self.send_text(HTTPStatus.BAD_REQUEST, "Expected 12 RL actions as a CSV 'a' argument or a0..a11")
            return
        for value in actions:
            clamp(float(value), -1.0, 1.0)
        self.state.mode = "rl"
        self.state.last_cmd_at = time.monotonic()
        self.send_text(HTTPStatus.OK, "RL ACK")

    def handle_torque(self, params: dict[str, str]) -> None:
        if "state" in params:
            requested = params["state"] == "1"
            if requested and self.state.emergency_stop:
                self.send_text(HTTPStatus.LOCKED, "Emergency stop active; call /estop/clear first")
                return
            self.state.torque_enabled = requested
            if requested:
                self.state.calibration_mode = False
                self.state.mode = "stand"
        self.send_text(HTTPStatus.OK, "TORQUE ENGAGED" if self.state.torque_enabled else "TORQUE DISABLED")

    def handle_calib(self, params: dict[str, str]) -> None:
        if self.state.emergency_stop:
            self.send_text(HTTPStatus.LOCKED, "Emergency stop active")
            return
        if "state" in params:
            self.state.calibration_mode = params["state"] == "1"
            self.state.torque_enabled = self.state.calibration_mode
            self.state.mode = "manual" if self.state.calibration_mode else "stand"
        self.send_text(HTTPStatus.OK, "CALIB ON" if self.state.calibration_mode else "CALIB OFF")

    def handle_test(self, params: dict[str, str]) -> None:
        if self.state.emergency_stop:
            self.send_text(HTTPStatus.LOCKED, "Emergency stop active")
            return
        self.state.mode = "manual"
        self.state.torque_enabled = True
        self.state.last_cmd_at = time.monotonic()
        self.send_text(HTTPStatus.OK, "TEST ACK")

    def handle_testseq(self, params: dict[str, str]) -> None:
        if self.state.emergency_stop:
            self.send_text(HTTPStatus.LOCKED, "Emergency stop active")
            return
        self.send_text(HTTPStatus.OK, "TEST SEQ START")

    def handle_seq(self, params: dict[str, str]) -> None:
        if "t" not in params:
            self.send_text(HTTPStatus.BAD_REQUEST, "Missing t")
            return
        if self.state.emergency_stop:
            self.send_text(HTTPStatus.LOCKED, "Emergency stop active")
            return
        self.state.mode = "manual"
        self.state.last_cmd_at = time.monotonic()
        self.send_text(HTTPStatus.OK, "SEQ ACK")

    def handle_settings_get(self, params: dict[str, str]) -> None:
        self.send_json({**self.state.leg_sets, "offsets": self.state.offsets, "motorChannels": MOTOR_CHANNELS})

    def handle_settings_set(self, params: dict[str, str]) -> None:
        for key in self.state.leg_sets:
            if key in params:
                self.state.leg_sets[key] = int(clamp(int(params[key]), 1, 4))
        for i in range(16):
            key = f"o{i}"
            if key in params:
                self.state.offsets[i] = float(params[key])
        self.send_text(HTTPStatus.OK, "SAVED TO NVS")

    def handle_flash(self, params: dict[str, str]) -> None:
        self.state.flash = params.get("state") == "1"
        self.send_text(HTTPStatus.OK, f"FLASH {1 if self.state.flash else 0}")

    def handle_flash_auto(self, params: dict[str, str]) -> None:
        self.state.flash = False
        self.send_text(HTTPStatus.OK, "FLASH AUTO")

    def handle_charge_rest(self, params: dict[str, str]) -> None:
        if self.state.emergency_stop:
            self.send_text(HTTPStatus.LOCKED, "Emergency stop active; call /estop/clear first")
            return
        self.state.mode = "charge_rest"
        self.state.vx = 0.0
        self.state.vy = 0.0
        self.state.wz = 0.0
        self.state.calibration_mode = False
        self.state.torque_enabled = False
        self.state.last_cmd_at = time.monotonic()
        self.send_text(HTTPStatus.OK, "CHARGE REST STARTED")

    def handle_estop(self, params: dict[str, str]) -> None:
        self.state.emergency_stop = True
        self.state.torque_enabled = False
        self.state.calibration_mode = False
        self.state.mode = "stand"
        self.state.last_cmd_at = time.monotonic()
        self.send_text(HTTPStatus.OK, "EMERGENCY STOP")

    def handle_estop_clear(self, params: dict[str, str]) -> None:
        self.state.emergency_stop = False
        self.state.torque_enabled = False
        self.state.calibration_mode = False
        self.state.mode = "stand"
        self.state.last_cmd_at = time.monotonic()
        self.send_text(HTTPStatus.OK, "EMERGENCY STOP CLEARED")

    def handle_i2c(self, params: dict[str, str]) -> None:
        self.send_json(
            {
                "imu_bus": "simulated",
                "imu_sda_pin": 13,
                "imu_scl_pin": 2,
                "imu_pins_scan": "I2C SCAN IMU PINS 13/2: 0x68",
                "shared_pins_scan": "I2C SCAN SHARED PINS 14/15: 0x40",
                "mpu6050_addr": 104,
            }
        )


def mode_code(mode: str) -> int:
    return {
        "stand": 1,
        "idle": 2,
        "manual": 3,
        "walk": 4,
        "sit": 5,
        "stretch": 6,
        "wag": 7,
        "dance": 8,
        "flip": 9,
        "rl": 10,
        "wave": 11,
        "charge_rest": 12,
    }.get(mode, 0)


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description="Run a local simulator for the SOLAR ESP32 HTTP API.")
    parser.add_argument("--host", default="127.0.0.1")
    parser.add_argument("--port", type=int, default=8081)
    parser.add_argument("--api-token", default="")
    parser.add_argument("--verbose", action="store_true")
    return parser


def main() -> int:
    args = build_parser().parse_args()
    server = ThreadingHTTPServer((args.host, args.port), SolarSimHandler)
    server.state = SimState()  # type: ignore[attr-defined]
    server.api_token = args.api_token  # type: ignore[attr-defined]
    server.verbose = args.verbose  # type: ignore[attr-defined]
    print(f"SOLAR ESP32 simulator listening on http://{args.host}:{args.port}", flush=True)
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        pass
    finally:
        server.server_close()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
