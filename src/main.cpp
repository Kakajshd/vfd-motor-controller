#ifndef ROLE_MASTER

#include "Config.h"
#include "Settings.h"
#include "VFDManager.h"
#include "WiFiWebManager.h"
#include "VfdProfiles.h"
#ifdef ROLE_NODE_ESPNOW
#include "node/NodeEspNow.h"
#include "common/Protocol.h"
#endif
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
static const unsigned long VFD_HEARTBEAT_MS = 2000; // Gửi định kỳ dù freq không đổi để tránh VFD timeout
static const TickType_t VFD_PROFILE_TASK_STACK_SIZE = 4096;
static const TickType_t VFD_PROFILE_TASK_DELAY_TICKS = pdMS_TO_TICKS(60);
static const unsigned long VFD_PROFILE_REQUEST_RETRY_MS = 1000;

#ifdef OLED_EN
#include <Wire.h>
#include <Adafruit_GFX.h>
#  if defined(OLED_DRIVER_SSD1306)
#    include <Adafruit_SSD1306.h>
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);
#    define OLED_WHITE WHITE
#  else
#    include <Adafruit_SH110X.h>
Adafruit_SH1106G display = Adafruit_SH1106G(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);
#    define OLED_WHITE SH110X_WHITE
#  endif
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

static volatile bool vfdProfileApplyInProgress = false;
static volatile VfdProfile currentVfdProfile = VFD_PROFILE_MANUAL;

static bool vfdProfileBtnRaw = false;
static bool vfdProfileBtnStable = false;
static bool vfdProfileSwitchStableReady = false;
static unsigned long vfdProfileBtnMarkMs = 0;
static VfdProfile vfdProfileLastRequestTarget = VFD_PROFILE_MANUAL;
static unsigned long vfdProfileLastRequestMs = 0;

static void startApplyVfdProfile(VfdProfile target);

static void handleVfdProfileRequest(VfdProfileRequest request) {
  if (request == VfdProfileRequest::Auto) {
    startApplyVfdProfile(VFD_PROFILE_AUTO);
    return;
  }
  if (request == VfdProfileRequest::Manual) {
    startApplyVfdProfile(VFD_PROFILE_MANUAL);
    return;
  }
  if (request == VfdProfileRequest::Toggle) {
    VfdProfile next = (currentVfdProfile == VFD_PROFILE_AUTO) ? VFD_PROFILE_MANUAL : VFD_PROFILE_AUTO;
    startApplyVfdProfile(next);
    return;
  }
}

float manualOffset = 0;
float boot_factor = 1.0;
unsigned long lastTime = 0;
volatile bool isNetTaskRunning = false;
portMUX_TYPE netTaskMux = portMUX_INITIALIZER_UNLOCKED;
float last_uFreq = 0.0;
float lastSentFreq = -1.0f;
float last_dist = 0.0;
uint8_t spike_reject_count = 0;  // Counter for consecutive spike rejections
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
unsigned long lastHeartbeatTick = 0;

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
  float t = (constrain(dist, cfg.udmin, cfg.udmax) - cfg.udmin) / (cfg.udmax - cfg.udmin);
  t = powf(t, FREQ_CURVE_GAMMA);
  return cfg.ufmin + t * (cfg.ufmax - cfg.ufmin);
}
//----------------------------------------------
struct NetJob {
  float freq;
};

struct VfdProfileJob {
  VfdProfile target;
  SystemMode restoreMode;
};

static const VfdParamWrite *getProfileParams(VfdProfile profile, size_t &outCount) {
  if (profile == VFD_PROFILE_AUTO) {
    outCount = VFD_PROFILE_PARAMS_AUTO_COUNT;
    return VFD_PROFILE_PARAMS_AUTO;
  }
  outCount = VFD_PROFILE_PARAMS_MANUAL_COUNT;
  return VFD_PROFILE_PARAMS_MANUAL;
}

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
  if (result) {
    Serial.printf("[VFD] %.1f Hz OK\n", job->freq);
  } else {
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

static void vfdProfileApplyTask(void *pvParameters) {
  VfdProfileJob *job = static_cast<VfdProfileJob *>(pvParameters);
  if (job == nullptr) {
    vfdProfileApplyInProgress = false;
    releaseNetTaskSlot();
    vTaskDelete(NULL);
    return;
  }

  const VfdProfile target = job->target;
  const SystemMode restoreMode = job->restoreMode;
  delete job;

  Serial.printf("[VFDSET] Apply profile: %s\n", target == VFD_PROFILE_AUTO ? "AUTO" : "MANUAL");

  unwindVFD.sendControl(0x0001);
  vTaskDelay(pdMS_TO_TICKS(120));

  size_t count = 0;
  const VfdParamWrite *params = getProfileParams(target, count);
  bool allOk = true;
  for (size_t i = 0; i < count; i++) {
    const VfdParamWrite &p = params[i];
    bool ok = unwindVFD.writeHoldingRegister(p.reg, p.value);
    Serial.printf("[VFDSET] %s(0x%04X)=%u %s\n",
                  p.code,
                  (unsigned int)p.reg,
                  (unsigned int)p.value,
                  ok ? "OK" : "FAIL");
    if (!ok) {
      allOk = false;
    }
    vTaskDelay(VFD_PROFILE_TASK_DELAY_TICKS);
  }

  if (restoreMode == MODE_RUN) {
    unwindVFD.sendControl(0x0012);
  } else {
    unwindVFD.sendControl(0x0001);
  }

  if (allOk) {
    currentVfdProfile = target;
    Serial.printf("[VFDSET] Profile %s applied\n", target == VFD_PROFILE_AUTO ? "AUTO" : "MANUAL");
  } else {
    Serial.printf("[VFDSET] Apply finished with failures (profile=%s)\n", target == VFD_PROFILE_AUTO ? "AUTO" : "MANUAL");
  }

  vfdProfileApplyInProgress = false;
  releaseNetTaskSlot();
  vTaskDelete(NULL);
}

static void startApplyVfdProfile(VfdProfile target) {
  if (vfdProfileApplyInProgress) {
    return;
  }
  if (!tryReserveNetTaskSlot()) {
    Serial.println("[VFDSET] Busy: RS485 task already running");
    return;
  }

  VfdProfileJob *job = new (std::nothrow) VfdProfileJob();
  if (job == nullptr) {
    releaseNetTaskSlot();
    return;
  }

  vfdProfileApplyInProgress = true;
  job->target = target;
  job->restoreMode = currentMode;

  BaseType_t taskCreated = xTaskCreatePinnedToCore(
    vfdProfileApplyTask,
    "VfdProf",
    (uint32_t)VFD_PROFILE_TASK_STACK_SIZE,
    job,
    1,
    NULL,
    1);

  if (taskCreated != pdPASS) {
    delete job;
    vfdProfileApplyInProgress = false;
    releaseNetTaskSlot();
    telemetryShared.netTaskCreateFail++;
  }
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
    Serial.printf("VFD PROFILE       : %s\n", currentVfdProfile == VFD_PROFILE_AUTO ? "AUTO" : "MANUAL");
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
    //manualOffset = 0;
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

  /*/ Tăng giảm tốc nhanh
  else if (cmd == "+")
    manualOffset += 1.0;
  else if (cmd == "++") manualOffset += 5.0;
  else if (cmd == "+++") manualOffset += 10.0;
  else if (cmd == "-") manualOffset -= 1.0;
  else if (cmd == "--") manualOffset -= 5.0;
  else if (cmd == "---") manualOffset -= 10.0;
*///
  // Cài đặt tham số ví dụ: uen=0 hoặc DIST_MIN=15
  else if (cmd.indexOf('=') > 0) {
    String key = cmd.substring(0, cmd.indexOf('='));
    String payload = cmd.substring(cmd.indexOf('=') + 1);

    if (key == "VFD") {
      if (payload == "AUTO" || payload == "1") {
        startApplyVfdProfile(VFD_PROFILE_AUTO);
        Serial.println("[VFD] Request AUTO");
        return;
      }
      if (payload == "MANUAL" || payload == "0") {
        startApplyVfdProfile(VFD_PROFILE_MANUAL);
        Serial.println("[VFD] Request MANUAL");
        return;
      }
      if (payload == "TOGGLE") {
        VfdProfile next = (currentVfdProfile == VFD_PROFILE_AUTO) ? VFD_PROFILE_MANUAL : VFD_PROFILE_AUTO;
        startApplyVfdProfile(next);
        Serial.println("[VFD] Request TOGGLE");
        return;
      }
      Serial.println("[VFD] ERR MODE");
      return;
    }

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

#ifdef ROLE_NODE_ESPNOW
uint8_t applyRemoteSetting(uint8_t command, float value) {
  SystemSettings next = settings;
  if (stateMutex != nullptr && xSemaphoreTake(stateMutex, STATE_MUTEX_TIMEOUT_TICKS) == pdTRUE) {
    next = settings;
    xSemaphoreGive(stateMutex);
  }

  switch (command) {
    case VfdNet::CMD_SET_UEN: next.uen = value > 0.5f; break;
    case VfdNet::CMD_SET_UDMIN: next.udmin = value; break;
    case VfdNet::CMD_SET_UDMAX: next.udmax = value; break;
    case VfdNet::CMD_SET_UFMIN: next.ufmin = value; break;
    case VfdNet::CMD_SET_UFMAX: next.ufmax = value; break;
    case VfdNet::CMD_SET_BOOST_L1: next.boost_level1_pct = value; break;
    case VfdNet::CMD_SET_BOOST_L2: next.boost_level2_pct = value; break;
    case VfdNet::CMD_SET_BOOST_L3: next.boost_level3_pct = value; break;
    case VfdNet::CMD_SET_HOLD_L1: next.boost_level1_hold = value; break;
    case VfdNet::CMD_SET_HOLD_L2: next.boost_level2_hold = value; break;
    case VfdNet::CMD_SET_HOLD_L3: next.boost_level3_hold = value; break;
    case VfdNet::CMD_SET_ESC_L2: next.boost_escalate_2 = value; break;
    case VfdNet::CMD_SET_ESC_L3: next.boost_escalate_3 = value; break;
    case VfdNet::CMD_SET_DECAY: next.boost_decay_time = value; break;
    case VfdNet::CMD_SET_DIST_RISE: next.dist_spike_rise_cm = value; break;
    case VfdNet::CMD_SET_DIST_FALL: next.dist_spike_fall_cm = value; break;
    default: return VfdNet::ACK_UNSUPPORTED;
  }

  next.bootfactor = next.boost_level1_pct;
  next.boost_time = next.boost_level1_hold;
  sanitizeSettings(next);

  if (next.udmax <= next.udmin || next.ufmax <= next.ufmin || next.dist_spike_fall_cm <= next.dist_spike_rise_cm) {
    return VfdNet::ACK_UNSUPPORTED;
  }

  if (stateMutex != nullptr && xSemaphoreTake(stateMutex, STATE_MUTEX_TIMEOUT_TICKS) == pdTRUE) {
    settings = next;
    xSemaphoreGive(stateMutex);
  } else {
    settings = next;
  }
  saveSettings(settings);
  Serial.printf("[ESPNOW] SET command=%u value=%.2f saved\n", command, value);
  return VfdNet::ACK_OK;
}

uint8_t getNetworkMode() {
  if (currentMode == MODE_PING) {
    return VfdNet::MACHINE_MODE_TEST;
  }
  if (currentMode == MODE_MANUAL) {
    return VfdNet::MACHINE_MODE_STOP;
  }
  return currentVfdProfile == VFD_PROFILE_AUTO ? VfdNet::MACHINE_MODE_AUTO : VfdNet::MACHINE_MODE_MANUAL;
}

uint8_t getNetworkRunState() {
  return currentMode == MODE_RUN ? VfdNet::RUN_STATE_RUN : VfdNet::RUN_STATE_STOP;
}

void handleMasterCommand(const VfdNet::MasterCommandPacket &command) {
  uint8_t result = VfdNet::ACK_OK;

  switch (command.command) {
    case VfdNet::CMD_RUN:
      currentMode = MODE_RUN;
      unwindVFD.sendControl(0x0012);
      Serial.println("[ESPNOW] CMD RUN");
      break;

    case VfdNet::CMD_STOP:
      currentMode = MODE_MANUAL;
      unwindVFD.sendControl(0x0001);
      Serial.println("[ESPNOW] CMD STOP");
      break;

    case VfdNet::CMD_SET_FREQ:
      if (command.value < settings.ufmin || command.value > settings.ufmax) {
        result = VfdNet::ACK_UNSUPPORTED;
      } else if (tryReserveNetTaskSlot()) {
        NetJob *job = new (std::nothrow) NetJob();
        if (job != nullptr) {
          job->freq = command.value;
          BaseType_t taskCreated = xTaskCreatePinnedToCore(backgroundNetTask, "NetTask", NET_TASK_STACK_SIZE, job, 1, NULL, 1);
          if (taskCreated != pdPASS) {
            delete job;
            releaseNetTaskSlot();
            telemetryShared.netTaskCreateFail++;
            result = VfdNet::ACK_BUSY;
          }
        } else {
          releaseNetTaskSlot();
          telemetryShared.netTaskCreateFail++;
          result = VfdNet::ACK_BUSY;
        }
      } else {
        result = VfdNet::ACK_BUSY;
      }
      Serial.printf("[ESPNOW] CMD FREQ %.1f result=%u\n", command.value, result);
      break;

    case VfdNet::CMD_APPLY_VFD_AUTO:
      startApplyVfdProfile(VFD_PROFILE_AUTO);
      Serial.println("[ESPNOW] CMD VFD AUTO");
      break;

    case VfdNet::CMD_APPLY_VFD_MANUAL:
      startApplyVfdProfile(VFD_PROFILE_MANUAL);
      Serial.println("[ESPNOW] CMD VFD MANUAL");
      break;

    case VfdNet::CMD_RESET_ALARM:
      telemetryShared.modbusFail = 0;
      Serial.println("[ESPNOW] CMD RESET ALARM");
      break;

    default:
      if (command.command >= VfdNet::CMD_SET_UEN && command.command <= VfdNet::CMD_SET_DIST_FALL) {
        result = applyRemoteSetting(command.command, command.value);
      } else {
        result = VfdNet::ACK_UNSUPPORTED;
      }
      break;
  }

  NodeEspNow::sendAck(command.header.seq, result);
}

void serviceMasterCommands() {
  VfdNet::MasterCommandPacket command;
  while (NodeEspNow::pollCommand(command)) {
    handleMasterCommand(command);
  }
}

void sendNodeStatusSnapshot() {
  RuntimeSharedData runtimeSnapshot;
  TelemetrySharedData telemetrySnapshot;
  SystemSettings settingsSnapshot;
  if (stateMutex != nullptr && xSemaphoreTake(stateMutex, pdMS_TO_TICKS(5)) == pdTRUE) {
    runtimeSnapshot = runtimeShared;
    telemetrySnapshot = telemetryShared;
    settingsSnapshot = settings;
    xSemaphoreGive(stateMutex);
  } else {
    runtimeSnapshot = runtimeShared;
    telemetrySnapshot = telemetryShared;
    settingsSnapshot = settings;
  }

  NodeEspNow::StatusInput status;
  status.distanceCm = runtimeSnapshot.dist;
  status.frequencyHz = runtimeSnapshot.freq;
  status.mode = getNetworkMode();
  status.runState = getNetworkRunState();
  status.boostLevel = runtimeSnapshot.boostLevel;
  status.vfdProfile = runtimeSnapshot.vfdProfile;
  status.alarmFlags = VfdNet::ALARM_NONE;
  if (runtimeSnapshot.dist <= 0.0f) {
    status.alarmFlags |= VfdNet::ALARM_SENSOR_LOST;
  }
  if (!runtimeSnapshot.vfdOk && telemetrySnapshot.modbusFail > 0) {
    status.alarmFlags |= VfdNet::ALARM_VFD_TIMEOUT;
  }
  status.vfdOnline = runtimeSnapshot.vfdOk;
  status.modbusOk = telemetrySnapshot.modbusOk;
  status.modbusFail = telemetrySnapshot.modbusFail;
  status.userEnabled = settingsSnapshot.uen;
  status.udmin = settingsSnapshot.udmin;
  status.udmax = settingsSnapshot.udmax;
  status.ufmin = settingsSnapshot.ufmin;
  status.ufmax = settingsSnapshot.ufmax;
  status.boostLevel1Pct = settingsSnapshot.boost_level1_pct;
  status.boostLevel2Pct = settingsSnapshot.boost_level2_pct;
  status.boostLevel3Pct = settingsSnapshot.boost_level3_pct;
  status.boostLevel1Hold = settingsSnapshot.boost_level1_hold;
  status.boostLevel2Hold = settingsSnapshot.boost_level2_hold;
  status.boostLevel3Hold = settingsSnapshot.boost_level3_hold;
  status.boostEscalate2 = settingsSnapshot.boost_escalate_2;
  status.boostEscalate3 = settingsSnapshot.boost_escalate_3;
  status.boostDecayTime = settingsSnapshot.boost_decay_time;
  status.distSpikeRiseCm = settingsSnapshot.dist_spike_rise_cm;
  status.distSpikeFallCm = settingsSnapshot.dist_spike_fall_cm;
  NodeEspNow::sendStatus(status);
}
#endif

void updateDisplay(float dist, float targetFreq, bool sB, float lastFreq, bool booting, float boostRemain) {
#ifdef OLED_EN
  display.clearDisplay();
  display.setTextColor(OLED_WHITE);
  
  // --- TIÊU ĐỀ (HEADER) ---
  display.setTextSize(1);
  display.setCursor(0, 0);
  if (currentMode == MODE_RUN) {
    display.print("MODE: ");
    display.print(currentVfdProfile == VFD_PROFILE_AUTO ? "AUTO" : "MANUAL");
  }
  else if (currentMode == MODE_PING) display.print("MODE: TEST");
  else display.print("MODE: STOP");

  // Icon truyền tin (nhấp nháy khi đang gửi VFD)
  if (isNetTaskRunning) {
    display.setCursor(110, 0);
    display.print(">>>");
  }
  display.drawLine(0, 10, 128, 10, OLED_WHITE);

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
  display.drawLine(0, 54, 128, 54, OLED_WHITE);
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
  pinMode(VFD_PROFILE_BTN_PIN, INPUT_PULLUP);

  Serial.begin(115200);
  Serial.setTimeout(10);
  Serial.printf("[BOOT] Reset reason: %s (%d)\n", getResetReasonText(esp_reset_reason()), (int)esp_reset_reason());
  Serial2.begin(VFD_BAUD, VFD_PROTOCOL, RX2_PIN, TX2_PIN);

#ifdef OLED_EN
    // Khởi tạo I2C với chân 21, 22
  Wire.begin(I2C_SDA, I2C_SCL);
  // Khởi tạo màn hình
#  if defined(OLED_DRIVER_SSD1306)
  if (!display.begin(SSD1306_SWITCHCAPVCC, I2C_ADDRESS)) {
    Serial.println(F("SSD1306 allocation failed"));
  }
#  else
  if (!display.begin(I2C_ADDRESS, true)) {
    Serial.println(F("SH1106 allocation failed"));
  }
#  endif
  display.setRotation(2); // Mặc định xoay 180°
  display.clearDisplay();
  display.display();
#endif

  stateMutex = xSemaphoreCreateMutex();
  if (stateMutex == nullptr) {
    Serial.println("[ERROR] Mutex init failed");
  }

#ifdef ROLE_NODE_ESPNOW
  const uint8_t masterMac[] = ESP_NOW_MASTER_MAC;
  if (NodeEspNow::begin(ESP_NOW_NODE_ID, masterMac, ESP_NOW_CHANNEL)) {
    Serial.printf("[ESPNOW] NODE %u started on channel %u\n", ESP_NOW_NODE_ID, ESP_NOW_CHANNEL);
  } else {
    Serial.println("[ESPNOW] NODE init failed; local VFD control continues");
  }
#else
  WiFiConfig wifiConfig;
  const char *bootSsid = WIFI_SSID;
  const char *bootPass = WIFI_PASS;
  if (loadWiFiConfig(wifiConfig) && wifiConfig.hasStored) {
    bootSsid = wifiConfig.ssid.c_str();
    bootPass = wifiConfig.pass.c_str();
    Serial.printf("[WIFI] Using stored SSID: %s\n", bootSsid);
  }

  WiFiWebManager::begin(bootSsid, bootPass);
#endif

  loadSettings(settings);

  unwindVFD.begin(UNWIND_VFD_ID, Serial2);  // Khởi tạo Modbus cho Serial2
  unwindVFD.sendControl(0x0012);

    //==================================== Webserver routes============================
#ifndef ROLE_NODE_ESPNOW
  WiFiWebManager::registerRoutes(server, settings, stateMutex, runtimeShared, telemetryShared, handleOledRotateRequest, handleVfdProfileRequest);

  server.begin();
#endif

  Serial.println("\n=== HỆ THỐNG ĐIỀU KHIỂN ĐỘNG CƠ BỘ TỜI DÂY ===");
}

//============================ MAIN LOOP ============================//

void loop() {
#ifdef ROLE_NODE_ESPNOW
  serviceMasterCommands();
#else
  WiFiWebManager::serviceReconnect();
#endif

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

  // VFD profile maintained switch (GPIO20) with debounce.
  // Active level maps to AUTO; inactive level maps to MANUAL.
  bool switchRawNow = digitalRead(VFD_PROFILE_BTN_PIN) == (VFD_PROFILE_BTN_ACTIVE_LOW ? LOW : HIGH);
  if (switchRawNow != vfdProfileBtnRaw) {
    vfdProfileBtnRaw = switchRawNow;
    vfdProfileBtnMarkMs = now;
    vfdProfileSwitchStableReady = false;
  }
  if (now - vfdProfileBtnMarkMs >= VFD_PROFILE_BTN_DEBOUNCE_MS) {
    vfdProfileBtnStable = vfdProfileBtnRaw;
    vfdProfileSwitchStableReady = true;
  }

  if (vfdProfileSwitchStableReady) {
    VfdProfile target = vfdProfileBtnStable ? VFD_PROFILE_AUTO : VFD_PROFILE_MANUAL;
    bool targetChanged = target != vfdProfileLastRequestTarget;
    bool retryDue = (now - vfdProfileLastRequestMs) >= VFD_PROFILE_REQUEST_RETRY_MS;
    if (target != currentVfdProfile && !vfdProfileApplyInProgress && (targetChanged || retryDue)) {
      vfdProfileLastRequestTarget = target;
      vfdProfileLastRequestMs = now;
      startApplyVfdProfile(target);
    }
  }

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
  float raw_dist = dist; // Giá trị thô trước khi lọc spike — dùng để hiển thị
  float uFreq = 0.0;

    if (dist > 0) {
      if (last_dist > 0) {
        float delta = dist - last_dist;
        if (delta > settingsSnapshot.dist_spike_rise_cm) {
          // Tang qua nhanh → spike cảm biến, từ chối
          spike_reject_count++;
          if (spike_reject_count >= SPIKE_REJECT_MAX_CYCLES) {
            // Spike kéo dài quá lâu → có thể là thay đổi thực, force accept
            dist = raw_dist;
            spike_reject_count = 0;
            Serial.printf("[SPIKE] Force accept after %u cycles: %.1f cm\n", SPIKE_REJECT_MAX_CYCLES, dist);
          } else {
            dist = last_dist;  // Still reject spike
          }
          } else if (delta < -settingsSnapshot.dist_spike_fall_cm) {
          // Giảm đột ngột → thay cuộn mới, chấp nhận ngay
          spike_reject_count = 0;  // Reset counter (positive change detected)
        } else {
          // Thay đổi hợp lệ → EMA thích nghi
          spike_reject_count = 0;  // Reset counter
          float norm = constrain(fabsf(delta) / settingsSnapshot.dist_spike_rise_cm, 0.0f, 1.0f);
          float alpha = 0.15f + 0.65f * norm; // 0.15 (mượt) .. 0.80 (nhanh)
          dist = (1.0f - alpha) * last_dist + alpha * dist;
        }
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

            bool freqChanged = fabsf(last_uFreq - lastSentFreq) >= NET_TASK_MIN_DELTA_HZ;
            bool heartbeatDue = (now - lastHeartbeatTick) >= VFD_HEARTBEAT_MS;
            bool shouldSendToVfd = freqChanged || heartbeatDue;
            if (!vfdProfileApplyInProgress && settingsSnapshot.uen && shouldSendToVfd && tryReserveNetTaskSlot()) {
              NetJob *job = new (std::nothrow) NetJob();
              if (job != nullptr) {
                job->freq = last_uFreq;
                lastHeartbeatTick = now;
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
                          raw_dist,
                          last_uFreq,
                          (unsigned int)boost_level,
                          boost_remain);
          }
        }
        break;
      }
    case MODE_PING:
      Serial.printf("[PING] D: %.1f cm | B:%s\n", raw_dist, sensorB ? "ON" : "OFF");
      break;
  }

  if (stateMutex != nullptr && xSemaphoreTake(stateMutex, STATE_MUTEX_TIMEOUT_TICKS) == pdTRUE) {
    runtimeShared.dist = raw_dist; // Hiển thị giá trị thô, thuật toán lọc chỉ ảnh hưởng tần số VFD
    runtimeShared.freq = last_uFreq;
    runtimeShared.boost = is_boosting;
    runtimeShared.boostRemain = boost_remain;
    runtimeShared.boostLevel = boost_level;
    runtimeShared.vfdProfile = (uint8_t)currentVfdProfile;

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
    updateDisplay(raw_dist, uFreq, sensorB, last_uFreq, is_boosting, boost_remain);

    const char *modeStr = (currentMode == MODE_RUN) ? "RUN" : (currentMode == MODE_PING) ? "TEST" : "STOP";
    RuntimeSharedData evtSnap;
    if (stateMutex != nullptr && xSemaphoreTake(stateMutex, pdMS_TO_TICKS(5)) == pdTRUE) {
      evtSnap = runtimeShared;
      xSemaphoreGive(stateMutex);
    } else {
      evtSnap = runtimeShared;
    }
#ifdef ROLE_NODE_ESPNOW
    static unsigned long lastEspNowHelloMs = 0;
    static unsigned long lastEspNowStatusMs = 0;
    if (now - lastEspNowHelloMs >= 5000UL) {
      lastEspNowHelloMs = now;
      NodeEspNow::sendHello();
    }
    if (now - lastEspNowStatusMs >= VfdNet::STATUS_INTERVAL_MS) {
      lastEspNowStatusMs = now;
      sendNodeStatusSnapshot();
    }
#else
    WiFiWebManager::pushEvent(evtSnap, modeStr);
#endif
  }
}

#endif
