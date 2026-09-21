from __future__ import annotations

import time

import pytest
from PySide6.QtWidgets import QApplication

from ui_machine.runtime import DemoRuntime, HttpRuntime

APP = QApplication.instance() or QApplication([])


def wait_for(predicate, timeout: float = 4) -> None:
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        if predicate():
            return
        time.sleep(0.02)
    pytest.fail("Runtime did not reach the expected state")


def test_demo_runtime_produces_bounded_preview_and_event() -> None:
    runtime = DemoRuntime(fps=15, width=320)
    try:
        runtime.start("demo")
        wait_for(lambda: runtime.snapshot()["event_total"] > 0)
        state = runtime.snapshot()
        event = state["events"][0]
        assert state["state"] == "running"
        assert state["processed"] > 0
        assert runtime.image("frame", state["sequence"]).startswith(b"\xff\xd8")
        assert runtime.image("crop", event["id"]).startswith(b"\xff\xd8")
        assert runtime.image("context", event["id"]).startswith(b"\xff\xd8")
        runtime.stop()
        wait_for(lambda: runtime.snapshot()["state"] == "stopped")
    finally:
        runtime.close()


def test_demo_runtime_rejects_camera_without_backend() -> None:
    runtime = DemoRuntime()
    with pytest.raises(ValueError, match="--api-url"):
        runtime.start("camera")


def test_http_runtime_reports_unreachable_backend() -> None:
    runtime = HttpRuntime("http://127.0.0.1:1/token/", timeout=0.05)
    state = runtime.snapshot()
    assert state["state"] == "error"
    assert "Mất kết nối" in state["error"]
