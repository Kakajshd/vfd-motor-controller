#include "Protocol.h"

namespace VfdNet {

uint16_t crc16(const uint8_t *data, size_t len) {
  uint16_t crc = 0xFFFF;
  for (size_t i = 0; i < len; i++) {
    crc ^= data[i];
    for (uint8_t bit = 0; bit < 8; bit++) {
      if (crc & 0x0001) {
        crc = (crc >> 1) ^ 0xA001;
      } else {
        crc >>= 1;
      }
    }
  }
  return crc;
}

bool hasValidCrc(const void *packet, size_t len) {
  if (packet == nullptr || len < sizeof(uint16_t)) {
    return false;
  }
  const uint8_t *bytes = static_cast<const uint8_t *>(packet);
  uint16_t expected = 0;
  memcpy(&expected, bytes + len - sizeof(uint16_t), sizeof(expected));
  return expected == crc16(bytes, len - sizeof(uint16_t));
}

void writeCrc(void *packet, size_t len) {
  if (packet == nullptr || len < sizeof(uint16_t)) {
    return;
  }
  uint8_t *bytes = static_cast<uint8_t *>(packet);
  uint16_t value = crc16(bytes, len - sizeof(uint16_t));
  memcpy(bytes + len - sizeof(uint16_t), &value, sizeof(value));
}

const char *modeName(uint8_t mode) {
  switch (mode) {
    case MACHINE_MODE_AUTO: return "AUTO";
    case MACHINE_MODE_MANUAL: return "MANUAL";
    case MACHINE_MODE_TEST: return "TEST";
    case MACHINE_MODE_STOP: return "STOP";
    default: return "UNKNOWN";
  }
}

const char *runStateName(uint8_t runState) {
  return runState == RUN_STATE_RUN ? "RUN" : "STOP";
}

}
