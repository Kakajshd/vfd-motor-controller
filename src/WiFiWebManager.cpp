#include "WiFiWebManager.h"
#include <Arduino.h>

static AsyncEventSource gEvents("/events");

namespace {
void restartEspSoonTask(void *pvParameters) {
  (void)pvParameters;
  vTaskDelay(pdMS_TO_TICKS(150));
  ESP.restart();
}

void scheduleRestartEsp() {
  xTaskCreatePinnedToCore(
    restartEspSoonTask,
    "esp-reset",
    2048,
    nullptr,
    1,
    nullptr,
    tskNO_AFFINITY);
}

}

namespace {
static const unsigned long WIFI_RETRY_MIN_MS = 1000;
static const unsigned long WIFI_RETRY_MAX_MS = 30000;

String gSsid = "";
String gPass = "";
bool gConnected = false;
unsigned long gLastAttemptMs = 0;
unsigned long gRetryDelayMs = WIFI_RETRY_MIN_MS;
uint32_t gReconnectAttempts = 0;
uint32_t gReconnectSuccess = 0;
uint32_t gDisconnectEvents = 0;

void startConnect() {
  if (gSsid.length() == 0) {
    return;
  }
  gLastAttemptMs = millis();
  WiFi.begin(gSsid.c_str(), gPass.c_str());
}

void onWiFiEvent(WiFiEvent_t event) {
  switch (event) {
    case ARDUINO_EVENT_WIFI_STA_GOT_IP:
      gConnected = true;
      gReconnectSuccess++;
      gRetryDelayMs = WIFI_RETRY_MIN_MS;
      Serial.print("[WIFI] Connected, IP: ");
      Serial.println(WiFi.localIP());
      break;
    case ARDUINO_EVENT_WIFI_STA_DISCONNECTED:
      gDisconnectEvents++;
      if (gConnected) {
        Serial.println("[WIFI] Disconnected, waiting for reconnect...");
      }
      gConnected = false;
      break;
    default:
      break;
  }
}

bool tryLock(SemaphoreHandle_t mutex) {
  if (mutex == nullptr) {
    return false;
  }
  return xSemaphoreTake(mutex, pdMS_TO_TICKS(5)) == pdTRUE;
}

}

namespace WiFiWebManager {

void begin(const char *ssid, const char *pass) {
  gSsid = ssid == nullptr ? "" : String(ssid);
  gPass = pass == nullptr ? "" : String(pass);

  WiFi.mode(WIFI_STA);
  WiFi.persistent(false);
  WiFi.setAutoReconnect(true);
  WiFi.onEvent(onWiFiEvent);

  Serial.println("[WIFI] Connecting in background...");
  startConnect();

  if (WiFi.status() == WL_CONNECTED) {
    gConnected = true;
    Serial.print("[WIFI] Boot connected, IP: ");
    Serial.println(WiFi.localIP());
  } else {
    Serial.println("[WIFI] Continue in offline mode until STA gets IP.");
  }
}

void serviceReconnect() {
  if (WiFi.status() == WL_CONNECTED) {
    gConnected = true;
    return;
  }
  gConnected = false;
  unsigned long now = millis();
  if (now - gLastAttemptMs < gRetryDelayMs) {
    return;
  }
  Serial.printf("[WIFI] Reconnect attempt (backoff=%lums)\n", gRetryDelayMs);
  gReconnectAttempts++;
  startConnect();
  gRetryDelayMs = min(gRetryDelayMs * 2UL, WIFI_RETRY_MAX_MS);
}

bool isConnected() {
  return gConnected && WiFi.status() == WL_CONNECTED;
}

void registerRoutes(
  AsyncWebServer &server,
  SystemSettings &settings,
  SemaphoreHandle_t stateMutex,
  RuntimeSharedData &runtimeData,
  TelemetrySharedData &telemetryData,
  OledRotateHandler oledRotateHandler) {

  gEvents.onConnect([](AsyncEventSourceClient *client) {
    client->send("connected", "info", millis());
  });
  server.addHandler(&gEvents);

  server.on("/", HTTP_GET, [&settings, stateMutex](AsyncWebServerRequest *request) {
    SystemSettings snapshot = settings;
    if (tryLock(stateMutex)) {
      snapshot = settings;
      xSemaphoreGive(stateMutex);
    }

    String ip = WiFi.localIP().toString();
    String html;
    html.reserve(9000);

    html += R"RAW(<!DOCTYPE html>
<html><head><meta charset="UTF-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>VFD Parameter Tuner</title><style>
*{box-sizing:border-box;margin:0;padding:0}
body{background:#000;color:#0ff;font-family:Consolas,monospace}
.hdr{display:flex;justify-content:space-between;align-items:center;padding:6px 12px;border-bottom:2px solid #fff}
.disp{display:grid;grid-template-columns:1fr 1fr 1fr;text-align:center;padding:10px 0 5px}
.dlbl{color:#fff;font-size:12px;margin-bottom:2px}
.dval{font-size:18px;font-weight:bold}
.dunit{color:#888;font-size:11px;min-height:14px}
.dsec{text-align:center;padding:3px 0}
.fqlb{color:#fff;font-size:12px}
.prm{background:#111;padding:6px 0}
.prow{display:flex;align-items:center;padding:2px 10px;gap:5px}
.plbl{color:#fff;min-width:72px;font-size:13px}
input[type=text]{background:#111;border:1px solid #0ff;color:#fff;width:88px;text-align:center;padding:3px;font-family:Consolas,monospace;font-size:13px}
.pdsc{color:#888;font-size:12px}
.arow{display:flex;justify-content:center;gap:8px;padding:5px}
button{padding:5px 14px;font-weight:bold;border:none;cursor:pointer;color:#fff;font-family:Consolas,monospace;font-size:13px}
#btn_apply{background:green}
#btn_apply:disabled{background:#333;cursor:default;color:#888}
#btn_apply:not(:disabled):hover{background:#0a0}
#btn_reset{background:#8b0000}
#btn_reset:disabled{background:#333;cursor:default;color:#888}
#btn_reset:not(:disabled):hover{background:#b22}
.boled{background:#006080}
.boled:hover{background:#008cad}
#xlog{background:#000;color:#fff;font-size:11px;padding:4px;height:108px;overflow-y:auto;border-top:1px solid #444;white-space:pre-wrap;word-break:break-all}
.ftr{text-align:right;color:#666;font-size:10px;padding:2px 10px 4px}
</style></head><body>
<div class="hdr">
  <div>MODE:&nbsp;<span id="xmode" style="color:#0ff">--</span></div>
  <span id="xconn" style="color:orange;font-size:12px">)RAW";
    html += ip;
    html += R"RAW(</span>
</div>
<div class="disp">
  <div><div class="dlbl">Distance</div><div class="dval" id="xdist" style="color:#0ff">--</div><div class="dunit">cm</div></div>
  <div><div class="dlbl">Boost Lv</div><div class="dval" id="xblv" style="color:#ff0">--</div><div class="dunit">&nbsp;</div></div>
  <div><div class="dlbl">Boost Hold</div><div class="dval" id="xbhd" style="color:#fc6">--</div><div class="dunit">s</div></div>
</div>
<div class="dsec"><div class="fqlb">Target Frequency</div>
  <div id="xfreq" style="color:lime;font-size:18px;font-weight:bold">-- Hz</div></div>
<div class="dsec"><div class="fqlb">VFD</div>
  <div id="xvfd" style="color:orange;font-size:15px">-- Hz</div></div>
<div id="xsbar" style="text-align:center;padding:3px;font-size:12px;color:#f66">Disconnected</div>
<div class="prm"><form id="pform">
<div class="prow"><span class="plbl">UDMIN</span><input type="text" id="i_udmin" name="udmin" value=")RAW";
    html += String(snapshot.udmin, 2);
    html += R"RAW("><span class="pdsc">Khoang cach day (cm)</span></div>
<div class="prow"><span class="plbl">UDMAX</span><input type="text" id="i_udmax" name="udmax" value=")RAW";
    html += String(snapshot.udmax, 2);
    html += R"RAW("><span class="pdsc">Khoang cach rong (cm)</span></div>
<div class="prow"><span class="plbl">UFMIN</span><input type="text" id="i_ufmin" name="ufmin" value=")RAW";
    html += String(snapshot.ufmin, 2);
    html += R"RAW("><span class="pdsc">Tan so min (Hz)</span></div>
<div class="prow"><span class="plbl">UFMAX</span><input type="text" id="i_ufmax" name="ufmax" value=")RAW";
    html += String(snapshot.ufmax, 2);
    html += R"RAW("><span class="pdsc">Tan so max (Hz)</span></div>
<div class="prow"><span class="plbl">BOOST</span><input type="text" id="i_boot" name="bootfactor" value=")RAW";
    html += String(snapshot.boost_level1_pct, 2);
    html += R"RAW("><span class="pdsc">Phan tram tang toc cap 1 (%)</span></div>
<div class="prow"><span class="plbl">BOOST T</span><input type="text" id="i_boosttime" name="boost_time" value=")RAW";
    html += String(snapshot.boost_level1_hold, 2);
    html += R"RAW("><span class="pdsc">Thoi gian boost cap 1 (s)</span></div>
<div class="prow"><span class="plbl">BOOST L2</span><input type="text" id="i_boost_l2" name="boost_l2" value=")RAW";
    html += String(snapshot.boost_level2_pct, 2);
    html += R"RAW("><span class="pdsc">% tang toc cap 2</span></div>
<div class="prow"><span class="plbl">BOOST L3</span><input type="text" id="i_boost_l3" name="boost_l3" value=")RAW";
    html += String(snapshot.boost_level3_pct, 2);
    html += R"RAW("><span class="pdsc">% tang toc cap 3</span></div>
<div class="prow"><span class="plbl">HOLD L2</span><input type="text" id="i_boost_h2" name="boost_h2" value=")RAW";
    html += String(snapshot.boost_level2_hold, 2);
    html += R"RAW("><span class="pdsc">Thoi gian giu cap 2 (s)</span></div>
<div class="prow"><span class="plbl">HOLD L3</span><input type="text" id="i_boost_h3" name="boost_h3" value=")RAW";
    html += String(snapshot.boost_level3_hold, 2);
    html += R"RAW("><span class="pdsc">Thoi gian giu cap 3 (s)</span></div>
<div class="prow"><span class="plbl">ESC L2</span><input type="text" id="i_boost_e2" name="boost_e2" value=")RAW";
    html += String(snapshot.boost_escalate_2, 2);
    html += R"RAW("><span class="pdsc">Thoi gian kich lien tuc len cap 2 (s)</span></div>
<div class="prow"><span class="plbl">ESC L3</span><input type="text" id="i_boost_e3" name="boost_e3" value=")RAW";
    html += String(snapshot.boost_escalate_3, 2);
    html += R"RAW("><span class="pdsc">Thoi gian kich lien tuc len cap 3 (s)</span></div>
<div class="prow"><span class="plbl">DECAY</span><input type="text" id="i_boost_decay" name="boost_decay" value=")RAW";
    html += String(snapshot.boost_decay_time, 2);
    html += R"RAW("><span class="pdsc">On dinh de giam 1 cap (s)</span></div>
<div class="arow">
  <button type="submit" id="btn_apply" disabled>Apply</button>
  <button type="button" id="btn_reset" disabled onclick="doReset()">RESET ESP</button>
</div></form>
<div class="arow">
  <button class="boled" onclick="doOled()">ROTATE OLED</button>
</div></div>
<div id="xlog"></div>
<div class="ftr">Developed by Hung - Staff FA</div>
<script>
const SC={DISCONNECTED:"#ff6666",CONNECTING:"#ffaa00",READY:"#00cc66",APPLYING:"#ffff00",RESTARTING:"#ff9900",ERROR:"#ff4444"};
const SM={DISCONNECTED:"Disconnected",CONNECTING:"Connecting...",READY:"Ready",APPLYING:"Applying...",RESTARTING:"Restarting...",ERROR:"Error - check connection"};
let appState="DISCONNECTED",toId=null,lastTs=0;
const LOG_MAX=300;
function setState(s,msg){
  appState=s;
  const el=document.getElementById("xsbar");
  el.style.color=SC[s]||"#ffcc00";
  el.textContent=msg||SM[s]||s;
  updateBtns();
}
function schedTO(s,ms){
  clearTimeout(toId);
  toId=setTimeout(()=>{if(appState===s){appendLog("[SYS] Timeout: no response (state="+s+")");setState("ERROR");}},ms);
}
function cancelTO(){clearTimeout(toId);toId=null;}
function sf(id){const v=parseFloat(document.getElementById(id).value);return[v,!isNaN(v)];}
function setIC(id,ok){
  const e=document.getElementById(id);
  e.style.background=ok?"#111111":"#330000";
  e.style.color=ok?"#fff":"#ff8888";
  e.style.borderColor=ok?"#00ffff":"#ff4444";
}
function validate(){
  const[udmin,ok1]=sf("i_udmin");
  const[udmax,ok2]=sf("i_udmax");
  const[ufmin,ok3]=sf("i_ufmin");
  const[ufmax,ok4]=sf("i_ufmax");
  const[l1,ok5]=sf("i_boot");
  const[h1,ok6]=sf("i_boosttime");
  const[l2,ok7]=sf("i_boost_l2");
  const[l3,ok8]=sf("i_boost_l3");
  const[h2,ok9]=sf("i_boost_h2");
  const[h3,ok10]=sf("i_boost_h3");
  const[e2,ok11]=sf("i_boost_e2");
  const[e3,ok12]=sf("i_boost_e3");
  const[dc,ok13]=sf("i_boost_decay");
  const v={
    i_udmin:ok1,
    i_udmax:ok2&&udmax>udmin,
    i_ufmin:ok3,
    i_ufmax:ok4&&ufmax>ufmin,
    i_boot:ok5&&l1>=0,
    i_boosttime:ok6&&h1>=0,
    i_boost_l2:ok7&&l2>=l1,
    i_boost_l3:ok8&&l3>=l2,
    i_boost_h2:ok9&&h2>=0,
    i_boost_h3:ok10&&h3>=0,
    i_boost_e2:ok11,
    i_boost_e3:ok12&&e3>e2,
    i_boost_decay:ok13&&dc>=0
  };
  for(const[id,ok]of Object.entries(v))setIC(id,ok);
  return Object.values(v).every(Boolean);
}
function updateBtns(){
  const ok=validate();
  document.getElementById("btn_apply").disabled=!(appState==="READY"&&ok);
  document.getElementById("btn_reset").disabled=!(appState==="READY"||appState==="APPLYING");
}
function appendLog(text){
  const el=document.getElementById("xlog");
  el.textContent+=text+"\n";
  const lines=el.textContent.split("\n");
  if(lines.length>LOG_MAX+5)el.textContent=lines.slice(lines.length-LOG_MAX).join("\n");
  el.scrollTop=el.scrollHeight;
}
let es=null;
function connectSSE(){
  setState("CONNECTING");
  es=new EventSource("/events");
  es.addEventListener("data",(e)=>{
    try{
      const d=JSON.parse(e.data);
      lastTs=Date.now();
      if(appState==="CONNECTING"||appState==="DISCONNECTED"||appState==="ERROR"||appState==="RESTARTING"){
        cancelTO();
        setState("READY");
      }
      document.getElementById("xdist").textContent=d.dist.toFixed(1);
      document.getElementById("xblv").textContent=d.boost_level>0?"L"+d.boost_level:"--";
      document.getElementById("xbhd").textContent=d.boost?d.boost_remain.toFixed(1):"--";
      document.getElementById("xfreq").textContent=d.freq.toFixed(1)+" Hz";
      document.getElementById("xmode").textContent=d.mode;
      if(d.vfd_freq>=0)document.getElementById("xvfd").textContent=d.vfd_freq.toFixed(1)+" Hz ("+(d.vfd_ok?"OK":"FAIL")+")";
      if(d.mode==="RUN")appendLog("[RUN] D:"+d.dist.toFixed(1)+" | F:"+d.freq.toFixed(1)+"Hz | Boost:L"+d.boost_level+" | Hold:"+d.boost_remain.toFixed(1)+"s");
      else if(d.mode==="TEST")appendLog("[PING] D: "+d.dist.toFixed(1)+" cm");
    }catch(err){}
  });
  es.onerror=()=>{
    if(appState!=="RESTARTING")setState("DISCONNECTED");
    es.close();
    setTimeout(connectSSE,2000);
  };
}
document.getElementById("pform").addEventListener("submit",async(e)=>{
  e.preventDefault();
  if(!validate())return;
  setState("APPLYING");
  schedTO("APPLYING",5000);
  const fd=new FormData(e.target);
  try{
    const r=await fetch("/update",{method:"POST",body:fd});
    const j=await r.json();
    cancelTO();
    if(r.ok&&j.ok){
      setState("READY","Applied OK");
      appendLog("[SYS] Settings applied OK");
      setTimeout(()=>{if(appState==="READY")setState("READY");},2000);
    }else{
      setState("ERROR","Apply failed: "+(j.error||"unknown"));
      appendLog("[SYS] Apply failed: "+(j.error||"unknown"));
    }
  }catch(err){
    cancelTO();
    setState("ERROR","Network error");
    appendLog("[SYS] Network error during apply");
  }
});
function doReset(){
  if(!confirm("Reset ESP32 ngay bay gio?"))return;
  appendLog("[SYS] Sent RESET command");
  setState("RESTARTING");
  schedTO("RESTARTING",10000);
  fetch("/reset",{method:"POST"}).catch(()=>{});
}
function doOled(){
  fetch("/oled/toggle",{method:"POST"}).then(r=>{if(r.ok)appendLog("[SYS] OLED rotated");}).catch(()=>{});
}
document.querySelectorAll("input[type=text]").forEach(el=>{
  el.addEventListener("input",updateBtns);
  el.addEventListener("blur",updateBtns);
});
setInterval(()=>{if(appState==="READY"&&Date.now()-lastTs>5000){setState("ERROR","Timeout - no data");}},1000);
connectSSE();
</script></body></html>
)RAW";

    request->send(200, "text/html", html);
  });

  server.on("/data", HTTP_GET, [stateMutex, &runtimeData](AsyncWebServerRequest *request) {
    if (WiFi.status() != WL_CONNECTED) {
      request->send(503, "application/json", "{\"ok\":false,\"error\":\"wifi_disconnected\"}");
      return;
    }
    RuntimeSharedData snapshot;
    if (tryLock(stateMutex)) {
      snapshot = runtimeData;
      xSemaphoreGive(stateMutex);
    } else {
      snapshot = runtimeData;
    }
    char json[180];
    snprintf(json, sizeof(json),
             "{\"dist\":%.2f,\"freq\":%.2f,\"boost\":%s,\"boost_level\":%u,\"boost_remain\":%.1f,\"vfd_freq\":%.2f,\"vfd_ok\":%s}",
             snapshot.dist,
             snapshot.freq,
             snapshot.boost ? "true" : "false",
             (unsigned)snapshot.boostLevel,
             snapshot.boostRemain,
             snapshot.vfdFreq,
             snapshot.vfdOk ? "true" : "false");
    request->send(200, "application/json", json);
  });

  server.on("/health", HTTP_GET, [](AsyncWebServerRequest *request) {
    if (WiFi.status() == WL_CONNECTED) {
      request->send(200, "application/json", "{\"ok\":true,\"wifi\":\"connected\"}");
      return;
    }
    request->send(503, "application/json", "{\"ok\":false,\"wifi\":\"disconnected\"}");
  });

  server.on("/telemetry", HTTP_GET, [stateMutex, &telemetryData](AsyncWebServerRequest *request) {
    TelemetrySharedData snapshot = {0, 0, 0, 0, 0, 0, 0, 0};
    if (tryLock(stateMutex)) {
      snapshot = telemetryData;
      xSemaphoreGive(stateMutex);
    }
    char json[320];
    snprintf(json, sizeof(json),
             "{\"uptime_sec\":%lu,\"free_heap\":%lu,\"min_free_heap\":%lu,\"loop_ticks\":%lu,\"modbus_ok\":%lu,\"modbus_fail\":%lu,\"nettask_create_fail\":%lu,\"lock_timeouts\":%lu,\"wifi_reconnect_attempts\":%lu,\"wifi_reconnect_success\":%lu,\"wifi_disconnect_events\":%lu}",
             (unsigned long)snapshot.uptimeSec,
             (unsigned long)snapshot.freeHeap,
             (unsigned long)snapshot.minFreeHeap,
             (unsigned long)snapshot.loopTicks,
             (unsigned long)snapshot.modbusOk,
             (unsigned long)snapshot.modbusFail,
             (unsigned long)snapshot.netTaskCreateFail,
             (unsigned long)snapshot.lockTimeouts,
             (unsigned long)gReconnectAttempts,
             (unsigned long)gReconnectSuccess,
             (unsigned long)gDisconnectEvents);
    request->send(200, "application/json", json);
  });

  server.on("/update", HTTP_POST, [stateMutex, &settings](AsyncWebServerRequest *request) {
    SystemSettings next;
    if (tryLock(stateMutex)) {
      next = settings;
      xSemaphoreGive(stateMutex);
    } else {
      next = settings;
    }

    if (request->hasParam("udmin", true))      next.udmin               = request->getParam("udmin",       true)->value().toFloat();
    if (request->hasParam("udmax", true))      next.udmax               = request->getParam("udmax",       true)->value().toFloat();
    if (request->hasParam("ufmin", true))      next.ufmin               = request->getParam("ufmin",       true)->value().toFloat();
    if (request->hasParam("ufmax", true))      next.ufmax               = request->getParam("ufmax",       true)->value().toFloat();
    if (request->hasParam("bootfactor", true)) next.bootfactor          = request->getParam("bootfactor",  true)->value().toFloat();
    if (request->hasParam("boost_time", true)) next.boost_time          = request->getParam("boost_time",  true)->value().toFloat();
    if (request->hasParam("boost_l2", true))   next.boost_level2_pct   = request->getParam("boost_l2",    true)->value().toFloat();
    if (request->hasParam("boost_l3", true))   next.boost_level3_pct   = request->getParam("boost_l3",    true)->value().toFloat();
    if (request->hasParam("boost_h2", true))   next.boost_level2_hold  = request->getParam("boost_h2",    true)->value().toFloat();
    if (request->hasParam("boost_h3", true))   next.boost_level3_hold  = request->getParam("boost_h3",    true)->value().toFloat();
    if (request->hasParam("boost_e2", true))   next.boost_escalate_2   = request->getParam("boost_e2",    true)->value().toFloat();
    if (request->hasParam("boost_e3", true))   next.boost_escalate_3   = request->getParam("boost_e3",    true)->value().toFloat();
    if (request->hasParam("boost_decay", true))next.boost_decay_time   = request->getParam("boost_decay", true)->value().toFloat();

    next.boost_level1_pct  = next.bootfactor;
    next.boost_level1_hold = next.boost_time;
    sanitizeSettings(next);

    if (next.udmax <= next.udmin
        || next.ufmax <= next.ufmin
        || next.boost_level1_pct < 0.0f
        || next.boost_level2_pct < next.boost_level1_pct
        || next.boost_level3_pct < next.boost_level2_pct
        || next.boost_level1_hold < 0.0f
        || next.boost_level2_hold < 0.0f
        || next.boost_level3_hold < 0.0f
        || next.boost_escalate_3 <= next.boost_escalate_2) {
      request->send(400, "application/json", "{\"ok\":false,\"error\":\"invalid_settings\"}");
      return;
    }

    saveSettings(next);

    if (tryLock(stateMutex)) {
      settings = next;
      xSemaphoreGive(stateMutex);
    } else {
      settings = next;
    }

    request->send(200, "application/json", "{\"ok\":true}");
  });

  server.on("/oled/toggle", HTTP_POST, [oledRotateHandler](AsyncWebServerRequest *request) {
    static bool rotate180 = true;
    rotate180 = !rotate180;
    if (oledRotateHandler != nullptr) {
      oledRotateHandler(rotate180);
    }
    request->send(200, "application/json", "{\"ok\":true}");
  });

  server.on("/reset", HTTP_POST, [](AsyncWebServerRequest *request) {
    request->send(200, "text/plain", "ESP32 is restarting...");
    scheduleRestartEsp();
  });
}

void pushEvent(const RuntimeSharedData &runtime, const char *mode) {
  if (gEvents.count() == 0) {
    return;
  }
  char buf[220];
  snprintf(buf, sizeof(buf),
           "{\"dist\":%.2f,\"freq\":%.2f,\"boost\":%s,\"boost_level\":%u,\"boost_remain\":%.1f,\"vfd_freq\":%.2f,\"vfd_ok\":%s,\"mode\":\"%s\"}",
           runtime.dist,
           runtime.freq,
           runtime.boost ? "true" : "false",
           (unsigned)runtime.boostLevel,
           runtime.boostRemain,
           runtime.vfdFreq,
           runtime.vfdOk ? "true" : "false",
           mode);
  gEvents.send(buf, "data", millis());
}

}
