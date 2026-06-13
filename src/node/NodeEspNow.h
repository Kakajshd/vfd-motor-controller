#ifndef NODE_ESPNOW_H
#define NODE_ESPNOW_H

#include <Arduino.h>
#include "../common/Protocol.h"

namespace NodeEspNow {

struct StatusInput {
  float distanceCm = 0.0f;
  float frequencyHz = 0.0f;
  uint8_t mode = VfdNet::MACHINE_MODE_STOP;
  uint8_t runState = VfdNet::RUN_STATE_STOP;
  uint8_t boostLevel = 0;
  uint8_t vfdProfile = 0;
  uint16_t alarmFlags = VfdNet::ALARM_NONE;
  bool vfdOnline = false;
  uint32_t modbusOk = 0;
  uint32_t modbusFail = 0;
  bool userEnabled = true;
  float udmin = 0.0f;
  float udmax = 0.0f;
  float ufmin = 0.0f;
  float ufmax = 0.0f;
  float boostLevel1Pct = 0.0f;
  float boostLevel2Pct = 0.0f;
  float boostLevel3Pct = 0.0f;
  float boostLevel1Hold = 0.0f;
  float boostLevel2Hold = 0.0f;
  float boostLevel3Hold = 0.0f;
  float boostEscalate2 = 0.0f;
  float boostEscalate3 = 0.0f;
  float boostDecayTime = 0.0f;
  float distSpikeRiseCm = 0.0f;
  float distSpikeFallCm = 0.0f;
};

bool begin(uint8_t nodeId, const uint8_t masterMac[6], uint8_t channel);
void sendHello();
void sendStatus(const StatusInput &status);
bool pollCommand(VfdNet::MasterCommandPacket &command);
void sendAck(uint16_t ackSeq, uint8_t result);
uint32_t txOkCount();
uint32_t txFailCount();
uint32_t rxCount();

}

#endif
