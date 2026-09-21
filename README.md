# TNM Vision — UI Machine

Giao diện HMI Qt Widgets dành cho trạm kiểm tra bề mặt vải của TNM Vision. Repo này chỉ chứa lớp giao diện vận hành; camera, mô hình detect và logic xử lý ảnh được giữ trong backend TNM Vision.

![Giao diện Night Shift hiện tại](docs/qt-runtime-preview.png)

## Trạng thái hiện tại

- Giao diện **Night Shift** theo bố cục máy công nghiệp, tối ưu cho màn hình vận hành và môi trường ánh sáng thấp.
- Hiển thị khung detect runtime ở bên trái và ảnh vùng nghi ngờ ở bên phải.
- Có danh sách ảnh sự kiện gần đây, chọn lại ảnh cũ hoặc tự theo ảnh mới nhất.
- Hiển thị tốc độ xử lý, thời gian mỗi khung hình, số khung đã kiểm tra và trạng thái kết nối.
- Có chế độ mô phỏng chạy độc lập, không cần camera hoặc backend.
- Có chế độ kết nối backend TNM Vision qua HTTP trên máy cục bộ.
- Logo TNM được đóng gói cùng ứng dụng.
- Hỗ trợ cửa sổ thường và toàn màn hình.

Thiết kế tham khảo đã duyệt nằm tại [docs/concept-night-shift.png](docs/concept-night-shift.png).

## Kiến trúc

```text
Camera + detector (TNM Vision) ── localhost HTTP ──> UI-machine (Qt Widgets)
                                                     │
                                                     └── DemoRuntime khi chạy độc lập
```

UI-machine không điều khiển chuyển động máy, PLC, GPIO hoặc cơ cấu chấp hành. Các nút Bắt đầu/Dừng chỉ điều khiển phiên xử lý ảnh.

## Yêu cầu

- Python 3.10 đến 3.13
- PySide6 Essentials 6.7 trở lên, dưới phiên bản 7
- Windows hoặc Linux có môi trường đồ họa

Ứng dụng được chọn theo hướng Qt Widgets để giảm số lớp runtime và phù hợp với Orange Pi 5 Plus. Hiệu năng trên thiết bị thật vẫn cần được đo cùng camera và detector mục tiêu.

## Cài đặt

### Windows

```powershell
git clone git@github.com:tranthinhembedded/UI-machine.git
cd UI-machine
python -m venv .venv
.\.venv\Scripts\python.exe -m pip install -e .
```

### Linux / Orange Pi

```bash
git clone git@github.com:tranthinhembedded/UI-machine.git
cd UI-machine
python3 -m venv .venv
. .venv/bin/activate
python -m pip install -e .
```

Nếu hệ điều hành chưa có Qt runtime hoặc plugin hiển thị, hãy cài các gói hệ thống tương ứng với bản Linux đang dùng trước khi khởi chạy.

## Chạy chế độ mô phỏng

Chế độ này tạo luồng vải và vùng nghi ngờ giả lập ngay trong ứng dụng, phù hợp để duyệt giao diện hoặc kiểm tra màn hình khi chưa có camera.

### Windows

```powershell
.\.venv\Scripts\ui-machine.exe --autostart demo
```

### Linux / Orange Pi

```bash
.venv/bin/ui-machine --autostart demo
```

Ví dụ chạy toàn màn hình với giới hạn 5 FPS và chiều rộng khung xử lý tối đa 640 px:

```bash
ui-machine --autostart demo --fullscreen --fps 5 --width 640
```

## Kết nối backend TNM Vision

1. Khởi động runtime server trong repo TNM Vision. Ví dụ trên Windows:

   ```powershell
   .\.venv\Scripts\python.exe -m fabric_inspection.runtime_server --no-browser
   ```

2. Sao chép đúng URL có `PORT` và `TOKEN` mà backend in ra.

3. Khởi động giao diện bằng URL đó:

   ```powershell
   .\.venv\Scripts\ui-machine.exe --api-url http://127.0.0.1:PORT/TOKEN/ --autostart camera --fullscreen
   ```

Giao diện hiện dùng các endpoint sau:

| Phương thức | Endpoint | Mục đích |
| --- | --- | --- |
| `GET` | `api/state` | Trạng thái runtime, thống kê và danh sách sự kiện |
| `GET` | `image/frame/{id}` | Khung hình đầy đủ của sự kiện |
| `GET` | `image/crop/{id}` | Ảnh cắt vùng nghi ngờ |
| `GET` | `image/context/{id}` | Ảnh ngữ cảnh quanh vùng nghi ngờ |
| `POST` | `api/start` | Bắt đầu phiên xử lý ảnh |
| `POST` | `api/stop` | Dừng phiên xử lý ảnh |

## Tham số dòng lệnh

| Tham số | Ý nghĩa | Mặc định |
| --- | --- | --- |
| `--autostart demo` | Mở ứng dụng và tự chạy dữ liệu mô phỏng | Không tự chạy |
| `--autostart camera` | Mở ứng dụng và tự kết nối backend | Không tự chạy |
| `--api-url URL` | URL gốc có token của TNM Vision runtime | Không có |
| `--fps N` | Giới hạn tốc độ xử lý mô phỏng, từ 1 đến 15 FPS | `5` |
| `--width N` | Chiều rộng xử lý mô phỏng, từ 320 đến 1280 px | `640` |
| `--fullscreen` | Khởi động ở chế độ toàn màn hình | Tắt |
| `--size RỘNGxCAO` | Kích thước cửa sổ, tối thiểu 800 × 600 | `1280x800` |

Xem danh sách đầy đủ trực tiếp từ bản đang cài:

```bash
ui-machine --help
```

## Thao tác vận hành

- **Bắt đầu kiểm tra**: bắt đầu dữ liệu mô phỏng hoặc gửi lệnh bắt đầu tới backend.
- **Dừng kiểm tra**: dừng phiên xử lý ảnh; không điều khiển chuyển động của máy.
- **Theo ảnh mới nhất**: tự chuyển vùng nghi ngờ sang sự kiện mới nhất.
- **Chọn ảnh trong danh sách**: giữ ảnh đã chọn để đối chiếu.
- **Xem toàn cảnh**: chuyển từ ảnh cắt sang khung đầy đủ của sự kiện.
- `F11`: bật hoặc tắt toàn màn hình.
- `Escape`: rời toàn màn hình.

Danh sách sự kiện là các ảnh chụp tại thời điểm phát hiện và không đại diện trực tiếp cho số lỗi vải độc lập.

## Cấu trúc chính

```text
src/ui_machine/
├── app.py                  Giao diện Qt và luồng tương tác
├── runtime.py              DemoRuntime và HTTP runtime adapter
└── assets/logo.png         Logo TNM đóng gói cùng ứng dụng
tests/test_runtime.py       Kiểm thử runtime
docs/
├── qt-runtime-preview.png  Ảnh giao diện Qt hiện tại
└── concept-night-shift.png Thiết kế Night Shift tham khảo
```

## Phát triển và kiểm tra

```bash
python -m pip install -e ".[dev]"
python -m pytest
python -m ruff check .
```

## Giới hạn hiện tại

- Chưa benchmark FPS, CPU, RAM và nhiệt độ trên Orange Pi 5 Plus với camera và detector thật.
- Chế độ camera cần TNM Vision runtime hoạt động và driver camera tương thích với thiết bị.
- Repo chưa có phần đóng gói tự khởi động cùng hệ điều hành hoặc triển khai dạng kiosk.
- Chưa tích hợp điều khiển PLC, GPIO, băng tải hoặc cơ cấu loại sản phẩm lỗi.

## Bản quyền

Phần mềm nội bộ của TNM Vision. Không phân phối nếu chưa có sự cho phép.
