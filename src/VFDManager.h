#ifndef VFD_MANAGER_H
#define VFD_MANAGER_H

#include <ModbusMaster.h>
#include "Config.h"

// Callback điều khiển hướng RS485
void preTransmission() {
  digitalWrite(MAX485_DE_RE, HIGH);
}
void postTransmission() {
  digitalWrite(MAX485_DE_RE, LOW);
}

class VFDManager {
private:
    uint8_t _slaveID;
    ModbusMaster _node;
    static const uint8_t WRITE_RETRY_COUNT = 3;
    // static VFDManager* _instance;

    bool writeRegister(uint16_t reg, uint16_t value) {
        for (uint8_t attempt = 0; attempt < WRITE_RETRY_COUNT; attempt++) {
            uint8_t result = _node.writeSingleRegister(reg, value);
            if (result == _node.ku8MBSuccess) {
                return true;
            }
        }

        return false;
    }

public:
    // static VFDManager* getInstance() {return _instance;};

    void begin(uint8_t slaveID, HardwareSerial &serial) {
        _slaveID = slaveID;
        pinMode(MAX485_DE_RE, OUTPUT);
        digitalWrite(MAX485_DE_RE, LOW);

        _node.begin(_slaveID, serial);
        _node.preTransmission(preTransmission);
        _node.postTransmission(postTransmission);

        // Gán chính đối tượng này cho biến instance khi vừa được tạo ra
        // _instance = this; 
    }

    bool setFrequency(float freq) {
        float clampedFreq = constrain(freq, 0.0f, 400.0f);
        return writeRegister(0x2001, (uint16_t)(clampedFreq * 100));
    }

    bool sendControl(uint16_t cmd) {
        return writeRegister(0x2000, cmd);
    }

    bool writeHoldingRegister(uint16_t reg, uint16_t value) {
        return writeRegister(reg, value);
    }
};

#endif