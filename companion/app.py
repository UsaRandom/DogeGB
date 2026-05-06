"""
DogeGB Companion App — desktop watch-only wallet + IR transport.

Workflow:
    1. Paste your DogeGB-derived Dogecoin address (watch-only).
    2. Refresh UTXOs.
    3. Enter recipient + amount + (optional) OP_RETURN.
    4. Build proposal — review the binary that will be sent to the GBC.
    5. Connect to the Arduino over USB, then send the proposal over IR.
    6. (Optional) Wait for the GBC to send a signed tx back, then broadcast it.
"""

import logging
import queue
import sys
import threading
import time

from PyQt6.QtCore import Qt, QTimer, pyqtSignal, QObject, QThread
from PyQt6.QtGui import QFont, QColor, QPalette, QImage, QPixmap
from PyQt6.QtWidgets import (
    QApplication, QMainWindow, QWidget, QVBoxLayout, QHBoxLayout,
    QGridLayout, QLabel, QLineEdit, QPushButton, QTextEdit, QComboBox,
    QProgressBar, QTabWidget, QFrame, QMessageBox, QSizePolicy, QDialog,
)

import dogecoin
import utxo_api
import wire_format
from dogecoin import (
    KOINU_PER_DOGE,
    DecodedAddress,
    InsufficientFundsError,
    TxInput,
    TxOutput,
    decode_address,
    doge_to_koinu,
    estimate_signed_tx_size,
    format_doge,
    is_valid_doge_address,
    script_op_return,
    select_coins_greedy,
    serialize_unsigned_tx,
)
from ir_transport import ArduinoBridge, ChunkedTransport, TransportError
from wire_format import (
    MSG_SIGNED_TX,
    MSG_TX_PROPOSAL,
    pack_message,
    pack_tx_proposal,
    parse_tx_proposal,
    unpack_message,
)

logging.basicConfig(level=logging.INFO, format="%(asctime)s %(name)s %(levelname)s: %(message)s")

# --- Theme ----------------------------------------------------------------

BG      = "#000000"
PANEL   = "#0E0E0E"
PANEL_ALT = "#1A1A1A"
BORDER  = "#222222"
BORDER_BRIGHT = "#3A3A3A"
TEXT    = "#E8E8E8"
TEXT_DIM = "#9A9A9A"
ACCENT  = "#C2A633"
ACCENT_HOVER = "#A88E22"
SUCCESS = "#3FB950"
WARN    = "#E5A35E"
ERROR   = "#F25C5C"

if sys.platform == "darwin":
    MONO = "Menlo"
elif sys.platform.startswith("win"):
    MONO = "Consolas"
else:
    MONO = "DejaVu Sans Mono"

STYLESHEET = f"""
QWidget {{
    background-color: {BG};
    color: {TEXT};
    font-family: "{MONO}";
    font-size: 12px;
}}
QFrame#panel {{
    background-color: {PANEL};
    border: 1px solid {BORDER};
    border-radius: 4px;
}}
QLabel {{
    background: transparent;
    border: none;
}}
QLineEdit {{
    background-color: {PANEL_ALT};
    color: {TEXT};
    border: 1px solid {BORDER_BRIGHT};
    border-radius: 3px;
    padding: 4px 6px;
    selection-background-color: {ACCENT};
    selection-color: #000000;
}}
QLineEdit:focus {{
    border-color: {ACCENT};
}}
QLineEdit:disabled {{
    color: {TEXT_DIM};
}}
QPushButton {{
    background-color: {ACCENT};
    color: #000000;
    border: none;
    border-radius: 3px;
    padding: 7px 14px;
    font-weight: bold;
}}
QPushButton:hover {{
    background-color: {ACCENT_HOVER};
}}
QPushButton:pressed {{
    background-color: {ACCENT_HOVER};
}}
QPushButton:disabled {{
    background-color: {PANEL_ALT};
    color: {TEXT_DIM};
}}
QPushButton#secondary {{
    background-color: {PANEL_ALT};
    color: {TEXT};
    border: 1px solid {BORDER_BRIGHT};
    font-weight: normal;
}}
QPushButton#secondary:hover {{
    background-color: {BORDER};
}}
QPushButton#secondary:disabled {{
    color: {TEXT_DIM};
    background-color: {PANEL};
}}
QPushButton#icon {{
    background-color: {PANEL_ALT};
    color: {TEXT};
    border: 1px solid {BORDER_BRIGHT};
    border-radius: 3px;
    padding: 6px 10px;
    font-weight: normal;
}}
QPushButton#icon:hover {{
    background-color: {BORDER};
}}
QComboBox {{
    background-color: {PANEL_ALT};
    color: {TEXT};
    border: 1px solid {BORDER_BRIGHT};
    border-radius: 3px;
    padding: 4px 6px;
}}
QComboBox::drop-down {{
    border: none;
    padding-right: 6px;
}}
QComboBox QAbstractItemView {{
    background-color: {PANEL_ALT};
    color: {TEXT};
    selection-background-color: {ACCENT};
    selection-color: #000000;
    border: 1px solid {BORDER_BRIGHT};
}}
QTextEdit {{
    background-color: {PANEL_ALT};
    color: {TEXT};
    border: none;
    border-radius: 3px;
    padding: 6px;
    font-size: 11px;
}}
QTabWidget::pane {{
    background-color: {PANEL_ALT};
    border: 1px solid {BORDER};
    border-radius: 3px;
}}
QTabBar::tab {{
    background-color: {PANEL};
    color: {TEXT_DIM};
    padding: 6px 14px;
    border: none;
    font-size: 10px;
}}
QTabBar::tab:selected {{
    background-color: {PANEL_ALT};
    color: {ACCENT};
}}
QTabBar::tab:hover {{
    color: {TEXT};
}}
QProgressBar {{
    background-color: {PANEL_ALT};
    border: none;
    border-radius: 3px;
    height: 6px;
    text-align: center;
    color: transparent;
}}
QProgressBar::chunk {{
    background-color: {ACCENT};
    border-radius: 3px;
}}
QScrollBar:vertical {{
    background: {PANEL};
    width: 8px;
    border: none;
}}
QScrollBar::handle:vertical {{
    background: {BORDER_BRIGHT};
    border-radius: 4px;
    min-height: 20px;
}}
QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical {{
    height: 0px;
}}
"""


def panel() -> QFrame:
    f = QFrame()
    f.setObjectName("panel")
    return f


def dim_label(text: str, small: bool = False) -> QLabel:
    lbl = QLabel(text)
    lbl.setStyleSheet(f"color: {TEXT_DIM}; font-size: {'10' if small else '11'}px;")
    return lbl


def accent_label(text: str, large: bool = False) -> QLabel:
    lbl = QLabel(text)
    size = "20" if large else "11"
    weight = "bold"
    lbl.setStyleSheet(f"color: {ACCENT}; font-size: {size}px; font-weight: {weight};")
    return lbl


# --- QR Scanner -----------------------------------------------------------

def _parse_bip21(data: str) -> tuple[str, float | None]:
    """Parse a plain address or dogecoin: BIP21 URI.
    Returns (address, amount_doge_or_None)."""
    data = data.strip()
    if data.lower().startswith("dogecoin:"):
        data = data[9:]
    parts = data.split("?", 1)
    addr = parts[0]
    amount = None
    if len(parts) > 1:
        for param in parts[1].split("&"):
            if param.lower().startswith("amount="):
                try:
                    amount = float(param[7:])
                except ValueError:
                    pass
    return addr, amount


def _is_doge_qr(data: str) -> bool:
    addr, _ = _parse_bip21(data)
    return is_valid_doge_address(addr)


class _CameraWorker(QObject):
    frame_ready = pyqtSignal(QImage)
    data_found = pyqtSignal(str)
    error = pyqtSignal(str)
    finished = pyqtSignal()

    def __init__(self, camera_index: int = 0, validator=None):
        super().__init__()
        self._camera_index = camera_index
        self._validator = validator if validator is not None else _is_doge_qr
        self._running = False

    def start(self):
        self._running = True
        try:
            import cv2
        except ImportError:
            self.error.emit("opencv-python is not installed (pip install opencv-python)")
            self.finished.emit()
            return

        cap = cv2.VideoCapture(self._camera_index)
        if not cap.isOpened():
            self.error.emit("Could not open camera")
            self.finished.emit()
            return

        detector = cv2.QRCodeDetector()
        found = False

        while self._running:
            ok, frame = cap.read()
            if not ok:
                continue

            data, _, _ = detector.detectAndDecode(frame)
            if data and not found and self._validator(data):
                found = True
                self._running = False
                self.data_found.emit(data.strip())

            rgb = cv2.cvtColor(frame, cv2.COLOR_BGR2RGB)
            h, w, ch = rgb.shape
            img = QImage(rgb.data, w, h, ch * w, QImage.Format.Format_RGB888)
            self.frame_ready.emit(img.copy())

        cap.release()
        self.finished.emit()

    def stop(self):
        self._running = False


class QRScannerDialog(QDialog):
    def __init__(self, parent=None, hint: str = "", validator=None):
        super().__init__(parent)
        self.setWindowTitle("Scan Dogecoin QR")
        self.setModal(True)
        self.resize(520, 440)
        self.scanned_data: str = ""

        layout = QVBoxLayout(self)
        layout.setContentsMargins(12, 12, 12, 12)
        layout.setSpacing(8)

        hint_lbl = QLabel(hint or "Point the camera at the QR code on your Game Boy screen.")
        hint_lbl.setStyleSheet(f"color: {TEXT_DIM}; font-size: 11px;")
        hint_lbl.setAlignment(Qt.AlignmentFlag.AlignCenter)
        layout.addWidget(hint_lbl)

        self._preview = QLabel()
        self._preview.setAlignment(Qt.AlignmentFlag.AlignCenter)
        self._preview.setMinimumSize(480, 360)
        self._preview.setStyleSheet(f"background: {PANEL_ALT}; border: 1px solid {BORDER};")
        layout.addWidget(self._preview)

        self._status = QLabel("Initialising camera…")
        self._status.setAlignment(Qt.AlignmentFlag.AlignCenter)
        self._status.setStyleSheet(f"color: {TEXT_DIM}; font-size: 11px;")
        layout.addWidget(self._status)

        cancel_btn = QPushButton("Cancel")
        cancel_btn.setObjectName("secondary")
        cancel_btn.clicked.connect(self._cancel)
        layout.addWidget(cancel_btn)

        self._thread = QThread()
        self._worker = _CameraWorker(validator=validator)
        self._worker.moveToThread(self._thread)
        self._thread.started.connect(self._worker.start)
        self._worker.frame_ready.connect(self._on_frame)
        self._worker.data_found.connect(self._on_found)
        self._worker.error.connect(self._on_error)
        self._worker.finished.connect(self._thread.quit)
        self._thread.start()

    def _on_frame(self, img: QImage):
        pix = QPixmap.fromImage(img).scaled(
            self._preview.size(),
            Qt.AspectRatioMode.KeepAspectRatio,
            Qt.TransformationMode.SmoothTransformation,
        )
        self._preview.setPixmap(pix)
        self._status.setText("Scanning…")

    def _on_found(self, data: str):
        self.scanned_data = data
        self._status.setText(f"Found: {data}")
        self._status.setStyleSheet(f"color: {SUCCESS}; font-size: 11px;")
        QTimer.singleShot(600, self.accept)

    def _on_error(self, msg: str):
        self._status.setText(msg)
        self._status.setStyleSheet(f"color: {ERROR}; font-size: 11px;")

    def _cancel(self):
        self._worker.stop()
        self._thread.wait(2000)
        self.reject()

    def closeEvent(self, event):
        self._worker.stop()
        self._thread.wait(2000)
        event.accept()


# --- Main window ----------------------------------------------------------

class DogeGBCompanion(QMainWindow):
    def __init__(self):
        super().__init__()
        self.setWindowTitle("DogeGB Companion")
        self.resize(800, 960)
        self.setMinimumSize(720, 720)

        self.watch_address: DecodedAddress | None = None
        self.utxos: list[dict] = []
        self.last_proposal_bytes: bytes | None = None
        self.last_proposal_inputs: list[TxInput] = []
        self.last_proposal_outputs: list[TxOutput] = []
        self.last_unsigned_tx_hex: str = ""
        self.bridge: ArduinoBridge | None = None
        self.transport: ChunkedTransport | None = None
        self.last_signed_tx_hex: str = ""

        self._ui_queue: queue.Queue = queue.Queue()
        self._timer = QTimer()
        self._timer.setInterval(50)
        self._timer.timeout.connect(self._drain_ui_queue)
        self._timer.start()

        central = QWidget()
        self.setCentralWidget(central)
        root = QVBoxLayout(central)
        root.setContentsMargins(12, 12, 12, 12)
        root.setSpacing(8)

        self._build_header(root)
        self._build_wallet_section(root)
        self._build_tx_section(root)
        self._build_review_section(root)
        self._build_transport_section(root)
        root.addStretch()

    # ------------------------------------------------------------------
    # UI helpers
    # ------------------------------------------------------------------

    def _drain_ui_queue(self):
        try:
            while True:
                fn = self._ui_queue.get_nowait()
                try:
                    fn()
                except Exception as e:
                    logging.exception("UI callback failed: %s", e)
        except queue.Empty:
            pass

    def _ui(self, fn):
        self._ui_queue.put(fn)

    def _log(self, msg: str, tag: str = "info"):
        colors = {
            "info": TEXT,
            "good": SUCCESS,
            "warn": WARN,
            "err":  ERROR,
            "dim":  TEXT_DIM,
        }
        color = colors.get(tag, TEXT)
        stamp = time.strftime("%H:%M:%S")

        def do():
            self.log_text.setReadOnly(False)
            cursor = self.log_text.textCursor()
            from PyQt6.QtGui import QTextCursor
            cursor.movePosition(QTextCursor.MoveOperation.End)

            fmt_dim = self.log_text.currentCharFormat()
            fmt_dim.setForeground(QColor(TEXT_DIM))
            cursor.setCharFormat(fmt_dim)
            cursor.insertText(f"{stamp}  ")

            fmt_msg = self.log_text.currentCharFormat()
            fmt_msg.setForeground(QColor(color))
            cursor.setCharFormat(fmt_msg)
            cursor.insertText(msg + "\n")

            self.log_text.setTextCursor(cursor)
            self.log_text.ensureCursorVisible()
            self.log_text.setReadOnly(True)

        if threading.current_thread() is threading.main_thread():
            do()
        else:
            self._ui(do)

    def _set_hex_box(self, box: QTextEdit, text: str):
        box.setReadOnly(False)
        box.setPlainText(text)
        box.setReadOnly(True)

    def _set_progress(self, frac: float):
        self.progress.setValue(int(max(0.0, min(1.0, frac)) * 100))

    def _list_serial_ports(self) -> list[str]:
        try:
            from serial.tools import list_ports
            ports = [p.device for p in list_ports.comports()]
            return ports if ports else ["(no ports detected)"]
        except Exception:
            return ["(install pyserial to enumerate)"]

    # ------------------------------------------------------------------
    # UI construction
    # ------------------------------------------------------------------

    def _build_header(self, root: QVBoxLayout):
        title = accent_label("◆ DogeGB Companion", large=True)
        sub = dim_label("watch-only wallet  ·  unsigned tx builder  ·  IR bridge")
        root.addWidget(title)
        root.addWidget(sub)

    def _section_label(self, text: str) -> QLabel:
        lbl = accent_label(text)
        lbl.setStyleSheet(f"color: {ACCENT}; font-size: 11px; font-weight: bold; padding: 4px 0;")
        return lbl

    def _build_wallet_section(self, root: QVBoxLayout):
        p = panel()
        layout = QGridLayout(p)
        layout.setContentsMargins(14, 10, 14, 12)
        layout.setSpacing(6)
        layout.setColumnStretch(1, 1)  # address entry expands

        layout.addWidget(self._section_label("WALLET"), 0, 0, 1, 3)

        layout.addWidget(dim_label("Watch address", small=False), 1, 0)

        self.address_entry = QLineEdit()
        layout.addWidget(self.address_entry, 1, 1)

        scan_btn = QPushButton("Scan QR")
        scan_btn.setObjectName("secondary")
        scan_btn.clicked.connect(self._on_scan_qr)
        layout.addWidget(scan_btn, 1, 2)

        self.refresh_btn = QPushButton("Fetch UTXOs")
        self.refresh_btn.clicked.connect(self._on_refresh_utxos)
        layout.addWidget(self.refresh_btn, 1, 3)

        layout.addWidget(dim_label("format: D… (your DogeGB-derived address)", small=True), 2, 1, 1, 3)

        self.stats_label = QLabel("  No address loaded")
        self.stats_label.setStyleSheet(f"color: {TEXT_DIM}; font-size: 11px;")
        layout.addWidget(self.stats_label, 3, 0, 1, 4)

        root.addWidget(p)

    def _build_tx_section(self, root: QVBoxLayout):
        p = panel()
        layout = QGridLayout(p)
        layout.setContentsMargins(14, 10, 14, 12)
        layout.setSpacing(6)
        layout.setColumnStretch(1, 1)
        layout.setColumnStretch(3, 1)

        layout.addWidget(self._section_label("NEW TRANSACTION"), 0, 0, 1, 4)

        layout.addWidget(dim_label("Send to"), 1, 0)
        self.recipient_entry = QLineEdit()
        layout.addWidget(self.recipient_entry, 1, 1, 1, 2)

        scan_recipient_btn = QPushButton("Scan QR")
        scan_recipient_btn.setObjectName("secondary")
        scan_recipient_btn.clicked.connect(self._on_scan_recipient_qr)
        layout.addWidget(scan_recipient_btn, 1, 3)

        layout.addWidget(dim_label("P2PKH (D…) or P2SH (A… / 9…)", small=True), 2, 1, 1, 3)

        layout.addWidget(dim_label("Amount (DOGE)"), 3, 0)
        self.amount_entry = QLineEdit()
        layout.addWidget(self.amount_entry, 3, 1)

        layout.addWidget(dim_label("Fee (DOGE/kB)"), 3, 2)
        self.fee_rate_entry = QLineEdit("1.0")
        layout.addWidget(self.fee_rate_entry, 3, 3)

        layout.addWidget(dim_label("OP_RETURN"), 4, 0)

        op_text_row = QHBoxLayout()
        self.op_return_entry = QLineEdit()
        self.op_return_entry.textChanged.connect(self._on_op_return_changed)
        op_text_row.addWidget(self.op_return_entry)
        self.op_return_count = QLabel("0 / 80")
        self.op_return_count.setStyleSheet(f"color: {TEXT_DIM}; font-size: 10px;")
        op_text_row.addWidget(self.op_return_count)
        layout.addLayout(op_text_row, 4, 1)

        layout.addWidget(dim_label("Value (DOGE)"), 4, 2)
        self.op_return_value_entry = QLineEdit("0.0")
        layout.addWidget(self.op_return_value_entry, 4, 3)

        layout.addWidget(dim_label("optional UTF-8 message (max 80 bytes) · value defaults to 0", small=True), 5, 1, 1, 3)

        self.build_btn = QPushButton("Build Transaction")
        self.build_btn.clicked.connect(self._on_build_tx)
        layout.addWidget(self.build_btn, 6, 0, 1, 4)

        root.addWidget(p)

    def _build_review_section(self, root: QVBoxLayout):
        p = panel()
        layout = QVBoxLayout(p)
        layout.setContentsMargins(14, 10, 14, 12)
        layout.setSpacing(6)

        layout.addWidget(self._section_label("REVIEW"))

        self.review_summary = QLabel("  Build a transaction first.")
        self.review_summary.setStyleSheet(f"color: {TEXT_DIM}; font-size: 11px;")
        self.review_summary.setWordWrap(True)
        layout.addWidget(self.review_summary)

        tabs = QTabWidget()
        layout.addWidget(tabs)

        tab_wire = QWidget()
        tw_layout = QVBoxLayout(tab_wire)
        tw_layout.setContentsMargins(0, 0, 0, 0)
        self.proposal_hex_box = QTextEdit()
        self.proposal_hex_box.setReadOnly(True)
        self.proposal_hex_box.setFixedHeight(130)
        tw_layout.addWidget(self.proposal_hex_box)
        tabs.addTab(tab_wire, "Wire (sent to GBC)")

        tab_unsigned = QWidget()
        tu_layout = QVBoxLayout(tab_unsigned)
        tu_layout.setContentsMargins(0, 0, 0, 0)
        self.unsigned_hex_box = QTextEdit()
        self.unsigned_hex_box.setReadOnly(True)
        self.unsigned_hex_box.setFixedHeight(130)
        tu_layout.addWidget(self.unsigned_hex_box)
        tabs.addTab(tab_unsigned, "Unsigned tx (Bitcoin format)")

        root.addWidget(p)

    def _build_transport_section(self, root: QVBoxLayout):
        p = panel()
        layout = QGridLayout(p)
        layout.setContentsMargins(14, 10, 14, 12)
        layout.setSpacing(6)
        layout.setColumnStretch(1, 1)

        layout.addWidget(self._section_label("IR TRANSPORT"), 0, 0, 1, 3)

        layout.addWidget(dim_label("Serial port"), 1, 0)

        port_row = QHBoxLayout()
        self.port_combo = QComboBox()
        ports = self._list_serial_ports()
        self.port_combo.addItems(ports)
        port_row.addWidget(self.port_combo, 1)

        refresh_ports_btn = QPushButton("↻")
        refresh_ports_btn.setObjectName("icon")
        refresh_ports_btn.setFixedWidth(36)
        refresh_ports_btn.clicked.connect(self._on_refresh_ports)
        port_row.addWidget(refresh_ports_btn)

        self.connect_btn = QPushButton("Connect")
        self.connect_btn.clicked.connect(self._on_toggle_connection)
        port_row.addWidget(self.connect_btn)

        layout.addLayout(port_row, 1, 1, 1, 2)

        self.connection_status = QLabel("○ disconnected")
        self.connection_status.setStyleSheet(f"color: {TEXT_DIM}; font-size: 11px;")
        layout.addWidget(self.connection_status, 2, 0, 1, 3)

        btn_row = QHBoxLayout()
        self.send_btn = QPushButton("↑  Send proposal to GBC")
        self.send_btn.setEnabled(False)
        self.send_btn.clicked.connect(self._on_send_proposal)
        btn_row.addWidget(self.send_btn)

        self.receive_btn = QPushButton("↓  Receive signed tx")
        self.receive_btn.setObjectName("secondary")
        self.receive_btn.setEnabled(False)
        self.receive_btn.clicked.connect(self._on_receive_signed)
        btn_row.addWidget(self.receive_btn)

        self.broadcast_btn = QPushButton("↗  Broadcast")
        self.broadcast_btn.setObjectName("secondary")
        self.broadcast_btn.setEnabled(False)
        self.broadcast_btn.clicked.connect(self._on_broadcast)
        btn_row.addWidget(self.broadcast_btn)

        layout.addLayout(btn_row, 3, 0, 1, 3)

        self.progress = QProgressBar()
        self.progress.setMaximum(100)
        self.progress.setValue(0)
        self.progress.setFixedHeight(6)
        self.progress.setTextVisible(False)
        layout.addWidget(self.progress, 4, 0, 1, 3)

        self.log_text = QTextEdit()
        self.log_text.setReadOnly(True)
        self.log_text.setFixedHeight(160)
        layout.addWidget(self.log_text, 5, 0, 1, 3)

        root.addWidget(p)

    # ------------------------------------------------------------------
    # Wallet actions
    # ------------------------------------------------------------------

    def _on_scan_qr(self):
        dlg = QRScannerDialog(self, hint="Scan your DogeGB watch address QR.")
        if dlg.exec() == QDialog.DialogCode.Accepted and dlg.scanned_data:
            addr, _ = _parse_bip21(dlg.scanned_data)
            self.address_entry.setText(addr)
            self._log(f"QR scan: imported watch address {addr}", "good")

    def _on_scan_recipient_qr(self):
        dlg = QRScannerDialog(self, hint="Scan recipient address QR (plain address or dogecoin: URI).")
        if dlg.exec() == QDialog.DialogCode.Accepted and dlg.scanned_data:
            addr, amount = _parse_bip21(dlg.scanned_data)
            self.recipient_entry.setText(addr)
            if amount is not None:
                self.amount_entry.setText(str(amount))
                self._log(f"QR scan: recipient {addr}, amount {amount} DOGE", "good")
            else:
                self._log(f"QR scan: recipient {addr}", "good")

    def _on_refresh_utxos(self):
        addr = self.address_entry.text().strip()
        if not addr:
            QMessageBox.warning(self, "DogeGB", "Enter your watch address first.")
            return
        try:
            decoded = decode_address(addr)
        except Exception as e:
            QMessageBox.critical(self, "DogeGB", f"Not a valid Dogecoin address:\n{e}")
            return

        self.watch_address = decoded
        self.refresh_btn.setEnabled(False)
        self.refresh_btn.setText("Fetching…")
        self._log(f"Fetching UTXOs for {addr}…")

        threading.Thread(target=self._do_refresh, args=(addr,), daemon=True).start()

    def _do_refresh(self, addr: str):
        try:
            utxos = utxo_api.fetch_utxos(addr)
            self.utxos = utxos
            total = sum(u["value"] for u in utxos)
            confirmed = [u for u in utxos if u["confirmations"] > 0]
            confirmed_total = sum(u["value"] for u in confirmed)

            def update():
                self.stats_label.setText(
                    f"  {format_doge(total)} DOGE total  "
                    f"(confirmed {format_doge(confirmed_total)})\n"
                    f"  {len(utxos)} UTXOs  "
                    f"({len(confirmed)} confirmed, {len(utxos) - len(confirmed)} pending)"
                )
                self.stats_label.setStyleSheet(f"color: {TEXT}; font-size: 11px;")
                self._log(f"Got {len(utxos)} UTXOs, total {format_doge(total)} DOGE.", "good")
            self._ui(update)
        except Exception as e:
            self._log(f"UTXO fetch failed: {e}", "err")
            self._ui(lambda: QMessageBox.critical(self, "DogeGB", f"UTXO fetch failed:\n{e}"))
        finally:
            self._ui(lambda: (self.refresh_btn.setEnabled(True), self.refresh_btn.setText("Fetch UTXOs")))

    # ------------------------------------------------------------------
    # Build transaction
    # ------------------------------------------------------------------

    def _on_op_return_changed(self, text: str):
        n = len(text.encode("utf-8"))
        color = TEXT_DIM if n <= 80 else ERROR
        self.op_return_count.setText(f"{n} / 80")
        self.op_return_count.setStyleSheet(f"color: {color}; font-size: 10px;")

    def _on_build_tx(self):
        if self.watch_address is None:
            QMessageBox.warning(self, "DogeGB", "Load a watch address first.")
            return
        if not self.utxos:
            QMessageBox.warning(self, "DogeGB", "No UTXOs available — fetch first.")
            return

        recipient_str = self.recipient_entry.text().strip()
        amount_str = self.amount_entry.text().strip()
        fee_rate_str = self.fee_rate_entry.text().strip() or "1.0"
        op_return_text = self.op_return_entry.text()

        try:
            recipient = decode_address(recipient_str)
        except Exception as e:
            QMessageBox.critical(self, "DogeGB", f"Bad recipient address:\n{e}")
            return

        try:
            amount_doge = float(amount_str)
            if amount_doge <= 0:
                raise ValueError("amount must be positive")
            send_value = doge_to_koinu(amount_doge)
        except Exception as e:
            QMessageBox.critical(self, "DogeGB", f"Bad amount:\n{e}")
            return

        try:
            fee_rate_doge_per_kb = float(fee_rate_str)
            if fee_rate_doge_per_kb < 0:
                raise ValueError("fee rate must be non-negative")
            fee_rate_koinu_per_byte = max(
                1, int(round(fee_rate_doge_per_kb * KOINU_PER_DOGE / 1000))
            )
        except Exception as e:
            QMessageBox.critical(self, "DogeGB", f"Bad fee rate:\n{e}")
            return

        op_return_bytes = b""
        op_return_value = 0
        if op_return_text:
            op_return_bytes = op_return_text.encode("utf-8")
            if len(op_return_bytes) > 80:
                QMessageBox.critical(
                    self, "DogeGB",
                    f"OP_RETURN payload is {len(op_return_bytes)} bytes; max is 80.",
                )
                return
            op_return_value_str = self.op_return_value_entry.text().strip() or "0"
            try:
                op_return_value = doge_to_koinu(float(op_return_value_str))
                if op_return_value < 0:
                    raise ValueError("value must be non-negative")
            except Exception as e:
                QMessageBox.critical(self, "DogeGB", f"Bad OP_RETURN value:\n{e}")
                return

        try:
            sel = select_coins_greedy(
                self.utxos,
                send_value=send_value,
                fee_rate_koinu_per_byte=fee_rate_koinu_per_byte,
                has_op_return=bool(op_return_bytes),
                op_return_len=len(op_return_bytes),
                recipient_is_p2sh=(recipient.address_type == "p2sh"),
            )
        except InsufficientFundsError as e:
            QMessageBox.critical(self, "DogeGB", str(e))
            return

        inputs = [
            TxInput(
                prev_txid=u["txid"],
                prev_vout=u["vout"],
                value=u["value"],
                prev_script_pubkey=u["script_pubkey"],
            )
            for u in sel.chosen
        ]

        outputs: list[TxOutput] = []
        outputs.append(TxOutput(send_value, recipient.script_pubkey()))
        if op_return_bytes:
            outputs.append(TxOutput(op_return_value, script_op_return(op_return_bytes)))
        if sel.change > 0:
            outputs.append(TxOutput(sel.change, self.watch_address.script_pubkey()))

        proposal_payload = pack_tx_proposal(inputs, outputs)
        envelope = pack_message(MSG_TX_PROPOSAL, proposal_payload)
        unsigned_tx = serialize_unsigned_tx(inputs, outputs)

        self.last_proposal_bytes = envelope
        self.last_proposal_inputs = inputs
        self.last_proposal_outputs = outputs
        self.last_unsigned_tx_hex = unsigned_tx.hex()

        op_return_line = (
            f"\n  OP_RETURN  {len(op_return_bytes):3d} bytes  ({op_return_text!r})"
            + (f"  value: {format_doge(op_return_value)} DOGE" if op_return_value else "")
            if op_return_bytes else ""
        )
        change_line = (
            f"\n  → change to {self.watch_address.original}: {format_doge(sel.change)} DOGE"
            if sel.change > 0 else ""
        )
        change_skip_line = (
            "\n  (no change output — leftover absorbed into fee)"
            if sel.change == 0 and sel.total_in > sel.total_out + sel.fee else ""
        )
        est_size = estimate_signed_tx_size(
            num_inputs=len(inputs),
            num_p2pkh_outputs=sum(1 for o in outputs if o.script_pubkey[:3] == b"\x76\xa9\x14"),
            num_p2sh_outputs=sum(1 for o in outputs if o.script_pubkey[:2] == b"\xa9\x14"),
            op_return_len=len(op_return_bytes),
        )
        summary = (
            f"  {len(inputs)} inputs  ·  {len(outputs)} outputs  ·  "
            f"~{est_size} bytes signed  ·  proposal {len(proposal_payload)} bytes\n"
            f"  → {recipient.original}: {format_doge(send_value)} DOGE"
            f"{change_line}{change_skip_line}{op_return_line}\n"
            f"  fee: {format_doge(sel.fee)} DOGE  "
            f"(@ {fee_rate_doge_per_kb:.4f} DOGE/kB)"
        )
        self.review_summary.setText(summary)
        self.review_summary.setStyleSheet(f"color: {TEXT}; font-size: 11px;")
        self._set_hex_box(self.proposal_hex_box, envelope.hex())
        self._set_hex_box(self.unsigned_hex_box, unsigned_tx.hex())

        self._log(
            f"Built proposal: {len(inputs)} in, {len(outputs)} out, "
            f"{len(proposal_payload)} payload bytes (envelope {len(envelope)}).",
            "good",
        )

        if self.transport is not None:
            self.send_btn.setEnabled(True)

    # ------------------------------------------------------------------
    # IR transport
    # ------------------------------------------------------------------

    def _on_refresh_ports(self):
        ports = self._list_serial_ports()
        self.port_combo.clear()
        self.port_combo.addItems(ports)

    def _on_toggle_connection(self):
        if self.bridge is None:
            self._connect()
        else:
            self._disconnect()

    def _connect(self):
        port = self.port_combo.currentText().strip()
        if not port or port.startswith("("):
            QMessageBox.warning(self, "DogeGB", "Pick a serial port first.")
            return
        self.connect_btn.setEnabled(False)
        self.connect_btn.setText("Opening…")
        self._log(f"Opening {port} @ 115200 baud…")
        threading.Thread(target=self._do_connect, args=(port,), daemon=True).start()

    def _do_connect(self, port: str):
        try:
            bridge = ArduinoBridge(port, debug_log=lambda s: self._log("arduino: " + s, "dim"))
        except Exception as e:
            self._log(f"Open failed: {e}", "err")
            self._ui(lambda: QMessageBox.critical(self, "DogeGB", f"Open failed:\n{e}"))
            self._ui(lambda: (self.connect_btn.setEnabled(True), self.connect_btn.setText("Connect")))
            return

        if not bridge.ping(timeout_s=3.0):
            bridge.close()
            self._log("No PONG — is the bridge sketch flashed?", "err")
            self._ui(lambda: QMessageBox.critical(
                self, "DogeGB",
                "Connected to the port but the Arduino didn't reply to PNG.\n"
                "Make sure arduino_bridge.ino is flashed.",
            ))
            self._ui(lambda: (self.connect_btn.setEnabled(True), self.connect_btn.setText("Connect")))
            return

        self.bridge = bridge
        self.transport = ChunkedTransport(bridge)
        self._log("Bridge online — got PONG.", "good")

        def update_ui():
            self.connect_btn.setEnabled(True)
            self.connect_btn.setText("Disconnect")
            self.connection_status.setText("● connected")
            self.connection_status.setStyleSheet(f"color: {SUCCESS}; font-size: 11px;")
            if self.last_proposal_bytes is not None:
                self.send_btn.setEnabled(True)
            self.receive_btn.setEnabled(True)
        self._ui(update_ui)

    def _disconnect(self):
        if self.bridge is not None:
            self.bridge.close()
        self.bridge = None
        self.transport = None
        self.connect_btn.setText("Connect")
        self.connection_status.setText("○ disconnected")
        self.connection_status.setStyleSheet(f"color: {TEXT_DIM}; font-size: 11px;")
        self.send_btn.setEnabled(False)
        self.receive_btn.setEnabled(False)
        self.broadcast_btn.setEnabled(False)
        self._log("Disconnected.", "dim")

    def _on_send_proposal(self):
        if self.transport is None or self.last_proposal_bytes is None:
            return
        reply = QMessageBox.question(
            self, "Send to DogeGB?",
            "Point your DogeGB at the IR sensor and put it in 'Receive' mode, "
            "then click Yes. This will take a few seconds.",
        )
        if reply != QMessageBox.StandardButton.Yes:
            return
        payload = self.last_proposal_bytes
        self._log(f"Sending {len(payload)} bytes (chunked) over IR…")
        self.send_btn.setEnabled(False)
        threading.Thread(target=self._do_send, args=(payload,), daemon=True).start()

    def _do_send(self, payload: bytes):
        def progress(done, total):
            self._ui(lambda: self._set_progress(done / total if total else 0))
            self._log(f"  chunk {done}/{total} acked", "dim")
        try:
            assert self.transport is not None
            self.transport.send_message(payload, progress=progress)
            self._log("Proposal delivered. The GBC should now display it for confirmation.", "good")
        except TransportError as e:
            self._log(f"Send failed: {e}", "err")
        finally:
            self._ui(lambda: self.send_btn.setEnabled(True))
            self._ui(lambda: self._set_progress(0))

    def _on_receive_signed(self):
        if self.transport is None:
            return
        self._log("Listening for signed tx from GBC… (sign on the GBC, then transmit)")
        self.receive_btn.setEnabled(False)
        threading.Thread(target=self._do_receive, daemon=True).start()

    def _do_receive(self):
        try:
            assert self.transport is not None
            payload = self.transport.receive_message(first_chunk_timeout_ms=120_000)
            msg_type, body = unpack_message(payload)
            if msg_type != MSG_SIGNED_TX:
                self._log(f"Got message type 0x{msg_type:02x}, expected SIGNED_TX.", "warn")
                return
            self.last_signed_tx_hex = body.hex()
            self._log(
                f"Got signed tx: {len(body)} bytes — {self.last_signed_tx_hex[:60]}…",
                "good",
            )
            self._ui(lambda: self.broadcast_btn.setEnabled(True))
        except TransportError as e:
            self._log(f"Receive failed: {e}", "err")
        except Exception as e:
            self._log(f"Decode failed: {e}", "err")
        finally:
            self._ui(lambda: self.receive_btn.setEnabled(True))

    def _on_broadcast(self):
        if not self.last_signed_tx_hex:
            return
        reply = QMessageBox.question(
            self, "Broadcast signed tx?",
            "This will publish the transaction to the Dogecoin network. "
            "Make sure you've reviewed it on the GBC. Continue?",
        )
        if reply != QMessageBox.StandardButton.Yes:
            return
        self.broadcast_btn.setEnabled(False)
        self._log("Broadcasting…")
        threading.Thread(target=self._do_broadcast, daemon=True).start()

    def _do_broadcast(self):
        try:
            txid = utxo_api.broadcast_tx(self.last_signed_tx_hex)
            self._log(f"Broadcast OK — txid {txid}", "good")
            self._ui(lambda: QMessageBox.information(
                self, "Broadcast OK",
                f"Transaction accepted by the network.\ntxid: {txid}",
            ))
        except Exception as e:
            self._log(f"Broadcast failed: {e}", "err")
            self._ui(lambda: QMessageBox.critical(self, "DogeGB", f"Broadcast failed:\n{e}"))
        finally:
            self._ui(lambda: self.broadcast_btn.setEnabled(True))

    def closeEvent(self, event):
        if self.bridge is not None:
            try:
                self.bridge.close()
            except Exception:
                pass
        event.accept()


# --- Entrypoint -----------------------------------------------------------

def main():
    app = QApplication(sys.argv)
    app.setStyleSheet(STYLESHEET)
    window = DogeGBCompanion()
    window.show()
    sys.exit(app.exec())


if __name__ == "__main__":
    main()
