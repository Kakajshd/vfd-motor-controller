# Changelog

All notable changes to this project will be documented here.  
Format based on [Keep a Changelog](https://keepachangelog.com/en/1.0.0/).

---

## [Unreleased]

### Added
- WiFi/IP info bar in PC config tool (dieuchinh.exe)
- `resource_path()` helper for PyInstaller compatibility
- `build.py` — one-command build script for exe packaging

---

## [1.2.0] - 2026-05-12

### Added
- Multi-level boost system (3 levels with escalation and decay)
- `boost_escalate_2`, `boost_escalate_3`, `boost_decay_time` parameters
- Serial commands: `BOOSTL1/L2/L3`, `BOOSTH1/H2/H3`, `BOOSTE2/E3`, `BOOSTDECAY`
- Boost state visible on OLED and PC app

### Changed
- Boost logic refactored to state-machine pattern (non-blocking)
- `TXN=` command updated to include boost_level1_pct and boost_level1_hold

---

## [1.1.0] - 2026-04

### Added
- FreeRTOS background task for non-blocking Modbus communication
- Telemetry: modbusOk, modbusFail, netTaskCreateFail, lockTimeouts, freeHeap
- Web dashboard with Server-Sent Events (SSE) realtime updates
- OLED display support (SH1106 128×64, I2C)
- Sensor debounce (40ms) for dancer sensor B

### Changed
- VFD frequency command only sent when delta >= 0.2 Hz (reduce bus noise)
- WiFi reconnect with exponential backoff (1s → 30s max)

---

## [1.0.0] - 2026-03

### Added
- Initial release
- Basic distance-to-frequency linear mapping
- Modbus RTU communication with VFD via RS-485
- Serial command interface (INFO, RUN, PING, STOP, SAVE, RESET)
- NVS persistent settings storage
- WiFi STA mode with stored credentials
- Python/tkinter PC config tool
