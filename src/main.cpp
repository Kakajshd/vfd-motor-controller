#include "Config.h"
#include "Settings.h"
#include "VFDManager.h"
#include "WiFiWebManager.h"
#include <ESPAsyncWebServer.h>
#include <AsyncTCP.h>
#include <new>
#include <math.h>
#include <esp_system.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/semphr.h>

Preferences prefs;
SystemSettings settings;
VFDManager unwindVFD;
AsyncWebServer server(80);

static const unsigned long LOOP_INTERVAL_MS = 20;
static const unsigned long DISPLAY_UPDATE_CYCLE_MS = 100;
static const TickType_t STATE_MUTEX_TIMEOUT_TICKS = pdMS_TO_TICKS(50);
static const uint32_t NET_TASK_STACK_SIZE = 4096;
static const float NET_TASK_MIN_DELTA_HZ = 0.2f;

#ifdef OLED_EN
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SH110X.h>
Adafruit_SH1106G display = Adafruit_SH1106G(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);
#endif

//----------------------------------------
#include <NewPing.h>
NewPing sonar[SONAR_NUM] = {                   // Sensor object array.
  NewPing(TRIG1_PIN, ECHO1_PIN, MAX_DISTANCE)  // Each sensor's trigger pin, echo pin, and max distance to ping.
#if defined(TRIG2_PIN) && defined(ECHO2_PIN)
  ,
  NewPing(TRIG2_PIN, ECHO2_PIN, MAX_DISTANCE)
#endif
};

float getDistance() {
  if (SONAR_NUM == 0) {
    return 0.0;
  }

  float total_distance = 0.0;
  uint8_t validCount = 0;
  for (uint8_t i = 0; i < SONAR_NUM; i++) {  // Loop through each sensor and display results.
    float distance = sonar[i].ping_cm();
    if (distance > 0) {
      total_distance += distance;
      validCount++;
    }
    // Serial.printf("%d = %dcm\n", i, distance);
  }
  if (validCount == 0) {
    return 0.0;
  }

  return total_distance / validCount;
}
//----------------------------------------
enum SystemMode { MODE_RUN,
                  MODE_PING,
                  MODE_MANUAL };
SystemMode currentMode = MODE_RUN;

float manualOffset = 0;
float boot_factor = 1.0;
unsigned long lastTime = 0;
volatile bool isNetTaskRunning = false;
portMUX_TYPE netTaskMux = portMUX_INITIALIZER_UNLOCKED;
float last_uFreq = 0.0;
float lastSentFreq = -1.0f;
float last_dist = 0.0;
bool is_boosting = false;
float boost_remain = 0.0;
uint8_t boost_level = 0;
unsigned long boost_hold_until = 0;
unsigned long boost_active_since = 0;
unsigned long boost_stable_since = 0;

bool rawSensorB = true;
bool stableSensorB = true;
unsigned long sensorDebounceMark = 0;
bool lastSensorB = true; // false if sensor active, true if idle (normally open)

static const unsigned long SENSOR_DEBOUNCE_MS = 40;
unsigned long lastLoopTick = 0;
unsigned long lastDisplayTick = 0;
SemaphoreHandle_t stateMutex = nullptr;
RuntimeSharedData runtimeShared;
TelemetrySharedData telemetryShared = {0, 0, 0, 0, 0, 0, 0, 0};
//---------------------------------------------
float getBoostPctForLevel(const SystemSettings &cfg, uint8_t level) {
  if (level == 1) return cfg.boost_level1_pct;
  if (level == 2) return cfg.boost_level2_pct;
  if (level >= 3) return cfg.boost_level3_pct;
  return 0.0f;
}

float getBoostHoldForLevel(const SystemSettings &cfg, uint8_t level) {
  if (level == 1) return cfg.boost_level1_hold;
  if (level == 2) return cfg.boost_level2_hold;
  if (level >= 3) return cfg.boost_level3_hold;
  return 0.0f;
}

float getFreq(float dist, const SystemSettings &cfg) {
  if (cfg.udmax <= cfg.udmin) {
    return cfg.ufmin;
  }

  float freq = cfg.ufmin + (constrain(dist, cfg.udmin, cfg.udmax) - cfg.udmin) * (cfg.ufmax - cfg.ufmin) / (cfg.udmax - cfg.udmin);
  return freq;
}
//----------------------------------------------
struct NetJob {
  float freq;
};

bool tryReserveNetTaskSlot() {
  bool reserved = false;
  portENTER_CRITICAL(&netTaskMux);
  if (!isNetTaskRunning) {
    isNetTaskRunning = true;
    reserved = true;
  }
  portEXIT_CRITICAL(&netTaskMux);
  return reserved;
}

void releaseNetTaskSlot() {
  portENTER_CRITICAL(&netTaskMux);
  isNetTaskRunning = false;
  portEXIT_CRITICAL(&netTaskMux);
}

const char *getResetReasonText(esp_reset_reason_t reason) {
  switch (reason) {
    case ESP_RST_POWERON: return "POWERON";
    case ESP_RST_EXT: return "EXTERNAL";
    case ESP_RST_SW: return "SOFTWARE";
    case ESP_RST_PANIC: return "PANIC";
    case ESP_RST_INT_WDT: return "INT_WDT";
    case ESP_RST_TASK_WDT: return "TASK_WDT";
    case ESP_RST_WDT: return "WDT";
    case ESP_RST_DEEPSLEEP: return "DEEPSLEEP";
    case ESP_RST_BROWNOUT: return "BROWNOUT";
    case ESP_RST_SDIO: return "SDIO";
    default: return "UNKNOWN";
  }
}

void backgroundNetTask(void *pvParameters) {
  NetJob *job = static_cast<NetJob *>(pvParameters);
  if (job == nullptr) {
    releaseNetTaskSlot();
    vTaskDelete(NULL);
    return;
  }

  bool result = unwindVFD.setFrequency(job->freq);
  if (!result) {
    Serial.printf("[WARN] Set frequency %0.1f failed\n", job->freq);
  }

  if (stateMutex != nullptr && xSemaphoreTake(stateMutex, STATE_MUTEX_TIMEOUT_TICKS) == pdTRUE) {
    if (result) {
      telemetryShared.modbusOk++;
      lastSentFreq = job->freq;
    } else {
      telemetryShared.modbusFail++;
    }
    runtimeShared.vfdFreq = job->freq;
    runtimeShared.vfdOk = result;
    xSemaphoreGive(stateMutex);
  } else {
    if (result) {
      telemetryShared.modbusOk++;
      lastSentFreq = job->freq;
    } else {
      telemetryShared.modbusFail++;
    }
    runtimeShared.vfdFreq = job->freq;
    runtimeShared.vfdOk = result;
    telemetryShared.lockTimeouts++;
  }

  delete job;
  releaseNetTaskSlot();
  vTaskDelete(NULL);
}

// --- XỬ LÝ LỆNH SERIAL ---
void processCommand(String input) {
  input.trim();
  String cmd = input;
  cmd.toUpperCase();
  cmd.replace(" ", "");

  SystemSettings settingsSnapshot = settings;
  if (stateMutex != nullptr && xSemaphoreTake(stateMutex, STATE_MUTEX_TIMEOUT_TICKS) == pdTRUE) {
    settingsSnapshot = settings;
    xSemaphoreGive(stateMutex);
  } else {
    telemetryShared.lockTimeouts++;
  }

  if (cmd == "RESET") {
    ESP.restart();
  } else if (cmd == "INFO") {
    Serial.println("---[ CURRENT SETTINGS ]---");
    Serial.printf("UEN (User En)     : %s\n", settingsSnapshot.uen ? "ON" : "OFF");
    Serial.printf("UD (Min/Max)      : %.2f / %.2f\n", settingsSnapshot.udmin, settingsSnapshot.udmax);
    Serial.printf("UF (Min/Max)      : %.2f / %.2f\n", settingsSnapshot.ufmin, settingsSnapshot.ufmax);
    Serial.printf("Boost L1/L2/L3 %%   : %.2f / %.2f / %.2f\n",
                  settingsSnapshot.boost_level1_pct,
                  settingsSnapshot.boost_level2_pct,
                  settingsSnapshot.boost_level3_pct);
    Serial.printf("Boost Hold L1/L2/L3: %.2f / %.2f / %.2f s\n",
                  settingsSnapshot.boost_level1_hold,
                  settingsSnapshot.boost_level2_hold,
                  settingsSnapshot.boost_level3_hold);
    Serial.printf("Boost Esc2/Esc3   : %.2f / %.2f s\n",
                  settingsSnapshot.boost_escalate_2,
                  settingsSnapshot.boost_escalate_3);
    Serial.printf("Boost Decay       : %.2f s\n", settingsSnapshot.boost_decay_time);
    Serial.println("-------------------------");
  } else if (cmd == "RUN") {
    currentMode = MODE_RUN;
    manualOffset = 0;
    unwindVFD.sendControl(0x0012);
    Serial.println("-> Chế độ TỰ ĐỘNG");
  } else if (cmd == "PING") {
    currentMode = MODE_PING;
    Serial.println("-> Chế độ THỬ CẢM BIẾN");
  } else if (cmd == "STOP") {
    currentMode = MODE_MANUAL;
    unwindVFD.sendControl(0x0001);
    //pullVFD.sendControl(0x0001);
    Serial.println("-> DỪNG KHẨN");
  } else if (cmd == "SAVE") {
    if (stateMutex != nullptr && xSemaphoreTake(stateMutex, STATE_MUTEX_TIMEOUT_TICKS) == pdTRUE) {
      saveSettings(settings);
      xSemaphoreGive(stateMutex);
    } else {
      saveSettings(settings);
    }
  }

  // Tăng giảm tốc nhanh
  else if (cmd == "+")
    manualOffset += 1.0;
  else if (cmd == "++") manualOffset += 5.0;
  else if (cmd == "+++") manualOffset += 10.0;
  else if (cmd == "-") manualOffset -= 1.0;
  else if (cmd == "--") manualOffset -= 5.0;
  else if (cmd == "---") manualOffset -= 10.0;

  // Cài đặt tham số ví dụ: uen=0 hoặc DIST_MIN=15
  else if (cmd.indexOf('=') > 0) {
    String key = cmd.substring(0, cmd.indexOf('='));
    String payload = cmd.substring(cmd.indexOf('=') + 1);

    if (key == "TXN") {
      float udmin = 0.0f;
      float udmax = 0.0f;
      float ufmin = 0.0f;
      float ufmax = 0.0f;
      float bootfactor = 0.0f;
      float boostTime = 0.0f;

      int parsed = sscanf(payload.c_str(), "%f,%f,%f,%f,%f,%f",
                          &udmin,
                          &udmax,
                          &ufmin,
                          &ufmax,
                          &bootfactor,
                          &boostTime);
      if (parsed != 6) {
        Serial.println("[TXN] ERR FORMAT");
        return;
      }

      SystemSettings next = settingsSnapshot;
      next.udmin = udmin;
      next.udmax = udmax;
      next.ufmin = ufmin;
      next.ufmax = ufmax;
      next.bootfactor = bootfactor;
      next.boost_time = boostTime;
        next.boost_level1_pct = bootfactor;
        next.boost_level1_hold = boostTime;

      if (next.udmax <= next.udmin ||
          next.ufmax <= next.ufmin ||
          next.bootfactor < 0.0f ||
          next.bootfactor > 200.0f ||
          next.boost_time < 0.0f ||
          next.boost_time > 60.0f) {
        Serial.println("[TXN] ERR RANGE");
        return;
      }

      sanitizeSettings(next);

      if (stateMutex != nullptr && xSemaphoreTake(stateMutex, STATE_MUTEX_TIMEOUT_TICKS) == pdTRUE) {
        settings = next;
        xSemaphoreGive(stateMutex);
      } else {
        telemetryShared.lockTimeouts++;
        Serial.println("[TXN] ERR LOCK");
        return;
      }

      saveSettings(next);
      Serial.println("[TXN] OK");
      return;
    }

    float val = payload.toFloat();

    if (stateMutex != nullptr && xSemaphoreTake(stateMutex, STATE_MUTEX_TIMEOUT_TICKS) == pdTRUE) {
      if (key == "UEN") settings.uen = (val > 0);
      else if (key == "UDMIN") settings.udmin = val;
      else if (key == "UDMAX") settings.udmax = val;
      else if (key == "UFMIN") settings.ufmin = val;
      else if (key == "UFMAX") settings.ufmax = val;
      else if (key == "BOOTFACTOR") {
        settings.bootfactor = val;
        settings.boost_level1_pct = val;
      }
      else if (key == "BOOSTTIME") {
        settings.boost_time = val;
        settings.boost_level1_hold = val;
      }
      else if (key == "BOOSTL1") settings.boost_level1_pct = val;
      else if (key == "BOOSTL2") settings.boost_level2_pct = val;
      else if (key == "BOOSTL3") settings.boost_level3_pct = val;
      else if (key == "BOOSTH1") settings.boost_level1_hold = val;
      else if (key == "BOOSTH2") settings.boost_level2_hold = val;
      else if (key == "BOOSTH3") settings.boost_level3_hold = val;
      else if (key == "BOOSTE2") settings.boost_escalate_2 = val;
      else if (key == "BOOSTE3") settings.boost_escalate_3 = val;
      else if (key == "BOOSTDECAY") settings.boost_decay_time = val;
      sanitizeSettings(settings);
      xSemaphoreGive(stateMutex);
    } else {
      if (key == "UEN") settings.uen = (val > 0);
      else if (key == "UDMIN") settings.udmin = val;
      else if (key == "UDMAX") settings.udmax = val;
      else if (key == "UFMIN") settings.ufmin = val;
      else if (key == "UFMAX") settings.ufmax = val;
      else if (key == "BOOTFACTOR") {
        settings.bootfactor = val;
        settings.boost_level1_pct = val;
      }
      else if (key == "BOOSTTIME") {
        settings.boost_time = val;
        settings.boost_level1_hold = val;
      }
      else if (key == "BOOSTL1") settings.boost_level1_pct = val;
      else if (key == "BOOSTL2") settings.boost_level2_pct = val;
      else if (key == "BOOSTL3") settings.boost_level3_pct = val;
      else if (key == "BOOSTH1") settings.boost_level1_hold = val;
      else if (key == "BOOSTH2") settings.boost_level2_hold = val;
      else if (key == "BOOSTH3") settings.boost_level3_hold = val;
      else if (key == "BOOSTE2") settings.boost_escalate_2 = val;
      else if (key == "BOOSTE3") settings.boost_escalate_3 = val;
      else if (key == "BOOSTDECAY") settings.boost_decay_time = val;
      sanitizeSettings(settings);
    }
    Serial.printf("-> Đã cập nhật %s = %.1f (Gõ SAVE để lưu)\n", key.c_str(), val);
  }
}

void updateDisplay(float dist, float targetFreq, bool sB, float lastFreq, bool booting, float boostRemain) {
#ifdef OLED_EN
  display.clearDisplay();
  display.setTextColor(SH110X_WHITE);
  
  // --- TIÊU ĐỀ (HEADER) ---
  display.setTextSize(1);
  display.setCursor(0, 0);
  if (currentMode == MODE_RUN) display.print("MODE: AUTO");
  else if (currentMode == MODE_PING) display.print("MODE: TEST");
  else display.print("MODE: STOP");

  // Icon truyền tin (nhấp nháy khi đang gửi VFD)
  if (isNetTaskRunning) {
    display.setCursor(110, 0);
    display.print(">>>");
  }
  display.drawLine(0, 10, 128, 10, SH110X_WHITE);

  // --- KHOẢNG CÁCH (DISTANCE) ---
  display.setCursor(0, 15);
  display.print("Distance:");
  display.setTextSize(2);
  display.setCursor(0, 25);
  if(dist>0) display.print(dist, 1); else  display.print("-");
  display.setTextSize(1);
  display.print(" cm");

  display.setCursor(64, 15);
  display.print("Boosting:");
  display.setTextSize(2);
  display.setCursor(64, 25);
  display.print(booting? "ON" : "OFF");

  // --- TẦN SỐ TÍNH TOÁN (TARGET FREQ) ---
  display.setTextSize(1);
  display.setCursor(0, 45);
  display.print("Target F:");
  display.print(targetFreq, 1);
  display.print(" Hz");

  // --- TRẠNG THÁI SENSOR & VFD THỰC TẾ ---
  display.drawLine(0, 54, 128, 54, SH110X_WHITE);
  display.setCursor(0, 57);
  display.print("B:"); display.print(sB ? "ON" : "OFF");
  
  display.setCursor(65, 57);
  display.print("VFD:"); 
  display.print(lastFreq, 1); 
  display.print("Hz");

  if (booting) {
    display.setCursor(0, 57 + 10);
    display.print("BT:");
    display.print(boostRemain, 1);
    display.print("s");
  }

  display.display();
#endif
}
//---------------------------------------------

void handleOledRotateRequest(bool rotate180) {
#ifdef OLED_EN
  display.setRotation(rotate180 ? 2 : 0);
  display.clearDisplay();
  display.display();
  Serial.printf("[OLED] Rotation set to %d\n", rotate180 ? 2 : 0);
#else
  (void)rotate180;
#endif
}

void setup() {
  pinMode(PULL_SENS_B, INPUT_PULLUP);

  Serial.begin(115200);
  Serial.setTimeout(10);
  Serial.printf("[BOOT] Reset reason: %s (%d)\n", getResetReasonText(esp_reset_reason()), (int)esp_reset_reason());
  Serial2.begin(VFD_BAUD, VFD_PROTOCOL, RX2_PIN, TX2_PIN);

#ifdef OLED_EN
    // Khởi tạo I2C với chân 21, 22
  Wire.begin(I2C_SDA, I2C_SCL);
  // Khởi tạo màn hình
  if(!display.begin(I2C_ADDRESS, true)) {
    Serial.println(F("SH1106 allocation failed"));
    // Có thể thêm vòng lặp vô hạn ở đây nếu muốn dừng hệ thống khi lỗi màn hình
    
  }
  //display.setRotation(2);
  display.clearDisplay();
  display.display();
#endif

  stateMutex = xSemaphoreCreateMutex();
  if (stateMutex == nullptr) {
    Serial.println("[ERROR] Mutex init failed");
  }

  WiFiConfig wifiConfig;
  const char *bootSsid = WIFI_SSID;
  const char *bootPass = WIFI_PASS;
  if (loadWiFiConfig(wifiConfig) && wifiConfig.hasStored) {
    bootSsid = wifiConfig.ssid.c_str();
    bootPass = wifiConfig.pass.c_str();
    Serial.printf("[WIFI] Using stored SSID: %s\n", bootSsid);
  }

  WiFiWebManager::begin(bootSsid, bootPass);

  loadSettings(settings);

  unwindVFD.begin(UNWIND_VFD_ID, Serial2);  // Khởi tạo Modbus cho Serial2
  unwindVFD.sendControl(0x0012);

    //==================================== Webserver routes============================
    WiFiWebManager::registerRoutes(server, settings, stateMutex, runtimeShared, telemetryShared, handleOledRotateRequest);

  server.begin();

  Serial.println("\n=== HỆ THỐNG ĐIỀU KHIỂN ĐỘNG CƠ BỘ TỜI DÂY ===");
}

//============================ MAIN LOOP ============================//

void loop() {
  WiFiWebManager::serviceReconnect();

  unsigned long now = millis();
  if (now - lastLoopTick < LOOP_INTERVAL_MS) {
    return;
  }
  lastLoopTick = now;

  SystemSettings settingsSnapshot = settings;
  if (stateMutex != nullptr && xSemaphoreTake(stateMutex, STATE_MUTEX_TIMEOUT_TICKS) == pdTRUE) {
    settingsSnapshot = settings;
    xSemaphoreGive(stateMutex);
  } else {
    telemetryShared.lockTimeouts++;
  }

  if (Serial.available()) processCommand(Serial.readStringUntil('\n'));

  // Read sensor B with debounce to avoid boost oscillation from contact noise.
  bool sensorRawNow = !digitalRead(PULL_SENS_B);
  if (sensorRawNow != rawSensorB) {
    rawSensorB = sensorRawNow;
    sensorDebounceMark = now;
  }
  if (now - sensorDebounceMark >= SENSOR_DEBOUNCE_MS) {
    stableSensorB = rawSensorB;
  }
  bool sensorB = stableSensorB;

  bool sensorActive = !sensorB;
  bool wasSensorActive = !lastSensorB;

  if (sensorActive) {
    if (!wasSensorActive) {
      boost_active_since = now;
    }

    float activeSec = (now - boost_active_since) / 1000.0f;
    uint8_t targetLevel = 1;
    if (activeSec >= settingsSnapshot.boost_escalate_2) targetLevel = 2;
    if (activeSec >= settingsSnapshot.boost_escalate_3) targetLevel = 3;

    if (targetLevel > boost_level) {
      boost_level = targetLevel;
    }

    boost_hold_until = 0;
    boost_stable_since = 0;
  } else {
    if (wasSensorActive) {
      float holdSec = getBoostHoldForLevel(settingsSnapshot, boost_level);
      boost_hold_until = holdSec > 0.0f ? (now + (unsigned long)(holdSec * 1000.0f)) : 0;
      boost_stable_since = now;
    }

    if (boost_level > 0) {
      if (boost_hold_until != 0 && now >= boost_hold_until) {
        boost_hold_until = 0;
        if (boost_stable_since == 0) {
          boost_stable_since = now;
        }
      }

      if (boost_hold_until == 0) {
        if (settingsSnapshot.boost_decay_time <= 0.0f) {
          boost_level = 0;
        } else if (boost_stable_since != 0 &&
                   (now - boost_stable_since) >= (unsigned long)(settingsSnapshot.boost_decay_time * 1000.0f)) {
          boost_level--;
          boost_stable_since = now;
        }
      }
    }
  }

  is_boosting = boost_level > 0;
  if (is_boosting && boost_hold_until != 0) {
    float remaining = (boost_hold_until - now) / 1000.0f;
    boost_remain = remaining > 0.0f ? remaining : 0.0f;
  } else {
    boost_remain = 0.0f;
  }

  boot_factor = 1.0f + (getBoostPctForLevel(settingsSnapshot, boost_level) / 100.0f);
  lastSensorB = sensorB;

  float dist = getDistance();
  float uFreq = 0.0;

    if (dist > 0) {
      if (last_dist > 0) {
        dist = 0.7 * last_dist + 0.3 * dist; // Lọc nhiễu bằng cách lấy trung bình động.
      }
      uFreq = getFreq(dist, settingsSnapshot);
      uFreq *= boot_factor;
      last_dist = dist;
    }

  //Update the freq
  switch (currentMode) {
    case MODE_RUN:
      {
        //Update the freq
        if (now - lastTime > UPDATE_CYCLE) {
          lastTime = now;

          if (dist > 0) {
            last_uFreq = uFreq;

            bool shouldSendToVfd = fabsf(last_uFreq - lastSentFreq) >= NET_TASK_MIN_DELTA_HZ;
            if (settingsSnapshot.uen && shouldSendToVfd && tryReserveNetTaskSlot()) {
              NetJob *job = new (std::nothrow) NetJob();
              if (job != nullptr) {
                job->freq = last_uFreq;
                BaseType_t taskCreated = xTaskCreatePinnedToCore(backgroundNetTask, "NetTask", NET_TASK_STACK_SIZE, job, 1, NULL, 1);
                if (taskCreated != pdPASS) {
                  delete job;
                  releaseNetTaskSlot();
                  telemetryShared.netTaskCreateFail++;
                }
              } else {
                releaseNetTaskSlot();
                telemetryShared.netTaskCreateFail++;
              }
            }

            Serial.printf("[RUN] D:%.1f | F:%.1fHz | Boost:L%u | Hold:%.1fs\n",
                          dist,
                          last_uFreq,
                          (unsigned int)boost_level,
                          boost_remain);
          }
        }
        break;
      }
    case MODE_PING:
      Serial.printf("[PING] D: %.1f cm | B:%s\n", dist, sensorB ? "ON" : "OFF");
      break;
  }

  if (stateMutex != nullptr && xSemaphoreTake(stateMutex, STATE_MUTEX_TIMEOUT_TICKS) == pdTRUE) {
    runtimeShared.dist = last_dist;
    runtimeShared.freq = last_uFreq;
    runtimeShared.boost = is_boosting;
    runtimeShared.boostRemain = boost_remain;
    runtimeShared.boostLevel = boost_level;

    telemetryShared.loopTicks++;
    telemetryShared.uptimeSec = now / 1000UL;
    telemetryShared.freeHeap = ESP.getFreeHeap();
    telemetryShared.minFreeHeap = ESP.getMinFreeHeap();
    xSemaphoreGive(stateMutex);
  } else {
    telemetryShared.lockTimeouts++;
  }

  if (now - lastDisplayTick >= DISPLAY_UPDATE_CYCLE_MS) {
    lastDisplayTick = now;
    updateDisplay(dist, uFreq, sensorB, last_uFreq, is_boosting, boost_remain);

    const char *modeStr = (currentMode == MODE_RUN) ? "RUN" : (currentMode == MODE_PING) ? "TEST" : "STOP";
    RuntimeSharedData evtSnap;
    if (stateMutex != nullptr && xSemaphoreTake(stateMutex, pdMS_TO_TICKS(5)) == pdTRUE) {
      evtSnap = runtimeShared;
      xSemaphoreGive(stateMutex);
    } else {
      evtSnap = runtimeShared;
    }
    WiFiWebManager::pushEvent(evtSnap, modeStr);
  }
}
