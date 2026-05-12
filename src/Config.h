#ifndef CONFIG_H
#define CONFIG_H

#include <Arduino.h>

#define OLED_EN
#define I2C_ADDRESS 0x3C // Địa chỉ mặc định của màn hình SH1106
#define I2C_SDA 8
#define I2C_SCL 9
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_RESET -1   // Không dùng chân Reset

// --- WiFi Configuration ---
#define WIFI_SSID "hung"
#define WIFI_PASS "123456789"
#define WIFI_MODE_AP false  // false: kết nối vào WiFi của điện thoại
                                                                                                                    
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
#define PULL_VFD_ID   2
#define VFD_PROTOCOL SERIAL_8N1
#define VFD_BAUD 9600
#define UPDATE_CYCLE 200

#endif