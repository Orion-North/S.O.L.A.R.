from __future__ import annotations

import json
from typing import Any
from urllib import error, parse, request


class RobotHttpError(RuntimeError):
    def __init__(self, status: int, body: str) -> None:
        super().__init__(f"robot HTTP {status}: {body}")
        self.status = status
        self.body = body


class RobotClient:
    """Small stdlib HTTP client for the ESP32 robot API."""

    def __init__(self, base_url: str, api_token: str = "", timeout: float = 2.0) -> None:
        self.base_url = self._normalize_base_url(base_url)
        self.api_token = api_token
        self.timeout = timeout

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
        if self.api_token:
            query["token"] = self.api_token
        encoded = parse.urlencode(query)
        return f"{self.base_url}{clean_path}{'?' + encoded if encoded else ''}"

    def get_bytes(self, path: str, params: dict[str, Any] | None = None) -> bytes:
        url = self._url(path, params)
        try:
            with request.urlopen(url, timeout=self.timeout) as response:
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

    def status(self) -> dict[str, Any]:
        return self.get_json("/status")

    def capture(self) -> bytes:
        return self.get_bytes("/capture")

    def estop(self) -> str:
        return self.get_text("/estop")

    def stand(self) -> str:
        return self.cmd({"mode": "stand", "vx": 0, "vy": 0, "wz": 0})

    def cmd(self, params: dict[str, Any]) -> str:
        return self.get_text("/cmd", params)
