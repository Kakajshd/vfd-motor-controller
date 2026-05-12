# ESP32-S3 VFD Motor Speed Controller

> **Hệ thống điều khiển tốc độ động cơ bộ tời dây tự động** sử dụng ESP32-S3, cảm biến siêu âm, giao tiếp Modbus RTU (RS-485), hiển thị OLED, web server thời gian thực, và công cụ cài đặt trên PC.

---

## Tổng quan hệ thống

Hệ thống đo khoảng cách cuộn dây qua cảm biến siêu âm, tính toán tần số mục tiêu theo đường đặc tính tuyến tính, sau đó truyền lệnh tốc độ tới biến tần (VFD) qua Modbus RTU/RS-485.  
Khi cảm biến căng dây kích hoạt, hệ thống tự động tăng tốc (boost) theo 3 cấp độ có thể cấu hình.

```
┌──────────────┐    Modbus RTU     ┌─────────────┐
│   ESP32-S3   │ ────────────────► │  VFD (RS485)│
│              │                   └─────────────┘
│  + Ultrasonic│◄── dist sensor
│  + Dancer B  │◄── pull sensor
│  + OLED 1.3" │
│  + WiFi AP   │──── Web UI (realtime SSE)
└──────────────┘
        │ USB CDC
        ▼
  PC App (dieuchinh.exe)
```

---

## Tính năng

| Tính năng | Mô tả |
|---|---|
| **Điều khiển tốc độ tự động** | Bản đồ khoảng cách → tần số tuyến tính (UDMIN/UDMAX → UFMIN/UFMAX) |
| **Boost 3 cấp** | Tự động tăng tốc khi cảm biến căng dây kích hoạt, có escalation và decay |
| **Modbus RTU** | Giao tiếp RS-485 không chặn (FreeRTOS task riêng) |
| **Web UI** | Dashboard realtime qua Server-Sent Events (SSE) |
| **OLED display** | Hiển thị trạng thái trực tiếp trên phần cứng (SH1106 128×64) |
| **PC Config Tool** | App Python/tkinter: cài đặt tham số qua USB CDC, xem log live |
| **Lưu cài đặt** | Persistent storage bằng ESP32 NVS (Preferences) |
| **WiFi STA/AP** | Kết nối vào mạng có sẵn, tự động reconnect |

---

## Cấu trúc dự án

```
esp_32_s3_vfd/
├── src/
│   ├── main.cpp            # Vòng lặp chính, xử lý cảm biến, logic boost
│   ├── Config.h            # Định nghĩa chân GPIO, tham số phần cứng
│   ├── Settings.h          # Struct cài đặt, load/save NVS
│   ├── VFDManager.h        # Giao tiếp Modbus RTU với biến tần
│   ├── WiFiWebManager.h    # WiFi + AsyncWebServer + SSE
│   └── WiFiWebManager.cpp  # Triển khai web server, routes, events
├── app.py                  # PC config tool (Python/tkinter)
├── build.py                # Script tự động build ra dieuchinh.exe
├── adjustments.png         # Icon cho app PC
├── platformio.ini          # Cấu hình PlatformIO
└── .github/
    └── instructions/       # Copilot coding instructions
```

---

## Yêu cầu phần cứng

| Linh kiện | Model / Ghi chú |
|---|---|
| Vi điều khiển | ESP32-S3 DevKitM-1 |
| Biến tần | Bất kỳ VFD hỗ trợ Modbus RTU (slave ID cấu hình trong `Config.h`) |
| Module RS-485 | MAX485 hoặc tương đương (cần chân DE/RE) |
| Cảm biến siêu âm | HC-SR04 hoặc tương đương |
| Cảm biến căng dây | Công tắc NPN/PNP, normally open |
| Màn hình OLED | SH1106 128×64, giao tiếp I2C |

### Sơ đồ chân (GPIO)

| Chức năng | GPIO | Ghi chú |
|---|---|---|
| I2C SDA (OLED) | 8 | |
| I2C SCL (OLED) | 9 | |
| Cảm biến căng dây B | 10 | INPUT_PULLUP |
| TRIG cảm biến siêu âm | 4 | |
| ECHO cảm biến siêu âm | 5 | |
| MAX485 DE/RE | 16 | Điều khiển hướng RS-485 |
| RS-485 TX | 17 | UART2 TX |
| RS-485 RX | 18 | UART2 RX |

> Thay đổi chân tại [`src/Config.h`](src/Config.h).

---

## Cài đặt & Build firmware

### Yêu cầu

- [Visual Studio Code](https://code.visualstudio.com/)
- Extension [PlatformIO IDE](https://platformio.org/install/ide?install=vscode)

### Build và nạp

```bash
# Clone dự án
git clone https://github.com/Kakajshd/vfd-motor-controller.git
cd vfd-motor-controller

# Mở trong VS Code, PlatformIO tự cài dependencies
# Hoặc dùng CLI:
pio run --target upload
```

### Dependencies (tự động cài qua PlatformIO)

```ini
lib_deps =
    4-20ma/ModbusMaster@^2.0.1
    teckel12/NewPing@^1.9.7
    adafruit/Adafruit GFX Library@^1.12.5
    adafruit/Adafruit SH110X@^2.1.14
    https://github.com/me-no-dev/AsyncTCP.git
    https://github.com/me-no-dev/ESPAsyncWebServer.git
```

---

## Cấu hình

Chỉnh sửa [`src/Config.h`](src/Config.h) trước khi build:

```cpp
// WiFi mặc định (có thể thay đổi qua Web UI sau)
#define WIFI_SSID "your_ssid"
#define WIFI_PASS "your_password"

// Modbus ID của biến tần
#define UNWIND_VFD_ID 1

// Baud rate Modbus
#define VFD_BAUD 9600
```

---

## PC Config Tool (dieuchinh.exe)

Giao diện PC kết nối qua USB CDC để cài đặt tham số và xem log trực tiếp.

### Chạy từ source

```bash
pip install pyserial pillow
python app.py
```

### Build ra exe

```bash
python build.py
# Output: dist/dieuchinh.exe
```

### Giao diện

- **Kết nối tự động**: Tự nhận dạng cổng COM có ESP32 (quét VID/PID)
- **Thanh thông tin server**: Hiển thị địa chỉ IP web server realtime
- **Bảng cài đặt**: UDMIN/UDMAX, UFMIN/UFMAX, Boost L1-L3, Hold, Escalate, Decay
- **Apply / RESET**: Gửi cài đặt và khởi động lại ESP32
- **Log**: Xem toàn bộ output serial từ thiết bị

---

## Web UI

Sau khi ESP32 kết nối WiFi, truy cập `http://<IP>` trên trình duyệt:

- Dashboard realtime: khoảng cách, tần số, boost status
- Chỉnh sửa cài đặt WiFi
- Điều khiển OLED rotation
- Xem telemetry: heap, uptime, Modbus stats

---

## Serial Commands

Giao tiếp qua Serial Monitor (115200 baud):

| Lệnh | Tác dụng |
|---|---|
| `INFO` | In toàn bộ cài đặt hiện tại |
| `RUN` | Chuyển sang chế độ tự động |
| `PING` | Chế độ test cảm biến |
| `STOP` | Dừng khẩn cấp |
| `SAVE` | Lưu cài đặt vào NVS |
| `RESET` | Khởi động lại ESP32 |
| `UDMIN=15.0` | Đặt khoảng cách đầy (cm) |
| `UDMAX=80.0` | Đặt khoảng cách rỗng (cm) |
| `UFMIN=20.0` | Tần số tối thiểu (Hz) |
| `UFMAX=50.0` | Tần số tối đa (Hz) |
| `BOOSTL1=20.0` | % tăng tốc cấp 1 |
| `BOOSTL2=35.0` | % tăng tốc cấp 2 |
| `BOOSTL3=50.0` | % tăng tốc cấp 3 |
| `BOOSTDECAY=5.0` | Thời gian giảm cấp (giây) |

---

## Logic điều khiển

```
dist = sonar_read()
freq = map(dist, UDMIN, UDMAX, UFMIN, UFMAX)

if dancer_sensor_active:
    escalate boost_level (1 → 2 → 3) theo thời gian kích
    
if boost_level > 0:
    freq *= (1 + boost_pct / 100)
    
send_to_VFD_via_Modbus(freq)
```

### Boost State Machine

```
IDLE ──(sensor ON)──► BOOST_L1 ──(t > ESC2)──► BOOST_L2 ──(t > ESC3)──► BOOST_L3
  ▲                      │
  └──(decay elapsed)─────┘  (sau khi sensor OFF → hold → decay từng cấp)
```

---

## License

MIT License — Xem file [LICENSE](LICENSE).

---

## Tác giả

**Hung** — Staff FA  
Dự án phát triển nội bộ cho hệ thống điều khiển dây cuộn công nghiệp.
