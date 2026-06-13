#ifndef VFD_ESPNOW_PROTOCOL_H
#define VFD_ESPNOW_PROTOCOL_H

#include <Arduino.h>

namespace VfdNet {

static constexpr uint8_t PROTOCOL_VERSION = 1;
static constexpr uint8_t BROADCAST_MAC[6] = {0xff, 0xff, 0xff, 0xff, 0xff, 0xff};
static constexpr uint8_t MAX_NODES = 20;
static constexpr uint32_t NODE_OFFLINE_TIMEOUT_MS = 2500;
static constexpr uint32_t STATUS_INTERVAL_MS = 300;
static constexpr uint32_t MASTER_AP_CHANNEL = 6;

enum PacketType : uint8_t {
  PKT_HELLO = 1,
  PKT_STATUS = 2,
  PKT_COMMAND = 3,
  PKT_ACK = 4
};

enum MachineMode : uint8_t {
  MACHINE_MODE_AUTO = 0,
  MACHINE_MODE_MANUAL = 1,
  MACHINE_MODE_TEST = 2,
  MACHINE_MODE_STOP = 3
};

enum RunState : uint8_t {
  RUN_STATE_STOP = 0,
  RUN_STATE_RUN = 1
};

enum CommandType : uint8_t {
  CMD_SET_MODE = 1,
  CMD_RUN = 2,
  CMD_STOP = 3,
  CMD_SET_FREQ = 4,
  CMD_RESET_ALARM = 5,
  CMD_APPLY_VFD_AUTO = 6,
  CMD_APPLY_VFD_MANUAL = 7,
  CMD_SET_UEN = 20,
  CMD_SET_UDMIN = 21,
  CMD_SET_UDMAX = 22,
  CMD_SET_UFMIN = 23,
  CMD_SET_UFMAX = 24,
  CMD_SET_BOOST_L1 = 25,
  CMD_SET_BOOST_L2 = 26,
  CMD_SET_BOOST_L3 = 27,
  CMD_SET_HOLD_L1 = 28,
  CMD_SET_HOLD_L2 = 29,
  CMD_SET_HOLD_L3 = 30,
  CMD_SET_ESC_L2 = 31,
  CMD_SET_ESC_L3 = 32,
  CMD_SET_DECAY = 33,
  CMD_SET_DIST_RISE = 34,
  CMD_SET_DIST_FALL = 35
};

enum AckResult : uint8_t {
  ACK_OK = 0,
  ACK_BAD_CRC = 1,
  ACK_BAD_TARGET = 2,
  ACK_UNSUPPORTED = 3,
  ACK_BUSY = 4
};

enum AlarmFlags : uint16_t {
  ALARM_NONE = 0x0000,
  ALARM_SENSOR_LOST = 0x0001,
  ALARM_VFD_TIMEOUT = 0x0002,
  ALARM_NODE_OFFLINE = 0x0004,
  ALARM_FREQ_LIMIT = 0x0008,
  ALARM_COMMAND_TIMEOUT = 0x0010
};

struct __attribute__((packed)) PacketHeader {
  uint8_t version;
  uint8_t type;
  uint8_t nodeId;
  uint16_t seq;
  uint32_t uptimeMs;
};

struct __attribute__((packed)) HelloPacket {
  PacketHeader header;
  uint8_t mac[6];
  uint16_t crc;
};

struct __attribute__((packed)) NodeStatusPacket {
  PacketHeader header;
  float distanceCm;
  float frequencyHz;
  uint8_t mode;
  uint8_t runState;
  uint8_t boostLevel;
  uint8_t vfdProfile;
  uint16_t alarmFlags;
  uint8_t vfdOnline;
  uint32_t modbusOk;
  uint32_t modbusFail;
  uint32_t freeHeap;
  uint8_t userEnabled;
  float udmin;
  float udmax;
  float ufmin;
  float ufmax;
  float boostLevel1Pct;
  float boostLevel2Pct;
  float boostLevel3Pct;
  float boostLevel1Hold;
  float boostLevel2Hold;
  float boostLevel3Hold;
  float boostEscalate2;
  float boostEscalate3;
  float boostDecayTime;
  float distSpikeRiseCm;
  float distSpikeFallCm;
  uint16_t crc;
};

struct __attribute__((packed)) MasterCommandPacket {
  PacketHeader header;
  uint8_t command;
  float value;
  uint16_t targetSeq;
  uint16_t crc;
};

struct __attribute__((packed)) AckPacket {
  PacketHeader header;
  uint16_t ackSeq;
  uint8_t result;
  uint16_t crc;
};

uint16_t crc16(const uint8_t *data, size_t len);
bool hasValidCrc(const void *packet, size_t len);
void writeCrc(void *packet, size_t len);
const char *modeName(uint8_t mode);
const char *runStateName(uint8_t runState);

}

#endif
