from __future__ import annotations

import http.client
import json
import struct
from typing import Any
from urllib import error, parse, request


class RobotHttpError(RuntimeError):
    def __init__(self, status: int, body: str) -> None:
        super().__init__(f"robot HTTP {status}: {body}")
        self.status = status
        self.body = body


class RobotClient:
    """Small stdlib HTTP client for the ESP32 robot API."""

    _IMU_BINARY_FORMAT = "<BBHIII12f"
    _IMU_BINARY_SIZE = struct.calcsize(_IMU_BINARY_FORMAT)
    _OBS_BINARY_FORMAT = "<BBBBIIfIII12f"
    _OBS_BINARY_SIZE = struct.calcsize(_OBS_BINARY_FORMAT)
    _MODE_BY_CODE = {
        1: "stand",
        2: "idle",
        3: "manual",
        4: "walk",
        5: "sit",
        6: "stretch",
        7: "wag",
        8: "dance",
        9: "flip",
        10: "rl",
    }

    def __init__(self, base_url: str, api_token: str = "", timeout: float = 2.0) -> None:
        self.base_url = self._normalize_base_url(base_url)
        self.api_token = api_token
        self.timeout = timeout
        parsed = parse.urlsplit(self.base_url)
        self._scheme = parsed.scheme
        self._netloc = parsed.netloc
        self._base_path = parsed.path.rstrip("/")
        self._connection: http.client.HTTPConnection | http.client.HTTPSConnection | None = None

    @staticmethod
    def _normalize_base_url(base_url: str) -> str:
        value = base_url.strip()
        if not value:
            raise ValueError("base_url is required")
        if not value.lower().startswith(("http://", "https://")):
            value = f"http://{value}"
        return value.rstrip("/")

    def _url(self, path: str, params: dict[str, Any] | None = None) -> str:
        clean_path = path if path.startswith("/") else f"/{path}"
        query: dict[str, Any] = {}
        if params:
            query.update({key: value for key, value in params.items() if value is not None})
        encoded = parse.urlencode(query)
        return f"{self.base_url}{clean_path}{'?' + encoded if encoded else ''}"

    def _request_target(self, path: str, params: dict[str, Any] | None = None) -> str:
        clean_path = path if path.startswith("/") else f"/{path}"
        query: dict[str, Any] = {}
        if params:
            query.update({key: value for key, value in params.items() if value is not None})
        encoded = parse.urlencode(query)
        target = f"{self._base_path}{clean_path}" if self._base_path else clean_path
        return f"{target}{'?' + encoded if encoded else ''}"

    def _headers(self) -> dict[str, str]:
        headers = {"connection": "keep-alive"}
        if self.api_token:
            headers["x-solar-token"] = self.api_token
        return headers

    def _open_connection(self) -> http.client.HTTPConnection | http.client.HTTPSConnection:
        if self._connection is not None:
            return self._connection
        if self._scheme == "https":
            self._connection = http.client.HTTPSConnection(self._netloc, timeout=self.timeout)
        else:
            self._connection = http.client.HTTPConnection(self._netloc, timeout=self.timeout)
        return self._connection

    def close(self) -> None:
        if self._connection is not None:
            self._connection.close()
            self._connection = None

    def _get_bytes_persistent(self, path: str, params: dict[str, Any] | None = None, retry: bool = True) -> bytes:
        conn = self._open_connection()
        try:
            conn.request("GET", self._request_target(path, params), headers=self._headers())
            response = conn.getresponse()
            payload = response.read()
        except (http.client.HTTPException, OSError):
            self.close()
            if retry:
                return self._get_bytes_persistent(path, params, retry=False)
            raise

        if response.getheader("connection", "").lower() == "close":
            self.close()
        if response.status >= 400:
            raise RobotHttpError(response.status, payload.decode("utf-8", errors="replace"))
        return payload

    def get_bytes(self, path: str, params: dict[str, Any] | None = None) -> bytes:
        if self._scheme in {"http", "https"} and self._netloc:
            return self._get_bytes_persistent(path, params)

        url = self._url(path, params)
        req = request.Request(url, headers=self._headers())
        try:
            with request.urlopen(req, timeout=self.timeout) as response:
                return response.read()
        except error.HTTPError as exc:
            body = exc.read().decode("utf-8", errors="replace")
            raise RobotHttpError(exc.code, body) from exc

    def get_text(self, path: str, params: dict[str, Any] | None = None) -> str:
        return self.get_bytes(path, params).decode("utf-8", errors="replace")

    def get_json(self, path: str, params: dict[str, Any] | None = None) -> dict[str, Any]:
        text = self.get_text(path, params)
        data = json.loads(text)
        if not isinstance(data, dict):
            raise ValueError(f"expected JSON object from {path}")
        return data

    def ping(self) -> str:
        return self.get_text("/ping")

    def status(self, fast: bool = False) -> dict[str, Any]:
        return self.get_json("/status", {"fast": 1} if fast else None)

    def imu(self, binary: bool = False) -> dict[str, Any]:
        if binary:
            return self.imu_binary()
        return self.get_json("/imu")

    def imu_binary(self) -> dict[str, Any]:
        payload = self.get_bytes("/imu", {"fmt": "bin"})
        if len(payload) != self._IMU_BINARY_SIZE:
            raise ValueError(f"expected {self._IMU_BINARY_SIZE} bytes from binary IMU, got {len(payload)}")

        unpacked = struct.unpack(self._IMU_BINARY_FORMAT, payload)
        version, flags, rate_hz, seq, sample_ms, age_ms, *values = unpacked
        ax, ay, az, gx, gy, gz, mx, my, mz, roll, pitch, heading = values
        return {
            "version": version,
            "seq": seq,
            "sample_ms": sample_ms,
            "age_ms": age_ms,
            "rate_hz": rate_hz,
            "accel_ready": bool(flags & 0x01),
            "gyro_ready": bool(flags & 0x02),
            "mag_ready": bool(flags & 0x04),
            "bmp180_ready": bool(flags & 0x08),
            "accel_g": [ax, ay, az],
            "gyro_dps": [gx, gy, gz],
            "mag_ut": [mx, my, mz],
            "roll_deg": roll,
            "pitch_deg": pitch,
            "heading_deg": heading,
        }

    def observation_binary(self) -> tuple[dict[str, Any], dict[str, Any]]:
        payload = self.get_bytes("/obs", {"fmt": "bin"})
        if len(payload) != self._OBS_BINARY_SIZE:
            raise ValueError(f"expected {self._OBS_BINARY_SIZE} bytes from binary observation, got {len(payload)}")

        unpacked = struct.unpack(self._OBS_BINARY_FORMAT, payload)
        version, flags, mode_code, _reserved, uptime_ms, last_cmd_ms_ago, solar_voltage, imu_seq, sample_ms, age_ms, *values = unpacked
        ax, ay, az, gx, gy, gz, mx, my, mz, roll, pitch, heading = values
        accel_ready = bool(flags & 0x08)
        gyro_ready = bool(flags & 0x10)
        mag_ready = bool(flags & 0x20)
        bmp_ready = bool(flags & 0x40)
        status = {
            "version": version,
            "mode": self._MODE_BY_CODE.get(mode_code, "unknown"),
            "uptime_ms": uptime_ms,
            "last_cmd_ms_ago": last_cmd_ms_ago,
            "emergency_stop": bool(flags & 0x01),
            "torque_enabled": bool(flags & 0x02),
            "solar_panel_configured": bool(flags & 0x04),
            "solar_panel_voltage_v": solar_voltage if flags & 0x04 else None,
            "imu_ready": accel_ready or gyro_ready or mag_ready,
            "accel_ready": accel_ready,
            "gyro_ready": gyro_ready,
            "mag_ready": mag_ready,
            "bmp180_ready": bmp_ready,
            "imu_seq": imu_seq,
            "imu_age_ms": age_ms,
            "roll_deg": roll,
            "pitch_deg": pitch,
            "heading_deg": heading,
        }
        imu = {
            "version": version,
            "seq": imu_seq,
            "sample_ms": sample_ms,
            "age_ms": age_ms,
            "rate_hz": 50,
            "accel_ready": accel_ready,
            "gyro_ready": gyro_ready,
            "mag_ready": mag_ready,
            "bmp180_ready": bmp_ready,
            "accel_g": [ax, ay, az],
            "gyro_dps": [gx, gy, gz],
            "mag_ut": [mx, my, mz],
            "roll_deg": roll,
            "pitch_deg": pitch,
            "heading_deg": heading,
        }
        return status, imu

    def capture(self) -> bytes:
        return self.get_bytes("/capture")

    def estop(self) -> str:
        return self.get_text("/estop")

    def clear_estop(self) -> str:
        return self.get_text("/estop/clear")

    def stand(self) -> str:
        return self.cmd({"mode": "stand", "vx": 0, "vy": 0, "wz": 0})

    def cmd(self, params: dict[str, Any]) -> str:
        return self.get_text("/cmd", params)

    def rl(self, params: dict[str, Any]) -> str:
        return self.get_text("/rl", params)
