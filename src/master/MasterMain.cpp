#ifdef ROLE_MASTER

#include <Arduino.h>
#include <WiFi.h>
#include <esp_now.h>
#include <esp_wifi.h>
#include <esp_arduino_version.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include "../common/Protocol.h"

namespace {

static constexpr const char *AP_SSID = "VFD_MASTER";
static constexpr const char *AP_PASS = "12345678";
static constexpr uint32_t DASHBOARD_PUSH_MS = 300;

AsyncWebServer server(80);
AsyncWebSocket ws("/ws");
QueueHandle_t rxQueue = nullptr;
QueueHandle_t cmdQueue = nullptr;
uint16_t txSeq = 0;

struct EspNowFrame {
  uint8_t mac[6];
  uint8_t data[250];
  uint8_t len;
};

struct CommandJob {
  uint8_t nodeId;
  uint8_t command;
  float value;
};

struct NodeSettingsMirror {
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

struct MachineState {
  uint8_t nodeId = 0;
  uint8_t mac[6] = {0};
  uint32_t lastSeenMs = 0;
  uint16_t lastSeq = 0;
  float distanceCm = 0.0f;
  float frequencyHz = 0.0f;
  uint8_t mode = VfdNet::MACHINE_MODE_STOP;
  uint8_t runState = VfdNet::RUN_STATE_STOP;
  uint8_t boostLevel = 0;
  uint8_t vfdProfile = 0;
  uint16_t alarmFlags = VfdNet::ALARM_NONE;
  bool online = false;
  bool vfdOnline = false;
  uint32_t modbusOk = 0;
  uint32_t modbusFail = 0;
  uint32_t freeHeap = 0;
  uint32_t rxPackets = 0;
  uint32_t crcErrors = 0;
  uint32_t seqLost = 0;
  uint8_t lastAckResult = 0;
  uint16_t lastAckSeq = 0;
  NodeSettingsMirror config;
};

MachineState machines[VfdNet::MAX_NODES + 1];

bool addPeer(const uint8_t mac[6]) {
  if (esp_now_is_peer_exist(mac)) {
    return true;
  }
  esp_now_peer_info_t peer{};
  memcpy(peer.peer_addr, mac, 6);
  peer.channel = VfdNet::MASTER_AP_CHANNEL;
  peer.encrypt = false;
  peer.ifidx = WIFI_IF_STA;
  return esp_now_add_peer(&peer) == ESP_OK;
}

void queueRx(const uint8_t *mac, const uint8_t *data, int len) {
  if (rxQueue == nullptr || mac == nullptr || data == nullptr || len <= 0) {
    return;
  }
  EspNowFrame frame{};
  memcpy(frame.mac, mac, 6);
  frame.len = min(len, (int)sizeof(frame.data));
  memcpy(frame.data, data, frame.len);
  xQueueSend(rxQueue, &frame, 0);
}

#if ESP_ARDUINO_VERSION_MAJOR >= 3
void onRecv(const esp_now_recv_info_t *info, const uint8_t *data, int len) {
  if (info == nullptr) {
    return;
  }
  queueRx(info->src_addr, data, len);
}
#else
void onRecv(const uint8_t *mac, const uint8_t *data, int len) {
  queueRx(mac, data, len);
}
#endif

void fillHeader(VfdNet::PacketHeader &header, uint8_t type, uint8_t nodeId) {
  header.version = VfdNet::PROTOCOL_VERSION;
  header.type = type;
  header.nodeId = nodeId;
  header.seq = txSeq++;
  header.uptimeMs = millis();
}

void sendCommand(const CommandJob &job) {
  if (job.nodeId == 0 || job.nodeId > VfdNet::MAX_NODES || machines[job.nodeId].nodeId == 0) {
    return;
  }

  VfdNet::MasterCommandPacket packet{};
  fillHeader(packet.header, VfdNet::PKT_COMMAND, job.nodeId);
  packet.command = job.command;
  packet.value = job.value;
  packet.targetSeq = machines[job.nodeId].lastSeq;
  VfdNet::writeCrc(&packet, sizeof(packet));

  addPeer(machines[job.nodeId].mac);
  esp_now_send(machines[job.nodeId].mac, reinterpret_cast<uint8_t *>(&packet), sizeof(packet));
}

void handleHello(const EspNowFrame &frame) {
  if (frame.len != sizeof(VfdNet::HelloPacket)) {
    return;
  }
  VfdNet::HelloPacket packet{};
  memcpy(&packet, frame.data, sizeof(packet));
  if (!VfdNet::hasValidCrc(&packet, sizeof(packet))) {
    return;
  }
  if (packet.header.nodeId == 0 || packet.header.nodeId > VfdNet::MAX_NODES) {
    return;
  }

  MachineState &m = machines[packet.header.nodeId];
  m.nodeId = packet.header.nodeId;
  memcpy(m.mac, frame.mac, 6);
  m.lastSeenMs = millis();
  m.online = true;
  m.alarmFlags &= ~VfdNet::ALARM_NODE_OFFLINE;
  addPeer(frame.mac);
  Serial.printf("[MASTER] HELLO node=%u mac=%02X:%02X:%02X:%02X:%02X:%02X\n",
                m.nodeId, frame.mac[0], frame.mac[1], frame.mac[2], frame.mac[3], frame.mac[4], frame.mac[5]);
}

void handleStatus(const EspNowFrame &frame) {
  if (frame.len != sizeof(VfdNet::NodeStatusPacket)) {
    return;
  }
  VfdNet::NodeStatusPacket packet{};
  memcpy(&packet, frame.data, sizeof(packet));
  uint8_t id = packet.header.nodeId;
  if (id == 0 || id > VfdNet::MAX_NODES) {
    return;
  }

  MachineState &m = machines[id];
  if (!VfdNet::hasValidCrc(&packet, sizeof(packet))) {
    m.crcErrors++;
    return;
  }

  if (m.nodeId == 0) {
    m.nodeId = id;
    memcpy(m.mac, frame.mac, 6);
    addPeer(frame.mac);
  }

  if (m.rxPackets > 0) {
    uint16_t expected = m.lastSeq + 1;
    if (packet.header.seq != expected) {
      m.seqLost++;
    }
  }

  m.lastSeenMs = millis();
  m.lastSeq = packet.header.seq;
  m.distanceCm = packet.distanceCm;
  m.frequencyHz = packet.frequencyHz;
  m.mode = packet.mode;
  m.runState = packet.runState;
  m.boostLevel = packet.boostLevel;
  m.vfdProfile = packet.vfdProfile;
  m.alarmFlags = packet.alarmFlags;
  m.online = true;
  m.vfdOnline = packet.vfdOnline != 0;
  m.modbusOk = packet.modbusOk;
  m.modbusFail = packet.modbusFail;
  m.freeHeap = packet.freeHeap;
  m.config.userEnabled = packet.userEnabled != 0;
  m.config.udmin = packet.udmin;
  m.config.udmax = packet.udmax;
  m.config.ufmin = packet.ufmin;
  m.config.ufmax = packet.ufmax;
  m.config.boostLevel1Pct = packet.boostLevel1Pct;
  m.config.boostLevel2Pct = packet.boostLevel2Pct;
  m.config.boostLevel3Pct = packet.boostLevel3Pct;
  m.config.boostLevel1Hold = packet.boostLevel1Hold;
  m.config.boostLevel2Hold = packet.boostLevel2Hold;
  m.config.boostLevel3Hold = packet.boostLevel3Hold;
  m.config.boostEscalate2 = packet.boostEscalate2;
  m.config.boostEscalate3 = packet.boostEscalate3;
  m.config.boostDecayTime = packet.boostDecayTime;
  m.config.distSpikeRiseCm = packet.distSpikeRiseCm;
  m.config.distSpikeFallCm = packet.distSpikeFallCm;
  m.rxPackets++;
}

void handleAck(const EspNowFrame &frame) {
  if (frame.len != sizeof(VfdNet::AckPacket)) {
    return;
  }
  VfdNet::AckPacket packet{};
  memcpy(&packet, frame.data, sizeof(packet));
  if (!VfdNet::hasValidCrc(&packet, sizeof(packet))) {
    return;
  }
  uint8_t id = packet.header.nodeId;
  if (id == 0 || id > VfdNet::MAX_NODES) {
    return;
  }
  machines[id].lastAckSeq = packet.ackSeq;
  machines[id].lastAckResult = packet.result;
}

void registryTask(void *pv) {
  (void)pv;
  EspNowFrame frame{};

  for (;;) {
    while (xQueueReceive(rxQueue, &frame, pdMS_TO_TICKS(50)) == pdTRUE) {
      if (frame.len < sizeof(VfdNet::PacketHeader)) {
        continue;
      }
      const VfdNet::PacketHeader *header = reinterpret_cast<const VfdNet::PacketHeader *>(frame.data);
      if (header->version != VfdNet::PROTOCOL_VERSION) {
        continue;
      }
      if (header->type == VfdNet::PKT_HELLO) {
        handleHello(frame);
      } else if (header->type == VfdNet::PKT_STATUS) {
        handleStatus(frame);
      } else if (header->type == VfdNet::PKT_ACK) {
        handleAck(frame);
      }
    }

    uint32_t now = millis();
    for (uint8_t id = 1; id <= VfdNet::MAX_NODES; id++) {
      MachineState &m = machines[id];
      if (m.nodeId == 0) {
        continue;
      }
      if (now - m.lastSeenMs > VfdNet::NODE_OFFLINE_TIMEOUT_MS) {
        m.online = false;
        m.alarmFlags |= VfdNet::ALARM_NODE_OFFLINE;
      }
    }
  }
}

String buildSnapshotJson() {
  String json;
  json.reserve(9000);
  json += "{\"type\":\"snapshot\",\"uptime\":";
  json += String(millis() / 1000UL);
  json += ",\"nodes\":[";
  bool first = true;
  uint32_t now = millis();
  for (uint8_t id = 1; id <= VfdNet::MAX_NODES; id++) {
    MachineState &m = machines[id];
    if (m.nodeId == 0) {
      continue;
    }
    if (!first) {
      json += ",";
    }
    first = false;
    json += "{\"id\":";
    json += id;
    json += ",\"online\":";
    json += m.online ? "true" : "false";
    json += ",\"lastSeen\":";
    json += String(now - m.lastSeenMs);
    json += ",\"distance\":";
    json += String(m.distanceCm, 1);
    json += ",\"freq\":";
    json += String(m.frequencyHz, 1);
    json += ",\"mode\":\"";
    json += VfdNet::modeName(m.mode);
    json += "\",\"run\":\"";
    json += VfdNet::runStateName(m.runState);
    json += "\",\"boost\":";
    json += String(m.boostLevel);
    json += ",\"vfd\":";
    json += m.vfdOnline ? "true" : "false";
    json += ",\"alarm\":";
    json += String(m.alarmFlags);
    json += ",\"rx\":";
    json += String(m.rxPackets);
    json += ",\"loss\":";
    json += String(m.seqLost);
    json += ",\"crc\":";
    json += String(m.crcErrors);
    json += ",\"heap\":";
    json += String(m.freeHeap);
    json += ",\"ackSeq\":";
    json += String(m.lastAckSeq);
    json += ",\"ackResult\":";
    json += String(m.lastAckResult);
    json += ",\"config\":{\"uen\":";
    json += m.config.userEnabled ? "true" : "false";
    json += ",\"udmin\":";
    json += String(m.config.udmin, 2);
    json += ",\"udmax\":";
    json += String(m.config.udmax, 2);
    json += ",\"ufmin\":";
    json += String(m.config.ufmin, 2);
    json += ",\"ufmax\":";
    json += String(m.config.ufmax, 2);
    json += ",\"boost1\":";
    json += String(m.config.boostLevel1Pct, 2);
    json += ",\"boost2\":";
    json += String(m.config.boostLevel2Pct, 2);
    json += ",\"boost3\":";
    json += String(m.config.boostLevel3Pct, 2);
    json += ",\"hold1\":";
    json += String(m.config.boostLevel1Hold, 2);
    json += ",\"hold2\":";
    json += String(m.config.boostLevel2Hold, 2);
    json += ",\"hold3\":";
    json += String(m.config.boostLevel3Hold, 2);
    json += ",\"esc2\":";
    json += String(m.config.boostEscalate2, 2);
    json += ",\"esc3\":";
    json += String(m.config.boostEscalate3, 2);
    json += ",\"decay\":";
    json += String(m.config.boostDecayTime, 2);
    json += ",\"distRise\":";
    json += String(m.config.distSpikeRiseCm, 2);
    json += ",\"distFall\":";
    json += String(m.config.distSpikeFallCm, 2);
    json += "}";
    json += "}";
  }
  json += "]}";
  return json;
}

void dashboardPushTask(void *pv) {
  (void)pv;
  for (;;) {
    if (ws.count() > 0) {
      ws.textAll(buildSnapshotJson());
    }
    ws.cleanupClients();
    vTaskDelay(pdMS_TO_TICKS(DASHBOARD_PUSH_MS));
  }
}

void commandTask(void *pv) {
  (void)pv;
  CommandJob job{};
  for (;;) {
    if (xQueueReceive(cmdQueue, &job, portMAX_DELAY) == pdTRUE) {
      sendCommand(job);
    }
  }
}

uint8_t parseCommandName(const String &name) {
  if (name == "RUN") return VfdNet::CMD_RUN;
  if (name == "STOP") return VfdNet::CMD_STOP;
  if (name == "AUTO") return VfdNet::CMD_APPLY_VFD_AUTO;
  if (name == "MANUAL") return VfdNet::CMD_APPLY_VFD_MANUAL;
  if (name == "FREQ") return VfdNet::CMD_SET_FREQ;
  if (name == "RESET_ALARM") return VfdNet::CMD_RESET_ALARM;
  if (name == "UEN") return VfdNet::CMD_SET_UEN;
  if (name == "UDMIN") return VfdNet::CMD_SET_UDMIN;
  if (name == "UDMAX") return VfdNet::CMD_SET_UDMAX;
  if (name == "UFMIN") return VfdNet::CMD_SET_UFMIN;
  if (name == "UFMAX") return VfdNet::CMD_SET_UFMAX;
  if (name == "BOOST1") return VfdNet::CMD_SET_BOOST_L1;
  if (name == "BOOST2") return VfdNet::CMD_SET_BOOST_L2;
  if (name == "BOOST3") return VfdNet::CMD_SET_BOOST_L3;
  if (name == "HOLD1") return VfdNet::CMD_SET_HOLD_L1;
  if (name == "HOLD2") return VfdNet::CMD_SET_HOLD_L2;
  if (name == "HOLD3") return VfdNet::CMD_SET_HOLD_L3;
  if (name == "ESC2") return VfdNet::CMD_SET_ESC_L2;
  if (name == "ESC3") return VfdNet::CMD_SET_ESC_L3;
  if (name == "DECAY") return VfdNet::CMD_SET_DECAY;
  if (name == "DISTRISE") return VfdNet::CMD_SET_DIST_RISE;
  if (name == "DISTFALL") return VfdNet::CMD_SET_DIST_FALL;
  return 0;
}

void handleWsData(uint8_t *data, size_t len) {
  String payload;
  payload.reserve(len + 1);
  for (size_t i = 0; i < len; i++) {
    payload += (char)data[i];
  }
  payload.trim();

  int idPos = payload.indexOf("\"nodeId\"");
  int cmdPos = payload.indexOf("\"command\"");
  if (idPos < 0 || cmdPos < 0) {
    return;
  }

  int idColon = payload.indexOf(':', idPos);
  int cmdColon = payload.indexOf(':', cmdPos);
  if (idColon < 0 || cmdColon < 0) {
    return;
  }

  uint8_t nodeId = (uint8_t)payload.substring(idColon + 1).toInt();
  int q1 = payload.indexOf('"', cmdColon + 1);
  int q2 = payload.indexOf('"', q1 + 1);
  if (q1 < 0 || q2 < 0) {
    return;
  }
  String cmdName = payload.substring(q1 + 1, q2);
  cmdName.toUpperCase();

  float value = 0.0f;
  int valuePos = payload.indexOf("\"value\"");
  if (valuePos >= 0) {
    int valueColon = payload.indexOf(':', valuePos);
    if (valueColon > 0) {
      value = payload.substring(valueColon + 1).toFloat();
    }
  }

  CommandJob job{};
  job.nodeId = nodeId;
  job.command = parseCommandName(cmdName);
  job.value = value;
  if (job.command != 0 && cmdQueue != nullptr) {
    xQueueSend(cmdQueue, &job, 0);
  }
}

const char DASHBOARD_HTML[] PROGMEM = R"HTML(
<!doctype html><html><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>VFD Master Dashboard</title>
<style>
*{box-sizing:border-box}body{margin:0;background:#101418;color:#e8eef4;font-family:Arial,sans-serif}
header{display:flex;justify-content:space-between;align-items:center;padding:14px 18px;background:#18212b;border-bottom:1px solid #2a3644}
h1{font-size:20px;margin:0}.meta{color:#9fb0c3;font-size:13px}.wrap{padding:14px;overflow-x:auto}
table{width:100%;border-collapse:collapse;background:#141b22}th,td{padding:10px;border-bottom:1px solid #263241;text-align:left;white-space:nowrap}
th{color:#9fb0c3;font-size:12px;text-transform:uppercase;background:#1b2530}tr.offline{color:#ff8e8e;background:#241719}
.pill{display:inline-block;padding:3px 8px;border-radius:999px;font-size:12px;font-weight:700}.ok{background:#113b28;color:#6dffa8}.bad{background:#4a1d22;color:#ff9a9a}.warn{background:#493712;color:#ffd36b}
button{border:0;border-radius:6px;padding:7px 10px;margin:2px;background:#2d6cdf;color:white;font-weight:700;cursor:pointer}button.stop{background:#b33a3a}button.mode{background:#5d6978}button.cfg{background:#198754}
input{width:76px;padding:6px;background:#0f141a;border:1px solid #344456;color:#fff;border-radius:5px}label{display:grid;gap:4px;color:#9fb0c3;font-size:12px}
.panel{position:fixed;inset:auto 0 0 0;background:#18212b;border-top:1px solid #344456;padding:14px;box-shadow:0 -8px 24px #0008;display:none;max-height:78vh;overflow:auto}
.panel.open{display:block}.panelHead{display:flex;justify-content:space-between;align-items:center;margin-bottom:10px}.panelHead h2{font-size:18px;margin:0}
.grid{display:grid;grid-template-columns:repeat(auto-fit,minmax(110px,1fr));gap:10px}.actions{display:flex;justify-content:flex-end;gap:8px;margin-top:12px}.muted{color:#9fb0c3;font-size:12px}
</style></head><body>
<header><h1>VFD Master Dashboard</h1><div class="meta" id="meta">Connecting...</div></header>
<div class="wrap"><table><thead><tr><th>ID</th><th>Link</th><th>Mode</th><th>Run</th><th>Freq</th><th>Distance</th><th>Boost</th><th>VFD</th><th>Alarm</th><th>Diag</th><th>Command</th></tr></thead><tbody id="body"></tbody></table></div>
<section class="panel" id="panel"><div class="panelHead"><h2 id="ptitle">Node Settings</h2><button class="mode" onclick="closePanel()">Close</button></div>
<div class="muted" id="pstatus">Values are copied from latest NODE telemetry.</div>
<div class="grid">
<label>UEN<input id="c_uen" type="number" min="0" max="1" step="1"></label>
<label>UDMIN<input id="c_udmin" type="number" step="0.1"></label><label>UDMAX<input id="c_udmax" type="number" step="0.1"></label>
<label>UFMIN<input id="c_ufmin" type="number" step="0.1"></label><label>UFMAX<input id="c_ufmax" type="number" step="0.1"></label>
<label>BOOST L1 %<input id="c_boost1" type="number" step="0.1"></label><label>BOOST L2 %<input id="c_boost2" type="number" step="0.1"></label><label>BOOST L3 %<input id="c_boost3" type="number" step="0.1"></label>
<label>HOLD L1 s<input id="c_hold1" type="number" step="0.1"></label><label>HOLD L2 s<input id="c_hold2" type="number" step="0.1"></label><label>HOLD L3 s<input id="c_hold3" type="number" step="0.1"></label>
<label>ESC L2 s<input id="c_esc2" type="number" step="0.1"></label><label>ESC L3 s<input id="c_esc3" type="number" step="0.1"></label><label>DECAY s<input id="c_decay" type="number" step="0.1"></label>
<label>DIST RISE cm<input id="c_distRise" type="number" step="0.1"></label><label>DIST FALL cm<input id="c_distFall" type="number" step="0.1"></label>
</div><div class="actions"><button onclick="saveConfig()">Apply To Node</button></div></section>
<script>
const body=document.getElementById("body"),meta=document.getElementById("meta");
let ws,nodes=[],selectedId=0;
function cls(ok){return ok?"pill ok":"pill bad"}
function send(id,command,value=0){ws&&ws.readyState===1&&ws.send(JSON.stringify({nodeId:id,command,value}))}
function row(n){return `<tr class="${n.online?"":"offline"}">
<td>${n.id}</td><td><span class="${cls(n.online)}">${n.online?"ONLINE":"OFFLINE"}</span><div>${n.lastSeen} ms</div></td>
<td>${n.mode}</td><td><span class="${n.run==="RUN"?"pill ok":"pill warn"}">${n.run}</span></td>
<td>${n.freq.toFixed(1)} Hz</td><td>${n.distance.toFixed(1)} cm</td><td>L${n.boost}</td><td>${n.vfd?"OK":"FAIL"}</td><td>${n.alarm}</td>
<td>rx ${n.rx} / loss ${n.loss} / crc ${n.crc}<br>heap ${n.heap}<br>ack ${n.ackSeq}/${n.ackResult}</td>
<td><button onclick="send(${n.id},'RUN')">RUN</button><button class="stop" onclick="send(${n.id},'STOP')">STOP</button><button class="mode" onclick="send(${n.id},'AUTO')">AUTO</button><button class="mode" onclick="send(${n.id},'MANUAL')">MANUAL</button><button class="cfg" onclick="openPanel(${n.id})">SET</button><input id="f${n.id}" type="number" step="0.1"><button onclick="send(${n.id},'FREQ',parseFloat(document.getElementById('f${n.id}').value)||0)">Hz</button></td>
</tr>`}
function fill(id,v){document.getElementById(id).value=Number(v||0).toFixed(2)}
function openPanel(id){const n=nodes.find(x=>x.id===id);if(!n)return;selectedId=id;const c=n.config||{};document.getElementById("ptitle").textContent=`Node ${id} Settings`;document.getElementById("pstatus").textContent=n.online?"Online - edit carefully and apply":"Offline - commands may be ignored";document.getElementById("c_uen").value=c.uen?1:0;fill("c_udmin",c.udmin);fill("c_udmax",c.udmax);fill("c_ufmin",c.ufmin);fill("c_ufmax",c.ufmax);fill("c_boost1",c.boost1);fill("c_boost2",c.boost2);fill("c_boost3",c.boost3);fill("c_hold1",c.hold1);fill("c_hold2",c.hold2);fill("c_hold3",c.hold3);fill("c_esc2",c.esc2);fill("c_esc3",c.esc3);fill("c_decay",c.decay);fill("c_distRise",c.distRise);fill("c_distFall",c.distFall);document.getElementById("panel").classList.add("open")}
function closePanel(){document.getElementById("panel").classList.remove("open")}
function val(id){return parseFloat(document.getElementById(id).value)||0}
function saveConfig(){if(!selectedId)return;const map=[["UEN",val("c_uen")>0?1:0],["UDMIN",val("c_udmin")],["UDMAX",val("c_udmax")],["UFMIN",val("c_ufmin")],["UFMAX",val("c_ufmax")],["BOOST1",val("c_boost1")],["BOOST2",val("c_boost2")],["BOOST3",val("c_boost3")],["HOLD1",val("c_hold1")],["HOLD2",val("c_hold2")],["HOLD3",val("c_hold3")],["ESC2",val("c_esc2")],["ESC3",val("c_esc3")],["DECAY",val("c_decay")],["DISTRISE",val("c_distRise")],["DISTFALL",val("c_distFall")]];map.forEach((x,i)=>setTimeout(()=>send(selectedId,x[0],x[1]),i*45));document.getElementById("pstatus").textContent="Commands sent. Wait for telemetry to confirm values."}
function connect(){ws=new WebSocket(`ws://${location.host}/ws`);ws.onopen=()=>meta.textContent="Connected";ws.onclose=()=>{meta.textContent="Disconnected, retrying...";setTimeout(connect,1500)};ws.onmessage=e=>{const m=JSON.parse(e.data);if(m.type==="snapshot"){nodes=m.nodes;meta.textContent=`Uptime ${m.uptime}s | Nodes ${m.nodes.length}`;body.innerHTML=m.nodes.map(row).join("")||"<tr><td colspan=11>No nodes detected</td></tr>"}}}
connect();
</script></body></html>
)HTML";

void setupWeb() {
  ws.onEvent([](AsyncWebSocket *server, AsyncWebSocketClient *client, AwsEventType type, void *arg, uint8_t *data, size_t len) {
    (void)server;
    (void)client;
    if (type == WS_EVT_DATA) {
      AwsFrameInfo *info = static_cast<AwsFrameInfo *>(arg);
      if (info != nullptr && info->final && info->index == 0 && info->len == len && info->opcode == WS_TEXT) {
        handleWsData(data, len);
      }
    }
  });
  server.addHandler(&ws);
  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request) {
    request->send_P(200, "text/html", DASHBOARD_HTML);
  });
  server.on("/api/state", HTTP_GET, [](AsyncWebServerRequest *request) {
    request->send(200, "application/json", buildSnapshotJson());
  });
  server.begin();
}

void initRadio() {
  WiFi.mode(WIFI_AP_STA);
  WiFi.persistent(false);
  WiFi.softAP(AP_SSID, AP_PASS, VfdNet::MASTER_AP_CHANNEL, false, 4);
  esp_wifi_set_channel(VfdNet::MASTER_AP_CHANNEL, WIFI_SECOND_CHAN_NONE);
  Serial.print("[MASTER] STA MAC for nodes: ");
  Serial.println(WiFi.macAddress());
  Serial.print("[MASTER] AP IP: ");
  Serial.println(WiFi.softAPIP());

  if (esp_now_init() != ESP_OK) {
    Serial.println("[MASTER] ESP-NOW init failed");
    return;
  }
  esp_now_register_recv_cb(onRecv);
}

}

void setup() {
  Serial.begin(115200);
  uint32_t serialWaitStart = millis();
  while (!Serial && millis() - serialWaitStart < 3000UL) {
    delay(10);
  }
  delay(300);
  Serial.println();
  Serial.println("[MASTER] Booting...");
  rxQueue = xQueueCreate(64, sizeof(EspNowFrame));
  cmdQueue = xQueueCreate(16, sizeof(CommandJob));
  initRadio();
  setupWeb();
  xTaskCreatePinnedToCore(registryTask, "registry", 8192, nullptr, 3, nullptr, 0);
  xTaskCreatePinnedToCore(commandTask, "cmdtx", 4096, nullptr, 2, nullptr, 0);
  xTaskCreatePinnedToCore(dashboardPushTask, "dash", 8192, nullptr, 1, nullptr, 1);
  Serial.println("[MASTER] Dashboard ready: http://192.168.4.1/");
}

void loop() {
  static uint32_t lastHeartbeatMs = 0;
  uint32_t now = millis();
  if (now - lastHeartbeatMs >= 3000UL) {
    lastHeartbeatMs = now;
    Serial.printf("[MASTER] alive uptime=%lus clients=%u free_heap=%lu sta_mac=%s ap_ip=%s\n",
                  (unsigned long)(now / 1000UL),
                  (unsigned)ws.count(),
                  (unsigned long)ESP.getFreeHeap(),
                  WiFi.macAddress().c_str(),
                  WiFi.softAPIP().toString().c_str());
  }
  vTaskDelay(pdMS_TO_TICKS(200));
}

#endif
