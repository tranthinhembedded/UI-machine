"""Runtime ports for the standalone UI package.

DemoRuntime keeps this repository reviewable without a camera or detector.
HttpRuntime connects the Qt UI to an existing TNM Vision runtime server.
"""

from __future__ import annotations

import json
import threading
import time
from collections import OrderedDict
from typing import Protocol
from urllib.error import HTTPError, URLError
from urllib.request import Request, urlopen

from PySide6.QtCore import QBuffer, QByteArray, QIODevice, QRect
from PySide6.QtGui import QColor, QFont, QImage, QPainter, QPen


class RuntimePort(Protocol):
    supports_camera: bool

    def start(self, source: str) -> None: ...
    def stop(self) -> None: ...
    def close(self) -> None: ...
    def snapshot(self) -> dict: ...
    def image(self, kind: str, key: int) -> bytes | None: ...


def _jpeg(image: QImage, quality: int = 78) -> bytes:
    data = QByteArray()
    buffer = QBuffer(data)
    buffer.open(QIODevice.OpenModeFlag.WriteOnly)
    image.save(buffer, "JPG", quality)
    buffer.close()
    return bytes(data)


class DemoRuntime:
    """Small bounded simulator that exercises every important HMI state."""

    supports_camera = False

    def __init__(self, *, fps: int = 5, width: int = 640) -> None:
        if not 1 <= fps <= 15 or not 320 <= width <= 1280:
            raise ValueError("FPS phải từ 1–15 và chiều rộng từ 320–1280 px.")
        self.fps = fps
        self.width = width
        self.lock = threading.Lock()
        self.stop_event = threading.Event()
        self.worker: threading.Thread | None = None
        self.state = "idle"
        self.source = "demo"
        self.error = ""
        self.sequence = 0
        self.processed = 0
        self.event_total = 0
        self.updated = 0.0
        self.processing_ms = 0.0
        self.actual_fps = 0.0
        self.frames: OrderedDict[int, bytes] = OrderedDict()
        self.events: OrderedDict[int, tuple[dict, bytes, bytes]] = OrderedDict()

    def start(self, source: str) -> None:
        if source == "camera":
            raise ValueError(
                "Camera cần dịch vụ detect. Hãy chạy với --api-url hoặc dùng nguồn mô phỏng."
            )
        if source != "demo":
            raise ValueError("Nguồn ảnh không hợp lệ.")
        with self.lock:
            if self.worker and self.worker.is_alive():
                raise ValueError("Phiên kiểm tra vẫn đang chạy hoặc đang dừng.")
            self.stop_event.clear()
            self.state, self.source, self.error = "starting", source, ""
            self.processed = self.event_total = 0
            self.updated = self.actual_fps = self.processing_ms = 0.0
            self.frames.clear()
            self.events.clear()
            self.worker = threading.Thread(target=self._run, daemon=True)
            self.worker.start()

    def stop(self) -> None:
        with self.lock:
            if self.worker and self.worker.is_alive():
                self.state = "stopping"
                self.stop_event.set()

    def close(self) -> None:
        self.stop()
        if self.worker:
            self.worker.join(timeout=3)

    def snapshot(self) -> dict:
        with self.lock:
            age = time.monotonic() - self.updated if self.updated else None
            return {
                "state": self.state,
                "source": self.source,
                "error": self.error,
                "sequence": self.sequence,
                "has_frame": bool(self.frames),
                "processed": self.processed,
                "event_total": self.event_total,
                "processing_ms": round(self.processing_ms, 1),
                "fps": round(self.actual_fps, 1),
                "target_fps": self.fps,
                "age_ms": round(age * 1000) if age is not None else None,
                "stale": self.state == "running" and (age is None or age > 3),
                "detector": "UI demo",
                "max_width": self.width,
                "events": [value[0] for value in reversed(self.events.values())],
            }

    def image(self, kind: str, key: int) -> bytes | None:
        with self.lock:
            if kind == "frame":
                return self.frames.get(key)
            event = self.events.get(key)
            if not event:
                return None
            return event[1 if kind == "crop" else 2]

    def _run(self) -> None:
        last_event = -float("inf")
        previous = time.monotonic()
        try:
            while not self.stop_event.is_set():
                tick = time.monotonic()
                clean, annotated, crop = self._render(self.processed)
                del clean
                now = time.monotonic()
                event = crop if crop and now - last_event >= 2 else None
                if event:
                    last_event = now
                completed = time.monotonic()
                with self.lock:
                    if self.stop_event.is_set():
                        break
                    self.sequence += 1
                    self.processed += 1
                    frame_bytes = _jpeg(annotated)
                    self.frames[self.sequence] = frame_bytes
                    while len(self.frames) > 3:
                        self.frames.popitem(last=False)
                    if event:
                        self.event_total += 1
                        metadata = {
                            "id": self.sequence,
                            "time": time.strftime("%H:%M:%S"),
                            "regions": 1,
                            "width": event.width(),
                            "height": event.height(),
                            "frame": self.processed,
                            "source": "demo",
                        }
                        self.events[self.sequence] = (
                            metadata,
                            _jpeg(event),
                            frame_bytes,
                        )
                        while len(self.events) > 24:
                            self.events.popitem(last=False)
                    self.processing_ms = (completed - tick) * 1000
                    self.actual_fps = (
                        1 / max(completed - previous, 0.001) if self.processed > 1 else 0
                    )
                    self.updated, self.state = completed, "running"
                previous = completed
                self.stop_event.wait(max(0, 1 / self.fps - (time.monotonic() - tick)))
        except Exception as error:
            with self.lock:
                self.error, self.state = str(error), "error"
        finally:
            with self.lock:
                if self.state != "error":
                    self.state = "stopped"

    def _render(self, index: int) -> tuple[QImage, QImage, QImage | None]:
        height = round(self.width * 0.625)
        image = QImage(self.width, height, QImage.Format.Format_RGB32)
        image.fill(QColor("#B9BAB6"))
        painter = QPainter(image)
        shift = index % 8
        for x in range(-shift, self.width, 6):
            painter.setPen(QPen(QColor("#777A76"), 2))
            painter.drawLine(x, 0, x, height)
            painter.setPen(QPen(QColor("#D9D9D5"), 1))
            painter.drawLine(x + 3, 0, x + 3, height)
        for y in range(0, height, 5):
            painter.setPen(QPen(QColor("#8C8E89"), 1))
            painter.drawLine(0, y, self.width, y)
            painter.setPen(QPen(QColor("#D0D1CC"), 1))
            painter.drawLine(0, y + 2, self.width, y + 2)
        has_region = (index // 20) % 2 == 0
        region = QRect(
            round(self.width * 0.31),
            round(height * 0.50),
            round(self.width * 0.38),
            max(12, round(height * 0.045)),
        )
        if has_region:
            painter.setPen(QPen(QColor("#F7F8F6"), max(4, round(height * 0.014))))
            painter.drawLine(
                region.left() + 8,
                region.center().y(),
                region.right() - 8,
                region.center().y(),
            )
        painter.end()

        annotated = image.copy()
        crop = None
        if has_region:
            painter = QPainter(annotated)
            painter.setPen(QPen(QColor("#00A5B1"), 3))
            painter.drawRect(region)
            painter.fillRect(region.left(), region.top() - 18, 25, 18, QColor("#005E65"))
            painter.setPen(QColor("white"))
            painter.setFont(QFont("Segoe UI", 8, QFont.Weight.Bold))
            painter.drawText(region.left() + 5, region.top() - 5, "#1")
            painter.end()
            padded = region.adjusted(-24, -28, 24, 28).intersected(image.rect())
            crop = image.copy(padded)
        return image, annotated, crop


class HttpRuntime:
    """Adapter for the localhost HTTP contract exposed by TNM Vision."""

    supports_camera = True

    def __init__(self, base_url: str, *, timeout: float = 0.4) -> None:
        self.base_url = base_url.rstrip("/") + "/"
        self.timeout = timeout
        self._last = self._offline("Đang kết nối với dịch vụ detect…")

    def _request(self, route: str, method: str = "GET", payload: dict | None = None) -> bytes:
        body = json.dumps(payload).encode("utf-8") if payload is not None else None
        request = Request(
            self.base_url + route,
            data=body,
            method=method,
            headers={"Content-Type": "application/json"},
        )
        with urlopen(request, timeout=self.timeout) as response:
            return response.read()

    def start(self, source: str) -> None:
        try:
            self._request("api/start", "POST", {"source": source})
        except (HTTPError, URLError, TimeoutError, json.JSONDecodeError) as error:
            raise ValueError(f"Không gửi được lệnh bắt đầu: {error}") from error

    def stop(self) -> None:
        try:
            self._request("api/stop", "POST", {})
        except (HTTPError, URLError, TimeoutError):
            pass

    def close(self) -> None:
        pass

    def snapshot(self) -> dict:
        try:
            self._last = json.loads(self._request("api/state"))
        except (HTTPError, URLError, TimeoutError, json.JSONDecodeError) as error:
            self._last = self._offline(f"Mất kết nối dịch vụ detect: {error}")
        return self._last

    def image(self, kind: str, key: int) -> bytes | None:
        try:
            return self._request(f"image/{kind}/{key}")
        except (HTTPError, URLError, TimeoutError):
            return None

    @staticmethod
    def _offline(message: str) -> dict:
        return {
            "state": "error",
            "source": "demo",
            "error": message,
            "sequence": 0,
            "has_frame": False,
            "processed": 0,
            "event_total": 0,
            "processing_ms": 0.0,
            "fps": 0.0,
            "target_fps": 0,
            "age_ms": None,
            "stale": False,
            "detector": "chưa kết nối",
            "max_width": 0,
            "events": [],
        }
