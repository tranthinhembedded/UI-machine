"""Native Qt Widgets HMI for TNM Vision."""

from __future__ import annotations

import argparse
import sys
from importlib.resources import files
from pathlib import Path

from PySide6.QtCore import QByteArray, QSize, Qt, QTimer
from PySide6.QtGui import QFont, QKeySequence, QPixmap, QShortcut
from PySide6.QtWidgets import (
    QApplication,
    QComboBox,
    QFrame,
    QHBoxLayout,
    QLabel,
    QListWidget,
    QListWidgetItem,
    QMainWindow,
    QPushButton,
    QSizePolicy,
    QSplitter,
    QVBoxLayout,
    QWidget,
)

from ui_machine.runtime import DemoRuntime, HttpRuntime, RuntimePort

INK = "#17262B"
MUTED = "#5E7075"
LINE = "#C9D1D3"
CANVAS = "#E9EDEE"
SURFACE = "#F9FAFA"
WELL = "#15252A"
TEAL = "#007E87"
RED = "#A12E33"

STYLE = f"""
* {{ font-family: "Segoe UI", "Noto Sans", sans-serif; color: {INK}; }}
QMainWindow, QWidget#root {{ background: {CANVAS}; }}
QFrame#header {{ background: {WELL}; border: none; }}
QLabel#brandMark {{
  background: {TEAL}; color: white; border: 1px solid #66ADB2;
  border-radius: 4px; font-size: 17px; font-weight: 700;
}}
QLabel#brandName {{ color: white; font-size: 17px; font-weight: 650; }}
QLabel#brandSub, QLabel#headerMeta {{ color: #B8C5C8; font-size: 12px; }}
QLabel#pageTitle {{ font-size: 25px; font-weight: 650; }}
QLabel#pageSub, QLabel[class="muted"] {{ color: {MUTED}; font-size: 13px; }}
QFrame[class="card"], QFrame#controls {{
  background: {SURFACE}; border: 1px solid {LINE}; border-radius: 5px;
}}
QLabel[class="sectionTitle"] {{ font-size: 15px; font-weight: 650; }}
QLabel[class="fieldLabel"] {{ font-size: 12px; font-weight: 600; }}
QComboBox {{
  background: white; border: 1px solid #AEBABD; border-radius: 4px;
  min-height: 40px; padding: 0 12px; font-size: 13px;
}}
QComboBox:focus, QPushButton:focus {{ border: 2px solid {TEAL}; }}
QPushButton {{
  background: #F4F6F6; border: 1px solid #AEBABD; border-radius: 4px;
  min-height: 40px; padding: 0 16px; font-size: 13px; font-weight: 600;
}}
QPushButton:hover {{ background: #E8EDEE; border-color: #87989C; }}
QPushButton:pressed {{ background: #DCE3E4; }}
QPushButton:disabled {{ color: #929EA1; background: #EEF1F1; border-color: #D4DADB; }}
QPushButton#startButton {{ background: {TEAL}; color: white; border-color: #005E65; }}
QPushButton#startButton:hover {{ background: #005E65; }}
QPushButton#stopButton {{ background: white; color: {RED}; border-color: #C58B8E; }}
QPushButton#headerButton {{
  background: #23383E; color: white; border-color: #60777C;
  min-height: 34px; padding: 0 12px;
}}
QLabel#imageWell, QLabel#contextWell {{
  background: {WELL}; color: #D6E0E2; border: none;
}}
QLabel#cropWell {{
  background: #E8ECEC; color: {MUTED}; border: 1px solid {LINE};
}}
QLabel[class="statLabel"] {{ color: {MUTED}; font-size: 11px; }}
QLabel[class="statValue"] {{ font-size: 23px; font-weight: 600; }}
QLabel#message {{
  background: white; border: 1px solid {LINE}; border-left: 4px solid {TEAL};
  border-radius: 4px; padding: 10px 12px; font-size: 13px;
}}
QLabel#message[tone="bad"] {{
  color: {RED}; border-left-color: {RED}; background: #FFF9F9;
}}
QLabel[class="badge"] {{
  border-radius: 4px; padding: 4px 8px; font-size: 11px; font-weight: 650;
}}
QLabel[class="badge"][tone="neutral"] {{
  color: #405156; background: #E9EDEE; border: 1px solid #C7D0D2;
}}
QLabel[class="badge"][tone="good"] {{
  color: #236A4B; background: #E5F0E9; border: 1px solid #AAC9B8;
}}
QLabel[class="badge"][tone="warn"] {{
  color: #8C5A12; background: #F5EBD7; border: 1px solid #D6BF95;
}}
QLabel[class="badge"][tone="bad"] {{
  color: {RED}; background: #F6E3E4; border: 1px solid #D4A1A4;
}}
QListWidget {{ background: {SURFACE}; border: none; outline: none; padding: 4px; }}
QListWidget::item {{
  border: 1px solid transparent; border-radius: 3px;
  padding: 8px 10px; min-height: 44px;
}}
QListWidget::item:selected {{
  color: {INK}; background: #E2F1F2; border-color: {TEAL};
}}
QSplitter::handle {{ background: transparent; width: 10px; }}
"""


def repolish(widget: QWidget) -> None:
    widget.style().unpolish(widget)
    widget.style().polish(widget)
    widget.update()


def text_label(text: str, css_class: str = "") -> QLabel:
    result = QLabel(text)
    if css_class:
        result.setProperty("class", css_class)
    return result


def separator() -> QFrame:
    result = QFrame()
    result.setFrameShape(QFrame.Shape.HLine)
    result.setStyleSheet(f"background:{LINE}; max-height:1px; border:none;")
    return result


def make_card() -> tuple[QFrame, QVBoxLayout]:
    result = QFrame()
    result.setProperty("class", "card")
    layout = QVBoxLayout(result)
    layout.setContentsMargins(0, 0, 0, 0)
    layout.setSpacing(0)
    return result, layout


def section_header(title: str, trailing: QWidget) -> QWidget:
    result = QWidget()
    layout = QHBoxLayout(result)
    layout.setContentsMargins(16, 12, 16, 12)
    layout.addWidget(text_label(title, "sectionTitle"))
    layout.addStretch(1)
    layout.addWidget(trailing)
    return result


class StatusBadge(QLabel):
    def __init__(self, text: str, tone: str = "neutral") -> None:
        super().__init__()
        self.setProperty("class", "badge")
        self.setAlignment(Qt.AlignmentFlag.AlignCenter)
        self.set_status(text, tone)

    def set_status(self, text: str, tone: str = "neutral") -> None:
        self.setText(text)
        self.setProperty("tone", tone)
        repolish(self)


class ImageView(QLabel):
    def __init__(self, name: str, empty_text: str, minimum_height: int) -> None:
        super().__init__(empty_text)
        self.setObjectName(name)
        self.setAlignment(Qt.AlignmentFlag.AlignCenter)
        self.setWordWrap(True)
        self.setMinimumHeight(minimum_height)
        self.setSizePolicy(QSizePolicy.Policy.Expanding, QSizePolicy.Policy.Expanding)
        self.source = QPixmap()

    def set_bytes(self, data: bytes | None) -> bool:
        pixmap = QPixmap()
        if not data or not pixmap.loadFromData(QByteArray(data), "JPG"):
            return False
        self.source = pixmap
        self._fit()
        return True

    def clear_image(self, text: str) -> None:
        self.source = QPixmap()
        self.setPixmap(QPixmap())
        self.setText(text)

    def resizeEvent(self, event) -> None:  # noqa: N802
        super().resizeEvent(event)
        self._fit()

    def _fit(self) -> None:
        if self.source.isNull() or self.width() < 2 or self.height() < 2:
            return
        self.setText("")
        self.setPixmap(
            self.source.scaled(
                self.size(),
                Qt.AspectRatioMode.KeepAspectRatio,
                Qt.TransformationMode.SmoothTransformation,
            )
        )


class RuntimeWindow(QMainWindow):
    state_names = {
        "idle": "Chưa chạy",
        "starting": "Đang khởi động",
        "running": "Đang kiểm tra",
        "stopping": "Đang dừng",
        "stopped": "Đã dừng",
        "error": "Lỗi nguồn / xử lý",
    }

    def __init__(self, runtime: RuntimePort) -> None:
        super().__init__()
        self.runtime = runtime
        self.sequence = -1
        self.event_ids: list[int] = []
        self.selected_event_id: int | None = None
        self.following = True
        self.context_visible = False
        self.last_state = "idle"
        self.setWindowTitle("TNM Vision — Qt Runtime")
        self.setMinimumSize(980, 680)
        self.resize(1280, 800)
        self.setStyleSheet(STYLE)
        self._build()

        self.timer = QTimer(self)
        self.timer.setInterval(200)
        self.timer.timeout.connect(self.refresh)
        self.timer.start()
        fullscreen = QShortcut(QKeySequence("F11"), self)
        fullscreen.activated.connect(self.toggle_fullscreen)
        escape = QShortcut(QKeySequence("Escape"), self)
        escape.activated.connect(self.leave_fullscreen)
        self.refresh()

    def _build(self) -> None:
        root = QWidget(objectName="root")
        root_layout = QVBoxLayout(root)
        root_layout.setContentsMargins(0, 0, 0, 0)
        root_layout.setSpacing(0)
        root_layout.addWidget(self._build_header())

        body = QWidget()
        layout = QVBoxLayout(body)
        layout.setContentsMargins(22, 16, 22, 12)
        layout.setSpacing(12)
        layout.addLayout(self._build_title())
        layout.addWidget(self._build_controls())

        splitter = QSplitter(Qt.Orientation.Horizontal)
        splitter.setChildrenCollapsible(False)
        splitter.addWidget(self._build_monitor())
        inspector = self._build_inspector()
        inspector.setMinimumWidth(340)
        splitter.addWidget(inspector)
        splitter.setStretchFactor(0, 5)
        splitter.setStretchFactor(1, 3)
        splitter.setSizes([790, 440])
        layout.addWidget(splitter, 1)

        footer = QHBoxLayout()
        self.config_label = text_label("Đang đọc cấu hình…", "muted")
        footer.addWidget(self.config_label)
        footer.addStretch(1)
        footer.addWidget(text_label("TNM Vision · Giao diện thử nghiệm Qt", "muted"))
        layout.addLayout(footer)
        root_layout.addWidget(body, 1)
        self.setCentralWidget(root)

    def _build_header(self) -> QWidget:
        header = QFrame(objectName="header")
        layout = QHBoxLayout(header)
        layout.setContentsMargins(22, 9, 22, 9)
        layout.setSpacing(11)

        mark = QLabel(objectName="brandLogo")
        logo_path = files("ui_machine").joinpath("assets/logo.png")
        logo = QPixmap(str(logo_path))
        mark.setPixmap(
            logo.scaled(
                46,
                46,
                Qt.AspectRatioMode.KeepAspectRatio,
                Qt.TransformationMode.SmoothTransformation,
            )
        )
        mark.setAlignment(Qt.AlignmentFlag.AlignCenter)
        mark.setAccessibleName("TNM logo")
        mark.setFixedSize(46, 46)
        layout.addWidget(mark)
        names = QVBoxLayout()
        names.setSpacing(1)
        names.addWidget(QLabel("TNM Vision", objectName="brandName"))
        names.addWidget(QLabel("Kiểm tra bề mặt vải", objectName="brandSub"))
        layout.addLayout(names)
        layout.addStretch(1)
        layout.addWidget(QLabel("Trạm kiểm tra / 01", objectName="headerMeta"))
        layout.addWidget(
            StatusBadge("Backend HTTP" if self.runtime.supports_camera else "Mô phỏng", "good")
        )
        button = QPushButton("Toàn màn hình", objectName="headerButton")
        button.clicked.connect(self.toggle_fullscreen)
        layout.addWidget(button)
        return header

    def _build_title(self) -> QHBoxLayout:
        layout = QHBoxLayout()
        title = QVBoxLayout()
        title.setSpacing(2)
        title.addWidget(QLabel("Kiểm tra trực tiếp", objectName="pageTitle"))
        title.addWidget(
            QLabel(
                "Quan sát bề mặt vải và đối chiếu vùng nghi ngờ trong cùng một màn hình.",
                objectName="pageSub",
            )
        )
        layout.addLayout(title)
        layout.addStretch(1)
        note = text_label("Xử lý trên thiết bị\nẢnh không gửi ra ngoài", "muted")
        note.setAlignment(Qt.AlignmentFlag.AlignRight | Qt.AlignmentFlag.AlignVCenter)
        layout.addWidget(note)
        return layout

    def _build_controls(self) -> QWidget:
        controls = QFrame(objectName="controls")
        layout = QHBoxLayout(controls)
        layout.setContentsMargins(14, 10, 14, 10)
        layout.setSpacing(10)
        source_box = QVBoxLayout()
        source_box.setSpacing(3)
        source_box.addWidget(text_label("Nguồn ảnh", "fieldLabel"))
        self.source = QComboBox()
        self.source.addItem("Mô phỏng vải chuyển động", "demo")
        self.source.addItem("Camera trên thiết bị", "camera")
        if not self.runtime.supports_camera:
            self.source.model().item(1).setEnabled(False)
            self.source.setToolTip("Kết nối --api-url để sử dụng camera.")
        self.source.setMinimumWidth(240)
        source_box.addWidget(self.source)
        layout.addLayout(source_box)

        self.start_button = QPushButton("Bắt đầu kiểm tra", objectName="startButton")
        self.stop_button = QPushButton("Dừng kiểm tra", objectName="stopButton")
        self.start_button.clicked.connect(self.start_runtime)
        self.stop_button.clicked.connect(self.stop_runtime)
        layout.addWidget(self.start_button, alignment=Qt.AlignmentFlag.AlignBottom)
        layout.addWidget(self.stop_button, alignment=Qt.AlignmentFlag.AlignBottom)
        layout.addStretch(1)
        note = text_label(
            "Dừng kiểm tra chỉ dừng phần mềm xử lý ảnh.\nKhông điều khiển chuyển động của máy.",
            "muted",
        )
        note.setAlignment(Qt.AlignmentFlag.AlignRight | Qt.AlignmentFlag.AlignVCenter)
        layout.addWidget(note)
        return controls

    def _build_monitor(self) -> QWidget:
        result = QWidget()
        outer = QVBoxLayout(result)
        outer.setContentsMargins(0, 0, 0, 0)
        outer.setSpacing(9)

        frame, layout = make_card()
        self.run_badge = StatusBadge("Chưa chạy")
        layout.addWidget(section_header("Khung detect runtime", self.run_badge))
        layout.addWidget(separator())
        self.frame_view = ImageView(
            "imageWell",
            "SẴN SÀNG QUAN SÁT\n\nChọn nguồn ảnh và bắt đầu kiểm tra.\n"
            "Vùng nghi ngờ sẽ xuất hiện ở khung bên cạnh.",
            300,
        )
        layout.addWidget(self.frame_view, 1)
        layout.addWidget(separator())

        stats = QHBoxLayout()
        stats.setContentsMargins(16, 9, 16, 9)
        stats.setSpacing(26)
        self.fps_value = self._stat(stats, "Tốc độ xử lý thực tế", "—", "FPS")
        self.latency_value = self._stat(stats, "Thời gian / khung", "—", "ms")
        self.processed_value = self._stat(stats, "Khung đã kiểm tra", "0")
        stats.addStretch(1)
        layout.addLayout(stats)
        outer.addWidget(frame, 1)

        self.message = QLabel("Chọn nguồn ảnh rồi nhấn Bắt đầu kiểm tra.", objectName="message")
        self.message.setWordWrap(True)
        self.message.setMinimumHeight(46)
        outer.addWidget(self.message)
        outer.addWidget(
            text_label(
                "▣  Khung xanh: vùng nghi ngờ cần kiểm tra, chưa phải kết luận lỗi.",
                "muted",
            )
        )
        return result

    def _stat(self, target: QHBoxLayout, title: str, value: str, unit: str = "") -> QLabel:
        box = QVBoxLayout()
        box.setSpacing(1)
        box.addWidget(text_label(title, "statLabel"))
        row = QHBoxLayout()
        row.setSpacing(4)
        value_label = text_label(value, "statValue")
        row.addWidget(value_label)
        row.addWidget(text_label(unit, "muted"), alignment=Qt.AlignmentFlag.AlignBottom)
        row.addStretch(1)
        box.addLayout(row)
        target.addLayout(box)
        return value_label

    def _build_inspector(self) -> QWidget:
        frame, layout = make_card()
        self.follow_label = text_label("Theo ảnh mới nhất", "muted")
        layout.addWidget(section_header("Vùng nghi ngờ", self.follow_label))
        layout.addWidget(separator())

        content = QWidget()
        content_layout = QVBoxLayout(content)
        content_layout.setContentsMargins(14, 11, 14, 9)
        content_layout.setSpacing(8)
        self.crop_view = ImageView(
            "cropWell", "Khi phát hiện vùng nghi ngờ,\nảnh cắt sẽ hiển thị tại đây.", 138
        )
        self.crop_view.setMaximumHeight(205)
        content_layout.addWidget(self.crop_view)

        detail = QHBoxLayout()
        self.event_title = text_label("Chưa có ảnh sự kiện", "sectionTitle")
        self.event_time = text_label("—", "muted")
        detail.addWidget(self.event_title)
        detail.addStretch(1)
        detail.addWidget(self.event_time)
        content_layout.addLayout(detail)
        self.event_detail = text_label("Ảnh được giữ lại để người vận hành đối chiếu.", "muted")
        self.event_detail.setWordWrap(True)
        content_layout.addWidget(self.event_detail)

        self.context_view = ImageView("contextWell", "", 110)
        self.context_view.setMaximumHeight(165)
        self.context_view.setVisible(False)
        content_layout.addWidget(self.context_view)

        actions = QHBoxLayout()
        self.context_button = QPushButton("Xem toàn cảnh")
        self.context_button.setEnabled(False)
        self.context_button.clicked.connect(self.toggle_context)
        self.follow_button = QPushButton("Theo ảnh mới nhất")
        self.follow_button.setEnabled(False)
        self.follow_button.clicked.connect(self.follow_latest)
        actions.addWidget(self.context_button)
        actions.addWidget(self.follow_button)
        content_layout.addLayout(actions)
        layout.addWidget(content)
        layout.addWidget(separator())

        history = QWidget()
        history_layout = QHBoxLayout(history)
        history_layout.setContentsMargins(14, 8, 14, 8)
        history_layout.addWidget(text_label("Ảnh sự kiện gần đây", "fieldLabel"))
        history_layout.addStretch(1)
        self.event_count = text_label("0 ảnh", "muted")
        history_layout.addWidget(self.event_count)
        layout.addWidget(history)
        layout.addWidget(separator())

        self.events = QListWidget()
        self.events.setMinimumHeight(105)
        self.events.setMaximumHeight(180)
        self.events.currentItemChanged.connect(self.select_history_item)
        layout.addWidget(self.events)
        layout.addWidget(separator())
        retention = text_label(
            "Giữ tối đa 24 ảnh trong RAM · Lấy mẫu tối đa 1 ảnh / 2 giây.\n"
            "Số ảnh sự kiện không phải số lỗi vải độc lập.",
            "muted",
        )
        retention.setWordWrap(True)
        retention.setContentsMargins(14, 7, 14, 8)
        layout.addWidget(retention)
        return frame

    def start_runtime(self) -> None:
        try:
            self._reset_session()
            self.runtime.start(self.source.currentData())
            self.set_message("Đang mở nguồn ảnh và xử lý khung đầu tiên…")
        except ValueError as error:
            self.set_message(str(error), bad=True)
        self.refresh()

    def stop_runtime(self) -> None:
        self.runtime.stop()
        self.set_message("Đang chờ xử lý hiện tại kết thúc và đóng nguồn ảnh…")
        self.refresh()

    def _reset_session(self) -> None:
        self.sequence = -1
        self.event_ids = []
        self.selected_event_id = None
        self.following = True
        self.context_visible = False
        self.events.clear()
        self.frame_view.clear_image("ĐANG KHỞI ĐỘNG…")
        self.crop_view.clear_image("Khi phát hiện vùng nghi ngờ,\nảnh cắt sẽ hiển thị tại đây.")
        self.context_view.setVisible(False)
        self.context_button.setText("Xem toàn cảnh")
        self.event_title.setText("Chưa có ảnh sự kiện")
        self.event_time.setText("—")

    def refresh(self) -> None:
        state = self.runtime.snapshot()
        active = state["state"] in {"starting", "running", "stopping"}
        self.source.setEnabled(not active)
        self.start_button.setEnabled(not active)
        self.stop_button.setEnabled(active and state["state"] != "stopping")

        if state["stale"]:
            status, tone = "Dữ liệu chậm", "warn"
        elif state["state"] == "running":
            status, tone = "Đang kiểm tra", "good"
        elif state["state"] == "error":
            status, tone = "Lỗi nguồn / xử lý", "bad"
        else:
            status, tone = self.state_names[state["state"]], "neutral"
        self.run_badge.set_status(status, tone)

        self.fps_value.setText(f"{state['fps']:.1f}" if state["processed"] > 1 else "—")
        self.latency_value.setText(
            str(round(state["processing_ms"])) if state["has_frame"] else "—"
        )
        self.processed_value.setText(f"{state['processed']:,}".replace(",", "."))
        self.event_count.setText(f"{len(state['events'])} / {state['event_total']} ảnh")
        self.config_label.setText(
            f"Detector {state['detector']} · Tối đa {state['max_width']} px · "
            f"Giới hạn {state['target_fps']} FPS"
        )

        if state["has_frame"] and state["sequence"] != self.sequence:
            if self.frame_view.set_bytes(self.runtime.image("frame", state["sequence"])):
                self.sequence = state["sequence"]

        self._sync_events(state["events"])
        if self.following and state["events"]:
            latest = state["events"][0]
            if latest["id"] != self.selected_event_id:
                self._show_event(latest)

        if state["state"] == "error":
            self.set_message(
                f"{state['error']} Có thể chọn lại nguồn rồi bắt đầu phiên mới.", bad=True
            )
        elif state["stale"]:
            self.set_message(
                "Ảnh đang cập nhật chậm. Không dùng ảnh cũ để kết luận trạng thái hiện tại.",
                bad=True,
            )
        elif state["state"] == "running":
            source_name = "vải mô phỏng" if state["source"] == "demo" else "ảnh camera"
            self.set_message(
                f"Đang chạy detector trên {source_name}. Chọn ảnh bên phải để đối chiếu."
            )
        elif state["state"] == "stopped" and self.last_state != "stopped":
            self.set_message(
                "Đã dừng kiểm tra. Ảnh cuối phiên và vùng nghi ngờ được giữ để đối chiếu."
            )
        elif state["state"] == "idle":
            self.set_message("Chọn nguồn ảnh rồi nhấn Bắt đầu kiểm tra.")
        self.last_state = state["state"]

    def _sync_events(self, events: list[dict]) -> None:
        ids = [event["id"] for event in events]
        if ids == self.event_ids:
            return
        selected = self.selected_event_id
        self.events.blockSignals(True)
        self.events.clear()
        for event in events:
            item = QListWidgetItem(
                f"Ảnh #{event['frame']}    {event['regions']} vùng nghi ngờ    {event['time']}"
            )
            item.setData(Qt.ItemDataRole.UserRole, event)
            item.setSizeHint(QSize(0, 48))
            self.events.addItem(item)
            if event["id"] == selected:
                self.events.setCurrentItem(item)
        self.events.blockSignals(False)
        self.event_ids = ids

    def select_history_item(
        self, current: QListWidgetItem | None, _previous: QListWidgetItem | None
    ) -> None:
        if current is None:
            return
        self.following = False
        self._show_event(current.data(Qt.ItemDataRole.UserRole))

    def _show_event(self, event: dict) -> None:
        event_id = event["id"]
        crop = self.runtime.image("crop", event_id)
        context = self.runtime.image("context", event_id)
        if not crop or not context:
            self.set_message("Ảnh đã rời bộ nhớ đệm. Chọn một ảnh mới hơn.", bad=True)
            return
        self.crop_view.set_bytes(crop)
        self.context_view.set_bytes(context)
        self.selected_event_id = event_id
        if self.following:
            self.events.blockSignals(True)
            for index in range(self.events.count()):
                item = self.events.item(index)
                if item.data(Qt.ItemDataRole.UserRole)["id"] == event_id:
                    self.events.setCurrentItem(item)
                    break
            self.events.blockSignals(False)
        self.event_title.setText(f"Ảnh #{event['frame']}")
        self.event_time.setText(event["time"])
        self.event_detail.setText(
            f"{event['regions']} vùng · Ảnh cắt vùng lớn nhất "
            f"{event['width']} × {event['height']} px"
        )
        self.follow_label.setText("Theo ảnh mới nhất" if self.following else "Đang giữ ảnh đã chọn")
        self.follow_button.setEnabled(not self.following)
        self.context_button.setEnabled(True)

    def follow_latest(self) -> None:
        self.following = True
        events = self.runtime.snapshot()["events"]
        if events:
            self._show_event(events[0])

    def toggle_context(self) -> None:
        self.context_visible = not self.context_visible
        self.context_view.setVisible(self.context_visible)
        self.context_button.setText("Ẩn toàn cảnh" if self.context_visible else "Xem toàn cảnh")

    def set_message(self, text: str, *, bad: bool = False) -> None:
        self.message.setText(text)
        self.message.setProperty("tone", "bad" if bad else "normal")
        repolish(self.message)

    def toggle_fullscreen(self) -> None:
        self.showNormal() if self.isFullScreen() else self.showFullScreen()

    def leave_fullscreen(self) -> None:
        if self.isFullScreen():
            self.showNormal()

    def closeEvent(self, event) -> None:  # noqa: N802
        self.timer.stop()
        self.runtime.close()
        event.accept()


def parse_size(value: str) -> tuple[int, int]:
    try:
        width, height = (int(part) for part in value.lower().split("x", 1))
    except (ValueError, TypeError) as error:
        raise argparse.ArgumentTypeError("Size must use the form 1280x800") from error
    if width < 800 or height < 600:
        raise argparse.ArgumentTypeError("Minimum size is 800x600")
    return width, height


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description="TNM Vision: Qt Widgets runtime HMI")
    parser.add_argument("--fps", type=int, default=5)
    parser.add_argument("--width", type=int, default=640)
    parser.add_argument(
        "--api-url",
        help="Tokenized root URL printed by TNM Vision runtime; omit for demo mode.",
    )
    parser.add_argument("--fullscreen", action="store_true")
    parser.add_argument("--autostart", choices=("demo", "camera"))
    parser.add_argument("--size", type=parse_size, default=(1280, 800))
    parser.add_argument("--screenshot", type=Path, help=argparse.SUPPRESS)
    args = parser.parse_args(argv)
    app = QApplication([sys.argv[0]])
    app.setApplicationName("TNM Vision")
    app.setFont(QFont("Segoe UI", 10))
    runtime: RuntimePort
    if args.api_url:
        runtime = HttpRuntime(args.api_url)
    else:
        runtime = DemoRuntime(fps=args.fps, width=args.width)
    window = RuntimeWindow(runtime)
    window.resize(*args.size)
    window.showFullScreen() if args.fullscreen else window.show()

    if args.autostart or args.screenshot:
        source = args.autostart or "demo"
        window.source.setCurrentIndex(0 if source == "demo" else 1)
        QTimer.singleShot(80, window.start_runtime)

    if args.screenshot:
        args.screenshot.parent.mkdir(parents=True, exist_ok=True)

        def capture() -> None:
            window.grab().save(str(args.screenshot), "PNG")
            window.close()

        QTimer.singleShot(2400, capture)

    return app.exec()


if __name__ == "__main__":
    raise SystemExit(main())
