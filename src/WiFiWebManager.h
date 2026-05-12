#ifndef WIFI_WEB_MANAGER_H
#define WIFI_WEB_MANAGER_H

#include <WiFi.h>
#include <ESPAsyncWebServer.h>
#include <freertos/semphr.h>
#include "Settings.h"

struct RuntimeSharedData {
  float dist = 0.0f;
  float freq = 0.0f;
  bool boost = false;
  float boostRemain = 0.0f;
  uint8_t boostLevel = 0;
  float vfdFreq = -1.0f;
  bool vfdOk = false;
};

struct TelemetrySharedData {
  uint32_t loopTicks;
  uint32_t modbusOk;
  uint32_t modbusFail;
  uint32_t netTaskCreateFail;
  uint32_t lockTimeouts;
  uint32_t uptimeSec;
  uint32_t freeHeap;
  uint32_t minFreeHeap;
};

using OledRotateHandler = void (*)(bool rotate180);

namespace WiFiWebManager {
void begin(const char *ssid, const char *pass);
void serviceReconnect();
bool isConnected();

void registerRoutes(
  AsyncWebServer &server,
  SystemSettings &settings,
  SemaphoreHandle_t stateMutex,
  RuntimeSharedData &runtimeData,
  TelemetrySharedData &telemetryData,
  OledRotateHandler oledRotateHandler = nullptr);

void pushEvent(const RuntimeSharedData &runtime, const char *mode);
}

#endif
