# Tai lieu ban giao du an ESP32-S3 VFD Motor Controller

## 1. Thong tin ban giao

| Hang muc | Noi dung |
| --- | --- |
| Ten du an | ESP32-S3 VFD Motor Speed Controller |
| Muc dich | Dieu khien toc do dong co bo toi day/cuon day bang cam bien sieu am, cam bien cang day va bien tan VFD qua Modbus RTU RS-485 |
| Nen tang | ESP32-S3 DevKitM-1, Arduino framework, PlatformIO |
| Kien truc hien co | 1) Ban all-in-one co web local tren tung ESP32; 2) Ban phan tan master/node qua ESP-NOW |
| Repository | `https://github.com/Kakajshd/vfd-motor-controller.git` |
| Trang thai ban giao | Firmware co the build theo cac moi truong `ESP32S3`, `master`, `node`, `node1` ... `node10` |

## 2. Tom tat he thong

He thong doc khoang cach tu cam bien sieu am, tinh tan so muc tieu theo duong dac tinh `UDMIN/UDMAX -> UFMIN/UFMAX`, sau do gui lenh tan so cho VFD qua Modbus RTU. Khi cam bien cang day kich hoat, firmware tang toc theo 3 cap boost de bu toc va giam dan sau khi he thong on dinh.

Trong ban trien khai moi, co the dung mot ESP32-S3 lam `master` de gom du lieu nhieu may qua ESP-NOW. Moi `node` van tu dieu khien VFD cuc bo; master chi lam dashboard, giam sat va gui lenh cap cao.

```text
Che do all-in-one:
Cam bien sieu am + cam bien cang day -> ESP32-S3 -> RS-485/Modbus -> VFD
                                           |
                                           +-> WiFi STA -> Web UI local

Che do master/node:
Node 1..20 -> ESP-NOW kenh 6 -> ESP32-S3 Master -> WiFi AP VFD_MASTER -> Browser
```

## 3. Pham vi chuc nang

| Chuc nang | Mo ta | Ghi chu ky thuat |
| --- | --- | --- |
| Dieu khien tu dong | Tinh tan so theo khoang cach do duoc | Gioi han bang `UFMIN/UFMAX` |
| Boost 3 cap | Tang tan so khi cam bien cang day kich hoat | Co escalation, hold va decay |
| Loc spike khoang cach | Loai nhieu tang dot ngot tu cam bien sieu am | Nguong `DIST RISE/FALL` co the cau hinh |
| Modbus RTU | Ghi thanh ghi VFD qua RS-485 | Tan so: `0x2001`, lenh control: `0x2000` |
| Web UI local | Cai tham so va xem realtime tung ESP32 | Dung voi env `ESP32S3` |
| ESP-NOW dashboard | Giam sat nhieu node tap trung | Dung voi env `master` va `nodeX` |
| OLED | Hien thi mode, distance, boost, target frequency, VFD frequency | Mac dinh xoay 180 do |
| PC tool | Ung dung Python/tkinter gui tham so qua USB serial | File chinh: `app.py` |

## 4. Yeu cau phan cung

| Linh kien | Yeu cau |
| --- | --- |
| MCU | ESP32-S3 DevKitM-1 |
| VFD | Bien tan ho tro Modbus RTU RS-485 |
| RS-485 transceiver | MAX485 hoac tuong duong, co chan DE/RE |
| Cam bien sieu am | HC-SR04 hoac tuong duong |
| Cam bien cang day | Cong tac/cam bien dua tin hieu digital, dau vao dung `INPUT_PULLUP` |
| OLED | SH1106 128x64 I2C, dia chi mac dinh `0x3C` |
| Nguon | Nguon on dinh cho ESP32, cam bien va mach RS-485; tach nhieu cong nghiep neu can |

### So do ket noi mac dinh

| Chuc nang | GPIO | Ghi chu |
| --- | --- | --- |
| OLED SDA | 8 | I2C |
| OLED SCL | 9 | I2C |
| Cam bien cang day B | 10 | `INPUT_PULLUP`, active khi bi keo xuong muc LOW |
| Ultrasonic TRIG | 4 | Cam bien sieu am |
| Ultrasonic ECHO | 5 | Can dam bao muc dien ap phu hop voi ESP32 |
| MAX485 DE/RE | 16 | Dieu khien huong truyen RS-485 |
| RS-485 TX | 17 | UART2 TX |
| RS-485 RX | 18 | UART2 RX |
| Nut/ cong tac profile VFD | 20 | Active LOW: AUTO; inactive: MANUAL |

## 5. Cai dat moi truong phat trien

### 5.1 Cai cong cu

1. Cai Visual Studio Code.
2. Cai extension PlatformIO IDE.
3. Clone repository private:

```powershell
git clone https://github.com/Kakajshd/vfd-motor-controller.git
cd vfd-motor-controller
```

4. Neu dung PC tool tu source:

```powershell
python -m pip install pyserial pillow
python app.py
```

### 5.2 Build firmware

Dung PlatformIO CLI neu `pio` co trong `PATH`:

```powershell
pio run -e ESP32S3
pio run -e master
pio run -e node1
```

Neu `pio` khong co trong `PATH`, dung duong dan mac dinh tren Windows:

```powershell
& "$env:USERPROFILE\.platformio\penv\Scripts\platformio.exe" run -e ESP32S3
& "$env:USERPROFILE\.platformio\penv\Scripts\platformio.exe" run -e master
& "$env:USERPROFILE\.platformio\penv\Scripts\platformio.exe" run -e node1
```

### 5.3 Nap firmware

Ket noi ESP32-S3 qua USB, chon dung moi truong va nap:

```powershell
pio run -e ESP32S3 -t upload
pio run -e master -t upload
pio run -e node1 -t upload
```

## 6. Cau hinh trien khai

### 6.1 Chon kieu trien khai

| Kieu trien khai | Moi truong build | Khi nao dung |
| --- | --- | --- |
| All-in-one | `ESP32S3` | Moi may co web rieng, khong can master tap trung |
| Master dashboard | `master` | Mot ESP32 lam trung tam giam sat nhieu node |
| Node may | `node1` ... `node10` hoac `node` | Moi node dieu khien mot VFD/cum may |

### 6.2 Cau hinh WiFi cho all-in-one

Trong `src/Config.h`:

```cpp
#define WIFI_SSID "hung"
#define WIFI_PASS "123456789"
#define WIFI_MODE_AP false
```

Firmware uu tien WiFi da luu trong NVS neu nguoi dung da cau hinh truoc do. Neu muon xoa cau hinh cu, can nap firmware co logic xoa NVS hoac dung tool rieng de clear flash.

### 6.3 Cau hinh master/node ESP-NOW

Master tao AP:

```text
SSID: VFD_MASTER
PASS: 12345678
URL : http://192.168.4.1/
Kenh: 6
```

Quy trinh lay MAC master:

1. Flash ESP32 lam `master`.
2. Mo Serial Monitor baud `115200`.
3. Doc dong log:

```text
[MASTER] STA MAC for nodes: xx:xx:xx:xx:xx:xx
```

4. Cap nhat `src/Config.h`:

```cpp
#define ESP_NOW_MASTER_MAC {0x20, 0x6E, 0xF1, 0xA1, 0xD2, 0x80}
```

Moi node phai co ID rieng. Nen build bang env co san:

```powershell
pio run -e node1 -t upload
pio run -e node2 -t upload
...
pio run -e node10 -t upload
```

## 7. Huong dan su dung cho nguoi van hanh

### 7.1 Khoi dong he thong

1. Kiem tra day RS-485 A/B, nguon VFD, nguon ESP32 va cam bien.
2. Cap nguon cho VFD truoc, sau do cap nguon ESP32.
3. Quan sat OLED:
   - `MODE: AUTO/MANUAL/RUN/STOP/TEST`
   - `Distance`: khoang cach do duoc
   - `Target F`: tan so tinh toan
   - `VFD`: tan so da gui gan nhat
4. Neu dung master/node, ket noi dien thoai/laptop vao WiFi `VFD_MASTER`, mo `http://192.168.4.1/`.
5. Neu dung all-in-one, ket noi cung mang WiFi voi ESP32 va mo dia chi IP hien tren Serial Monitor/log.

### 7.2 Van hanh tren dashboard master

| Nut/ truong | Tac dung |
| --- | --- |
| `RUN` | Cho node chay che do dieu khien |
| `STOP` | Dung node, gui lenh stop VFD |
| `AUTO` | Ap profile VFD auto |
| `MANUAL` | Ap profile VFD manual |
| `Hz` | Gui tan so manual cho node, chi chap nhan trong khoang `UFMIN/UFMAX` |
| `SET` | Mo bang cai dat rieng cho node |

Sau khi nhan `SET` va `Apply To Node`, doi telemetry moi quay lai de xac nhan gia tri da cap nhat. Khong nen thay doi nhieu node dong loat khi may dang chay tai nang.

### 7.3 Van hanh web local all-in-one

| Khu vuc | Y nghia |
| --- | --- |
| Distance | Khoang cach cuon/day do tu cam bien |
| Boost Lv | Cap boost hien tai |
| Boost Hold | Thoi gian giu boost con lai |
| Target Frequency | Tan so muc tieu firmware tinh ra |
| VFD | Tan so da gui va trang thai giao tiep VFD |
| Form tham so | `UDMIN`, `UDMAX`, `UFMIN`, `UFMAX`, boost, spike filter |
| `Apply` | Luu tham so vao NVS va ap dung ngay |
| `RESET ESP` | Khoi dong lai ESP32 |
| `ROTATE OLED` | Xoay man hinh OLED |
| `VFD AUTO/MANUAL` | Ghi profile tham so VFD tu firmware |

### 7.4 Lenh serial bao tri

Mo Serial Monitor baud `115200`, line ending newline.

| Lenh | Tac dung |
| --- | --- |
| `INFO` | In cau hinh hien tai |
| `RUN` | Chuyen che do chay |
| `PING` | Test cam bien, khong dieu khien theo logic run |
| `STOP` | Dung VFD |
| `SAVE` | Luu tham so vao flash |
| `RESET` | Khoi dong lai ESP32 |
| `VFD=AUTO` | Ap profile VFD auto |
| `VFD=MANUAL` | Ap profile VFD manual |
| `UDMIN=10` | Dat khoang cach day |
| `UDMAX=50` | Dat khoang cach rong |
| `UFMIN=20` | Dat tan so toi thieu |
| `UFMAX=50` | Dat tan so toi da |
| `BOOSTL1=20` | Dat boost cap 1 theo % |
| `BOOSTL2=35` | Dat boost cap 2 theo % |
| `BOOSTL3=50` | Dat boost cap 3 theo % |
| `BOOSTH1=5` | Dat hold cap 1, giay |
| `BOOSTE2=0.8` | Thoi gian kich lien tuc de len cap 2 |
| `BOOSTE3=1.6` | Thoi gian kich lien tuc de len cap 3 |
| `BOOSTDECAY=5` | Thoi gian giam 1 cap boost |

## 8. Huong dan nhan biet trang thai

| Dau hieu | Trang thai co the |
| --- | --- |
| OLED hien `Distance: -` hoac dashboard distance `0` | Cam bien sieu am khong doc duoc, mat nguon cam bien, sai day TRIG/ECHO, vat can ngoai tam |
| Dashboard node `OFFLINE` | Node mat nguon, sai master MAC, sai kenh ESP-NOW, qua xa hoac nhieu RF |
| `VFD: FAIL` hoac `modbus_fail` tang | Loi RS-485/VFD, sai slave ID, sai baud, VFD chua san sang |
| `crc` tang tren master | Nhieu goi ESP-NOW hoac packet khong dung version/CRC |
| `loss` tang | Mat goi ESP-NOW, node qua xa, kenh WiFi nhieu nhieu |
| `heap` giam lien tuc | Nguy co ro ri bo nho hoac task tao that bai lap lai |
| Serial log `Busy: RS485 task already running` | Dang co tac vu ghi VFD, lenh moi bi bo qua/doi |
| Reset reason `BROWNOUT` | Nguon yeu, sut ap khi VFD/relay/cam bien hoat dong |
| Reset reason `TASK_WDT`/`PANIC` | Firmware treo, loi bo nho, task bi chan qua lau |

## 9. Xu ly su co

### 9.1 Khong build duoc firmware

| Nguyen nhan | Cach kiem tra | Huong xu ly |
| --- | --- | --- |
| Chua cai PlatformIO | `pio --version` khong chay | Cai PlatformIO IDE hoac dung duong dan `platformio.exe` trong venv |
| Thieu library | Log build bao missing header | Chay lai `pio run`, dam bao may co internet lan dau de tai `lib_deps` |
| Sai env | Build `master` nhung mong co web local node | Chon dung env theo bang muc 6.1 |
| Xung dot Arduino/ESP32 core | Loi API ESP-NOW callback | Du an dang pin `espressif32@6.6.0`; khong nang platform khi chua test |

### 9.2 Khong thay node tren master

1. Xac nhan master da boot va Serial co dong `Dashboard ready: http://192.168.4.1/`.
2. Xac nhan node build dung env `nodeX`, moi node co ID rieng.
3. Xac nhan `ESP_NOW_MASTER_MAC` trong `src/Config.h` dung voi STA MAC cua master.
4. Xac nhan `ESP_NOW_CHANNEL` bang `MASTER_AP_CHANNEL` la `6`.
5. Dua node gan master de loai tru nguyen nhan tam song.
6. Neu van loi, xem Serial node co dong `[ESPNOW] NODE init failed` hay khong.

### 9.3 Web local khong truy cap duoc

| Buoc | Cach lam |
| --- | --- |
| 1 | Mo Serial Monitor, tim dong `[WIFI] Connected, IP:` |
| 2 | Kiem tra laptop/dien thoai cung mang voi ESP32 |
| 3 | Mo `http://<ip>` thay vi `https://<ip>` |
| 4 | Kiem tra endpoint `/health`: tra `200` la OK, `503` la WiFi disconnected |
| 5 | Neu ESP32 dung WiFi cu trong NVS, cap nhat lai qua firmware/tool hoac clear NVS |

### 9.4 VFD khong chay hoac tan so khong thay doi

1. Do day A/B RS-485, thu dao A/B neu nghi dau day nguoc.
2. Xac nhan VFD slave ID = `UNWIND_VFD_ID` trong `src/Config.h` (mac dinh `1`).
3. Xac nhan baud/parity = `9600`, `SERIAL_8N1`.
4. Kiem tra chan `MAX485_DE_RE=16`, `RX2=18`, `TX2=17`.
5. Xem serial log `[VFD] xx.x Hz OK` hay `[WARN] Set frequency failed`.
6. Xac nhan VFD chap nhan register:
   - Control: `0x2000`
   - Frequency: `0x2001`, gia tri = Hz x 100

### 9.5 Tan so dao dong hoac may chay khong on dinh

| Trieu chung | Nguyen nhan kha nang | Countermeasure |
| --- | --- | --- |
| Distance nhay lon | Cam bien sieu am bi nhieu, goc do sai | Can chinh cam bien, tang `DIST RISE`, kiem tra nguon |
| VFD tang giam qua nhanh | `UDMIN/UDMAX` qua hep hoac `UFMIN/UFMAX` qua rong | Hieu chuan lai duong cong |
| Boost kich lien tuc | Cam bien cang day rung/chong | Kiem tra co khi, tang hold/decay hop ly |
| Boost len cap cao qua nhanh | `BOOSTE2/E3` qua thap | Tang thoi gian escalation |
| May bi cham phan ung | Loc spike qua manh hoac update cycle dai | Giam nguong loc/kiem tra `UPDATE_CYCLE` |

## 10. Quy trinh hieu chuan khi ban giao tai hien truong

1. De may khong tai, chay `PING`, kiem tra distance on dinh.
2. Do khoang cach tai trang thai day nhat, gan vao `UDMIN`.
3. Do khoang cach tai trang thai rong nhat, gan vao `UDMAX`.
4. Dat `UFMIN/UFMAX` theo gioi han co khi va gioi han an toan cua day chuyen.
5. Chay `RUN` voi toc do thap, quan sat VFD va day.
6. Tang dan `BOOSTL1/L2/L3` neu may khong bat kip luc cang.
7. Ghi lai bo tham so sau nghiem thu vao bien ban.

## 11. Quan ly ma nguon va cap nhat GitHub private

Remote hien tai:

```powershell
git remote -v
# origin  https://github.com/Kakajshd/vfd-motor-controller.git
```

Quy trinh cap nhat khuyen nghi:

```powershell
git status -sb
git add docs/PROJECT_HANDOVER_VI.md platformio.ini
git commit -m "Add project handover documentation"
git push origin main
```

Neu can day toan bo thay doi firmware hien co, chi thuc hien sau khi da build pass:

```powershell
git add -A
git commit -m "Update ESP-NOW VFD controller firmware"
git push origin main
```

Khong nen `git push --force` len repo private neu chua co backup hoac chua thong bao cac ben dang cung lam viec.

## 12. Rui ro va gioi han

| Rui ro | Tac dong | Khuyen nghi |
| --- | --- | --- |
| ESP-NOW khong phai kenh an toan chuc nang | Khong duoc dung lam emergency stop | Mach E-stop va interlock phai tach rieng bang phan cung |
| Mat WiFi/ESP-NOW | Mat dashboard hoac lenh giam sat | Node van phai giu logic dieu khien local |
| Sai master MAC/node ID | Node khong len dashboard hoac trung node | Quan ly danh sach ID/MAC khi lap dat |
| Sai register VFD theo model bien tan | VFD khong nhan lenh hoac nhan sai chuc nang | Doi chieu manual VFD truoc khi chay tai |
| Nhieu cam bien sieu am | Tan so dao dong, boost sai | Che chan, loc nguon, dat nguong spike theo thuc te |
| Luu NVS tham so sai | May khoi dong lai voi tham so khong mong muon | Co bien ban tham so chuan va quy trinh reset/cau hinh lai |

## 13. Checklist nghiem thu ban giao

| Hang muc | Ket qua mong doi | Trang thai |
| --- | --- | --- |
| Build `ESP32S3` | Build thanh cong | Chua dien |
| Build `master` | Build thanh cong | Chua dien |
| Build `node1` | Build thanh cong | Chua dien |
| Master AP | Thay WiFi `VFD_MASTER` va vao duoc `192.168.4.1` | Chua dien |
| Node online | Node xuat hien tren dashboard | Chua dien |
| Distance | Gia tri on dinh, phu hop thuc te | Chua dien |
| Modbus | VFD nhan lenh RUN/STOP/tan so | Chua dien |
| Boost | Kich cam bien cang day thi boost dung cap | Chua dien |
| Luu tham so | Reset ESP32 xong tham so van con | Chua dien |
| GitHub | Code va tai lieu da push len repo private | Chua dien |

## 14. Tai lieu/lien ket lien quan

| Tai lieu | Muc dich |
| --- | --- |
| `README.md` | Mo ta tong quan cu, hien co dau hieu loi encoding |
| `docs/espnow_master_node.md` | Ghi chu ky thuat rieng cho kien truc ESP-NOW master/node |
| `platformio.ini` | Cau hinh moi truong build |
| `src/Config.h` | Chan GPIO, WiFi, ESP-NOW, Modbus |
| `src/Settings.h` | Cau truc tham so va luu NVS |
| `src/main.cpp` | Logic dieu khien chinh |
| `src/master/MasterMain.cpp` | Firmware master dashboard |
| `src/node/NodeEspNow.cpp` | Giao tiep ESP-NOW cua node |
| `app.py` | PC configuration tool |

