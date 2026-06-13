#ifdef ROLE_NODE_ESPNOW

#include "NodeEspNow.h"
#include <WiFi.h>
#include <esp_now.h>
#include <esp_wifi.h>
#include <esp_arduino_version.h>

namespace NodeEspNow {
namespace {

uint8_t gNodeId = 0;
uint8_t gMasterMac[6] = {0};
uint16_t gSeq = 0;
QueueHandle_t gCommandQueue = nullptr;
volatile uint32_t gTxOk = 0;
volatile uint32_t gTxFail = 0;
volatile uint32_t gRx = 0;

void fillHeader(VfdNet::PacketHeader &header, uint8_t type) {
  header.version = VfdNet::PROTOCOL_VERSION;
  header.type = type;
  header.nodeId = gNodeId;
  header.seq = gSeq++;
  header.uptimeMs = millis();
}

void onSent(const uint8_t *mac, esp_now_send_status_t status) {
  (void)mac;
  if (status == ESP_NOW_SEND_SUCCESS) {
    gTxOk++;
  } else {
    gTxFail++;
  }
}

void handleRx(const uint8_t *data, int len) {
  if (data == nullptr || len < (int)sizeof(VfdNet::PacketHeader)) {
    return;
  }

  const VfdNet::PacketHeader *header = reinterpret_cast<const VfdNet::PacketHeader *>(data);
  if (header->version != VfdNet::PROTOCOL_VERSION || header->type != VfdNet::PKT_COMMAND) {
    return;
  }

  if (len != (int)sizeof(VfdNet::MasterCommandPacket)) {
    return;
  }

  VfdNet::MasterCommandPacket command;
  memcpy(&command, data, sizeof(command));
  if (!VfdNet::hasValidCrc(&command, sizeof(command))) {
    sendAck(command.header.seq, VfdNet::ACK_BAD_CRC);
    return;
  }

  if (command.header.nodeId != gNodeId && command.header.nodeId != 0) {
    sendAck(command.header.seq, VfdNet::ACK_BAD_TARGET);
    return;
  }

  gRx++;
  if (gCommandQueue != nullptr) {
    xQueueSend(gCommandQueue, &command, 0);
  }
}

#if ESP_ARDUINO_VERSION_MAJOR >= 3
void onRecv(const esp_now_recv_info_t *info, const uint8_t *data, int len) {
  (void)info;
  handleRx(data, len);
}
#else
void onRecv(const uint8_t *mac, const uint8_t *data, int len) {
  (void)mac;
  handleRx(data, len);
}
#endif

bool addPeer(const uint8_t mac[6], uint8_t channel) {
  esp_now_peer_info_t peer{};
  memcpy(peer.peer_addr, mac, 6);
  peer.channel = channel;
  peer.encrypt = false;
  peer.ifidx = WIFI_IF_STA;

  if (esp_now_is_peer_exist(mac)) {
    return true;
  }
  return esp_now_add_peer(&peer) == ESP_OK;
}

}

bool begin(uint8_t nodeId, const uint8_t masterMac[6], uint8_t channel) {
  gNodeId = nodeId;
  memcpy(gMasterMac, masterMac, 6);
  gCommandQueue = xQueueCreate(8, sizeof(VfdNet::MasterCommandPacket));
  if (gCommandQueue == nullptr) {
    return false;
  }

  WiFi.mode(WIFI_STA);
  WiFi.persistent(false);
  esp_wifi_set_promiscuous(true);
  esp_wifi_set_channel(channel, WIFI_SECOND_CHAN_NONE);
  esp_wifi_set_promiscuous(false);

  if (esp_now_init() != ESP_OK) {
    return false;
  }

  esp_now_register_send_cb(onSent);
  esp_now_register_recv_cb(onRecv);
  if (!addPeer(gMasterMac, channel)) {
    return false;
  }

  sendHello();
  return true;
}

void sendHello() {
  VfdNet::HelloPacket packet{};
  fillHeader(packet.header, VfdNet::PKT_HELLO);
  WiFi.macAddress(packet.mac);
  VfdNet::writeCrc(&packet, sizeof(packet));
  esp_now_send(gMasterMac, reinterpret_cast<uint8_t *>(&packet), sizeof(packet));
}

void sendStatus(const StatusInput &status) {
  VfdNet::NodeStatusPacket packet{};
  fillHeader(packet.header, VfdNet::PKT_STATUS);
  packet.distanceCm = status.distanceCm;
  packet.frequencyHz = status.frequencyHz;
  packet.mode = status.mode;
  packet.runState = status.runState;
  packet.boostLevel = status.boostLevel;
  packet.vfdProfile = status.vfdProfile;
  packet.alarmFlags = status.alarmFlags;
  packet.vfdOnline = status.vfdOnline ? 1 : 0;
  packet.modbusOk = status.modbusOk;
  packet.modbusFail = status.modbusFail;
  packet.freeHeap = ESP.getFreeHeap();
  packet.userEnabled = status.userEnabled ? 1 : 0;
  packet.udmin = status.udmin;
  packet.udmax = status.udmax;
  packet.ufmin = status.ufmin;
  packet.ufmax = status.ufmax;
  packet.boostLevel1Pct = status.boostLevel1Pct;
  packet.boostLevel2Pct = status.boostLevel2Pct;
  packet.boostLevel3Pct = status.boostLevel3Pct;
  packet.boostLevel1Hold = status.boostLevel1Hold;
  packet.boostLevel2Hold = status.boostLevel2Hold;
  packet.boostLevel3Hold = status.boostLevel3Hold;
  packet.boostEscalate2 = status.boostEscalate2;
  packet.boostEscalate3 = status.boostEscalate3;
  packet.boostDecayTime = status.boostDecayTime;
  packet.distSpikeRiseCm = status.distSpikeRiseCm;
  packet.distSpikeFallCm = status.distSpikeFallCm;
  VfdNet::writeCrc(&packet, sizeof(packet));
  esp_now_send(gMasterMac, reinterpret_cast<uint8_t *>(&packet), sizeof(packet));
}

bool pollCommand(VfdNet::MasterCommandPacket &command) {
  if (gCommandQueue == nullptr) {
    return false;
  }
  return xQueueReceive(gCommandQueue, &command, 0) == pdTRUE;
}

void sendAck(uint16_t ackSeq, uint8_t result) {
  VfdNet::AckPacket packet{};
  fillHeader(packet.header, VfdNet::PKT_ACK);
  packet.ackSeq = ackSeq;
  packet.result = result;
  VfdNet::writeCrc(&packet, sizeof(packet));
  esp_now_send(gMasterMac, reinterpret_cast<uint8_t *>(&packet), sizeof(packet));
}

uint32_t txOkCount() {
  return gTxOk;
}

uint32_t txFailCount() {
  return gTxFail;
}

uint32_t rxCount() {
  return gRx;
}

}

#endif
