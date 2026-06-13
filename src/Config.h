#ifndef CONFIG_H
#define CONFIG_H

#include <Arduino.h>

#define OLED_EN

// Chọn driver OLED: bật một trong hai dòng sau
#define OLED_DRIVER_SH1106 // Dùng cho SH1106 (màn 1.3" phổ biến)
// define OLED_DRIVER_SSD1306  // Dùng cho SSD1306 (màn 0.96" hoặc 1.3" clone)

#define I2C_ADDRESS 0x3C // Thử 0x3D nếu 0x3C không hiển thị
#define I2C_SDA 8
#define I2C_SCL 9
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_RESET -1 // Không dùng chân Reset

// --- WiFi Configuration ---
#define WIFI_SSID "hung"
#define WIFI_PASS "123456789"
#define WIFI_MODE_AP false // false: kết nối vào WiFi của điện thoại

// --- ESP-NOW distributed architecture ---
// Use a unique NODE_ID for each machine when building env:node.
#ifndef ESP_NOW_NODE_ID
#define ESP_NOW_NODE_ID 1
#endif

#define ESP_NOW_CHANNEL 6

// Replace this with the MASTER ESP32 STA MAC address.
// MASTER prints AP IP on boot; read STA MAC with WiFi.macAddress() if needed.
#define ESP_NOW_MASTER_MAC {0x20, 0x6E, 0xF1, 0xA1, 0xD2, 0x80}

// --- Cảm biến bể chứa dây (Dancer Sensors) ---
// #define PULL_SENS_A 14 // Bỏ sensor A
#define PULL_SENS_B 10 // Trung bình (Cần bù tốc)

#define SONAR_NUM 1      // Number of sensors.
#define MAX_DISTANCE 100 // Maximum distance (in cm) to ping.
#define TRIG1_PIN 4
#define ECHO1_PIN 5

#define MAX485_DE_RE 16
#define RX2_PIN 18
#define TX2_PIN 17

// // --- Kết nối phần cứng ---
// #define TRIG1_PIN 5
// #define ECHO1_PIN 18
// #define TRIG2_PIN 19
// #define ECHO2_PIN 21

// #define MAX485_DE_RE 4
// #define RX2_PIN 16
// #define TX2_PIN 17

// --- Modbus IDs ---
#define UNWIND_VFD_ID 1
#define PULL_VFD_ID 2
#define VFD_PROTOCOL SERIAL_8N1
#define VFD_BAUD 9600
#define UPDATE_CYCLE 200
#define DIST_SPIKE_RISE_CM 1.5f    // Max tang per loop truoc khi reject spike (cm, loop=20ms)
#define DIST_SPIKE_FALL_CM 3.0f    // Min giam de nhan dang thay cuon moi, chap nhan ngay (cm)
#define SPIKE_REJECT_MAX_CYCLES 10 // Neu reject lien tuc qua nhieu chu ky, force accept (8 chu ky = ~160ms)
#define FREQ_CURVE_GAMMA 1.0f      // Freq mapping curve: <1.0 phan ung nhanh o dist nho, >1.0 phan ung nhanh o dist lon

// --- VFD Parameter Profile Switch ---
// Wiring: use INPUT_PULLUP, switch shorts pin to GND in AUTO position.
#define VFD_PROFILE_BTN_PIN 20
#define VFD_PROFILE_BTN_ACTIVE_LOW 1
#define VFD_PROFILE_BTN_DEBOUNCE_MS 50

#endif
