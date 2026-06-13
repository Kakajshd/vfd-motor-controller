# ESP-NOW Master/Node Deployment

## Situation

This project now supports three PlatformIO environments:

| Environment | Role | Notes |
| --- | --- | --- |
| `ESP32S3` | Legacy all-in-one | Existing local webserver firmware |
| `node` | ESP-NOW machine node | Keeps sensor/VFD control, removes local web dashboard |
| `master` | Central dashboard | Creates AP and receives ESP-NOW telemetry |

## Architecture

```text
ESP32 NODE 1 --\
ESP32 NODE 2 ---- ESP-NOW channel 6 ---- ESP32 MASTER ---- WiFi AP ---- Browser
ESP32 NODE N --/
```

Nodes remain autonomous. The master is supervisory only: dashboard, status aggregation, and high-level commands.

## Configuration

Edit `src/Config.h` before flashing nodes:

```cpp
#define ESP_NOW_NODE_ID 1
#define ESP_NOW_CHANNEL 6
#define ESP_NOW_MASTER_MAC {0x24, 0x6F, 0x28, 0xAA, 0xBB, 0xCC}
```

Each node must have a unique `ESP_NOW_NODE_ID`.

The master AP uses:

```text
SSID: VFD_MASTER
PASS: 12345678
URL : http://192.168.4.1/
```

## Build

```powershell
pio run -e master
pio run -e node
```

If `pio` is not in `PATH`, use:

```powershell
& "$env:USERPROFILE\.platformio\penv\Scripts\pio.exe" run -e master
& "$env:USERPROFILE\.platformio\penv\Scripts\pio.exe" run -e node
```

## Packet Flow

```text
NODE boot -> HELLO -> MASTER registry
NODE runtime -> STATUS every ~300 ms
Browser -> WebSocket command -> MASTER -> ESP-NOW command -> NODE -> ACK
MASTER marks node offline if no packet for 2500 ms
```

## Per-Node Settings From Dashboard

The master dashboard has a `SET` button on each node row.

Flow:

```text
Dashboard SET panel
  -> WebSocket small setting commands
  -> MASTER command queue
  -> ESP-NOW MasterCommandPacket
  -> NODE validates setting
  -> NODE saves to NVS
  -> NODE sends updated STATUS
```

Supported settings:

| UI field | Node setting |
| --- | --- |
| `UEN` | Enable/disable automatic VFD frequency updates |
| `UDMIN`, `UDMAX` | Distance range |
| `UFMIN`, `UFMAX` | Frequency range |
| `BOOST L1/L2/L3` | Boost percentages |
| `HOLD L1/L2/L3` | Boost hold times |
| `ESC L2/L3` | Escalation times |
| `DECAY` | Boost decay time |
| `DIST RISE/FALL` | Ultrasonic spike filter thresholds |

The dashboard values come from the latest telemetry packet. After applying, wait for the next telemetry refresh and confirm the displayed values changed.

## Validation Roadmap

1. Flash `master`, connect phone/laptop to `VFD_MASTER`, open `http://192.168.4.1/`.
2. Flash one `node` with correct `ESP_NOW_MASTER_MAC`.
3. Confirm the node appears online and distance/frequency update.
4. Test `RUN`, `STOP`, `AUTO`, `MANUAL`.
5. Power off node and confirm offline alarm after about 2.5 seconds.
6. Add nodes one by one with unique IDs.

## Industrial Notes

- Do not use ESP-NOW for emergency stop or safety interlock.
- Keep VFD control local on each node.
- Use fixed WiFi channel for both master AP and ESP-NOW.
- Use command ACK/retry for critical commands; telemetry can use latest-state semantics.
- For 10-20 nodes, reduce status rate if packet loss increases.
