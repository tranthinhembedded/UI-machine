# TNM Vision trên ARM

Ứng dụng giao diện Qt 6 dùng OpenCV 4 và Hikrobot MVS SDK để thu nhận ảnh từ camera công nghiệp. Dự án được cấu hình để tìm MVS tại `/opt/MVS` và tự chọn thư viện `aarch64`/`arm64` khi build trên ARM 64-bit.

## 1. Cài Hikrobot MVS SDK cho ARM

### Kiểm tra kiến trúc

Trên Orange Pi hoặc bo ARM, chạy:

```bash
uname -m
dpkg --print-architecture
```

Với kết quả `aarch64` hoặc `arm64`, cần tải gói **Linux ARM 64-bit / aarch64**. Không dùng gói `x86_64`, vì thư viện trong gói đó không thể chạy trên ARM.

### Tải MVS

1. Mở [trang tải xuống Machine Vision của Hikrobot](https://www.hikrobotics.com/en/machinevision/service/download?module=0).
2. Tìm **MVS (Linux)** hoặc **Machine Vision Industrial Camera SDK (Linux)**.
3. Chọn bản dành cho **ARM 64-bit / aarch64**, chấp nhận điều khoản và tải gói mới nhất.
4. Chép gói vừa tải sang bo ARM, ví dụ:

```bash
scp MVS-*_aarch64_*.tar.gz orangepi@<IP_ARM>:~/
```

Tên và cách đóng gói có thể thay đổi theo phiên bản. Luôn chọn đúng kiến trúc; không nên dùng một URL tải trực tiếp cũ vì Hikrobot có thể thay liên kết và yêu cầu xác nhận điều khoản tải xuống.

### Cài đặt trên ARM

Nếu file tải về là `.zip`, giải nén trước và chọn file `aarch64` bên trong:

```bash
sudo apt update
sudo apt install -y unzip tar
unzip MVS_Linux_*.zip
tar -xzf MVS-*_aarch64_*.tar.gz
cd MVS-*_aarch64_*
sudo ./setup.sh
```

Nếu gói cung cấp file `.deb` cho ARM64, có thể cài bằng:

```bash
sudo apt install ./MVS-*_aarch64_*.deb
```

Sau khi cài, đăng xuất/đăng nhập lại hoặc khởi động lại bo để các biến môi trường, dịch vụ và quy tắc thiết bị có hiệu lực:

```bash
sudo reboot
```

### Kiểm tra SDK

```bash
test -f /opt/MVS/include/MvCameraControl.h && echo "MVS header: OK"
find /opt/MVS/lib -name 'libMvCameraControl.so*' -print
ldconfig -p | grep MvCameraControl
```

Nếu `ldconfig` chưa thấy thư viện, xác định thư mục chứa `libMvCameraControl.so` rồi thêm nó vào cấu hình linker. Với gói aarch64 thông dụng:

```bash
echo /opt/MVS/lib/aarch64 | sudo tee /etc/ld.so.conf.d/hikrobot-mvs.conf
sudo ldconfig
```

Nếu gói dùng `/opt/MVS/lib/arm64`, thay đường dẫn trên bằng thư mục đó. Có thể mở công cụ MVS để kiểm tra camera trước khi chạy ứng dụng:

```bash
/opt/MVS/bin/MVS
```

> Trình cài MVS cần quyền quản trị vì có thể cài driver, dịch vụ, udev rule và thay đổi cấu hình mạng. Nên đọc `README`/hướng dẫn đi kèm đúng phiên bản trước khi chạy trên máy sản xuất.

## 2. Chuẩn bị bo ARM cho Qt

Cách đơn giản và ít lỗi ABI nhất là **build trực tiếp trên bo ARM qua Qt Creator Remote Linux**. Qt, OpenCV và MVS khi đó đều là bản native ARM trên cùng thiết bị.

```bash
sudo apt update
sudo apt install -y \
  build-essential cmake ninja-build pkg-config \
  qt6-base-dev qt6-base-dev-tools \
  libopencv-dev \
  openssh-server gdbserver
sudo systemctl enable --now ssh
```

Kiểm tra các công cụ:

```bash
cmake --version
g++ --version
qtpaths6 --version
pkg-config --modversion opencv4
hostname -I
```

Tên package Qt 6 có thể khác trên image Linux của bo. Nếu `apt` không tìm thấy `qt6-base-dev`, cần dùng repository của bản phân phối hoặc tự cài Qt 6 cho ARM.

## 3. Remote Qt Creator tới ARM

### 3.1 Tạo kết nối SSH

Từ máy phát triển, kiểm tra trước:

```bash
ssh orangepi@<IP_ARM>
```

Trong Qt Creator:

1. Mở **Preferences > Devices > Devices**.
2. Chọn **Add > Remote Linux Device**.
3. Nhập IP, cổng `22` và tài khoản trên bo ARM.
4. Tạo/chọn SSH key, chọn **Deploy Public Key**, rồi bấm **Test**.

Hướng dẫn chính thức: [Add remote Linux devices](https://doc.qt.io/qtcreator/creator-how-to-add-remote-linux.html).

### 3.2 Tạo kit build trực tiếp trên ARM

Trong Qt Creator, thêm từng công cụ bằng cách chọn đường dẫn **Remote** trên thiết bị ARM:

- **Preferences > CMake > Tools**: `/usr/bin/cmake`.
- **Preferences > Kits > Qt Versions**: chọn `qmake6` hoặc công cụ Qt 6 mà Qt Creator phát hiện trên ARM.
- **Preferences > Kits > Compilers**: `/usr/bin/gcc` và `/usr/bin/g++`.
- **Preferences > Kits > Debuggers**: `/usr/bin/gdb` nếu có; `gdbserver` phải được cài trên bo.
- **Preferences > Kits**: tạo kit mới, đặt **Build device** và **Run device** là bo ARM, rồi gán Qt, compiler, debugger và CMake vừa thêm.

Một số bản Qt Creator có nút **Run Auto-Detection Now** và **Create Kits** trong cấu hình thiết bị; có thể dùng các nút này thay cho khai báo thủ công.

Quy trình chính thức: [Build applications on remote Linux devices](https://doc.qt.io/qtcreator/creator-how-to-build-on-remote-linux.html).

### 3.3 Mở và build dự án

Source phải tồn tại trên bo ARM. Có thể chép bằng `rsync`:

```bash
rsync -av --delete \
  /duong-dan/ui_machine_v1/src/ \
  orangepi@<IP_ARM>:~/ui_machine_v1/src/
```

Không dùng `--delete` nếu thư mục đích có dữ liệu cần giữ. Trong Qt Creator, chọn **File > Open From Device**, mở `CMakeLists.txt` trên ARM và chọn kit Remote ARM.

Cũng có thể kiểm tra build ngay trên bo:

```bash
cd ~/ui_machine_v1/src
cmake -S . -B build -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DMVS_ROOT=/opt/MVS
cmake --build build -j"$(nproc)"
```

Chạy ở chế độ cửa sổ:

```bash
./build/untitled --windowed
```

Không truyền `--windowed` thì ứng dụng khởi động toàn màn hình.

### 3.4 Deploy và chạy từ Qt Creator

Trong **Projects > Deploy Settings**, giữ bước upload qua SFTP hoặc dùng bước CMake Install. `CMakeLists.txt` hiện đã có lệnh cài executable vào `bin`. Trong **Projects > Run Settings**, kiểm tra executable từ xa và working directory, sau đó bấm **Run** hoặc **Debug**. Qt Creator sẽ kết nối, deploy và chạy chương trình trên bo; log xuất hiện trong **Application Output**.

Tài liệu tham khảo: [Deploy applications to remote Linux devices](https://doc.qt.io/qtcreator/creator-deployment-embedded-linux.html) và [Run on remote Linux devices](https://doc.qt.io/qtcreator/creator-how-to-run-on-remote-linux.html).

## 4. Hiển thị giao diện từ xa

Nếu bo có màn hình gắn trực tiếp, ứng dụng Qt sẽ xuất hiện trên màn hình đó. Khi chạy qua SSH mà cần hiển thị cửa sổ trên máy phát triển, có thể dùng X11 forwarding:

```bash
ssh -X orangepi@<IP_ARM>
cd ~/ui_machine_v1/src
./build/untitled --windowed
```

Máy phát triển phải có X server; bo ARM cần `xauth`. Trong Qt Creator cũng có tùy chọn **Use X11 forwarding** tại **Projects > Run Settings**. X11 qua mạng phù hợp để cấu hình/debug, nhưng không lý tưởng cho luồng ảnh camera tốc độ cao; khi vận hành nên hiển thị trực tiếp trên bo hoặc dùng remote desktop/VNC trong mạng nội bộ.

Nếu chạy qua SSH vào desktop đang mở trên bo và gặp lỗi `could not connect to display`, cần truyền đúng display và quyền truy cập của phiên desktop, ví dụ:

```bash
export DISPLAY=:0
export XAUTHORITY=/home/orangepi/.Xauthority
./build/untitled --windowed
```

Thay `orangepi` bằng user thực tế. Không chạy ứng dụng GUI bằng `sudo` nếu không cần thiết.

## 5. Lỗi thường gặp

- **`Hikrobot MVS SDK not found`**: kiểm tra `/opt/MVS/include/MvCameraControl.h`, file `libMvCameraControl.so`, hoặc truyền `-DMVS_ROOT=<duong-dan-MVS>`.
- **`wrong ELF class` / `Exec format error`**: đã cài nhầm SDK x86 hoặc ARM 32-bit; cài lại bản aarch64/ARM64.
- **Không tìm thấy `libMvCameraControl.so` khi chạy**: chạy `sudo ldconfig` và kiểm tra thư mục trong `/etc/ld.so.conf.d/hikrobot-mvs.conf`.
- **Không thấy camera USB**: rút/cắm lại sau khi cài MVS, kiểm tra `lsusb` và udev rule của SDK.
- **Không thấy camera GigE**: đặt camera và cổng Ethernet cùng subnet; kiểm tra firewall, MTU và packet size bằng công cụ MVS.
- **Qt Creator không deploy được**: kiểm tra SSH/SFTP và quyền ghi của thư mục đích.
- **Không build được source hiện tại**: `CMakeLists.txt` còn tham chiếu `resources.qrc` và thư mục `pp7_camera_cpp`, nhưng hai thành phần này chưa có trong thư mục hiện tại. Cần bổ sung chúng từ bản source đầy đủ trước khi build.

