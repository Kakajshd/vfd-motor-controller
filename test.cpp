#include "Config.h"
#include "Settings.h"
#include "VFDManager.h"
#include <WiFi.h>
#include <ESPAsyncWebServer.h>
#include <AsyncTCP.h>
SystemSettings settings;
VFDManager unwindVFD;
AsyncWebServer server(80);

#ifdef OLED_EN
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SH110X.h>
Adafruit_SH1106G display = Adafruit_SH1106G(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);
#endif

//----------------------------------------
#include <NewPing.h>
NewPing sonar[SONAR_NUM] = {                    // Sensor object array.
    NewPing(TRIG1_PIN, ECHO1_PIN, MAX_DISTANCE) // Each sensor's trigger pin, echo pin, and max distance to ping.
#if defined(TRIG2_PIN) && defined(ECHO2_PIN)
    ,
    NewPing(TRIG2_PIN, ECHO2_PIN, MAX_DISTANCE)
#endif
};

float getDistance()
{
    float total_distance;
    for (uint8_t i = 0; i < SONAR_NUM; i++)
    { // Loop through each sensor and display results.
        if (i > 0)
            delay(50); // Wait 50ms between pings (about 20 pings/sec). 29ms should be the shortest delay between pings.
        float distance = sonar[i].ping_cm();
        total_distance += distance;
        // Serial.printf("%d = %dcm\n", i, distance);
    }
    return total_distance / SONAR_NUM;
}
//----------------------------------------
enum SystemMode
{
    MODE_RUN,
    MODE_PING,
    MODE_MANUAL
};
SystemMode currentMode = MODE_RUN;

float manualOffset = 0;
float boot_factor = 1.0;
unsigned long lastTime = 0;
// Thêm biến toàn cục ở đầu file
bool isNetTaskRunning = false;
// Biến lưu trữ tần số thực tế đã gửi thành công (để hiển thị)
float last_uFreq = 0.0;
float last_dist = 0.0;
// Biến cho boost timer
unsigned long boost_start_time = 0;
bool is_boosting = false;
float boost_remain = 0.0;
bool lastSensorB = true; // false if sensor active, true if idle (normally open)
//---------------------------------------------
float getFreq(float dist)
{
    float freq = settings.ufmin + (constrain(dist, settings.udmin, settings.udmax) - settings.udmin) * (settings.ufmax - settings.ufmin) / (settings.udmax - settings.udmin);
    // Serial.printf("Distance(cm): %0.2f, Frequency Hz): %0.2f", dist, freq);
    return freq;
}
//----------------------------------------------
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
// #include <freertos/queue.h>
// #include <freertos/semphr.h>   // BẮT BUỘC có cái này để dùng Mutex (safeDraw)
#include <functional> // Cần thư viện này để dùng std::function

// Gói dữ liệu "vạn năng"
struct NetJob
{
    float freq;
    // Đây là "mệnh lệnh" sẽ thực hiện sau khi có kết quả
    std::function<void(bool)> callback;
};

// Hàm tĩnh bao bọc để gọi member function
void backgroundNetTask(void *pvParameters)
{
    // Lấy gói công việc từ tham số truyền vào
    NetJob *job = (NetJob *)pvParameters;

    // Trước khi làm việc nặng, nghỉ 1 chút để IDLE Task chạy
    vTaskDelay(pdMS_TO_TICKS(10));

    // 1. Giao tiếp VFD (Chạy trên Core 0)
    // Lấy đối tượng WiFiManager thông qua hàm static chúng ta vừa tạo
    //   VFDManager *vfd = VFDManager::getInstance();
    //   bool result = vfd->setFrequency(job->freq);
    bool result = unwindVFD.setFrequency(job->freq);
    if (result)
        Serial.printf("Set frequency %0.1f OK\n", job->freq);
    else
        Serial.printf("Set frequency %0.1f NG\n", job->freq);

    // 2. Thực hiện lệnh "callback" (xử lý kết quả)
    if (job->callback)
    {
        job->callback(result);
    }

    // Giải phóng bộ nhớ của struct NetJob sau khi xong việc
    delete job;

    // Đánh dấu đã xong để loop() có thể tạo task tiếp theo
    isNetTaskRunning = false;
    // Tự hủy task để giải phóng RAM cho ESP32

    vTaskDelete(NULL);
}

// --- XỬ LÝ LỆNH SERIAL ---
void processCommand(String input)
{
    input.trim();
    String cmd = input;
    cmd.toUpperCase();
    cmd.replace(" ", "");

    if (cmd == "RESET")
    {
        ESP.restart();
    }
    else if (cmd == "INFO")
    {
        Serial.println("---[ CURRENT SETTINGS ]---");
        Serial.printf("UEN (User En)     : %s\n", settings.uen ? "ON" : "OFF");
        Serial.printf("PEN (Pump En)     : %s\n", settings.pen ? "ON" : "OFF");
        Serial.printf("UD (Min/Max)      : %.2f / %.2f\n", settings.udmin, settings.udmax);
        Serial.printf("UF (Min/Max)      : %.2f / %.2f\n", settings.ufmin, settings.ufmax);
        Serial.printf("PF (Min/Max)      : %.2f / %.2f\n", settings.pfmin, settings.pfmax);
        Serial.printf("Boot Factor       : %.2f\n", settings.bootfactor);
        Serial.printf("Boost Time        : %.2f s\n", settings.boost_time);
        Serial.println("-------------------------");
    }
    else if (cmd == "RUN")
    {
        currentMode = MODE_RUN;
        manualOffset = 0;
        unwindVFD.sendControl(0x0012);
        Serial.println("-> Chế độ TỰ ĐỘNG");
    }
    else if (cmd == "PING")
    {
        currentMode = MODE_PING;
        Serial.println("-> Chế độ THỬ CẢM BIẾN");
    }
    else if (cmd == "STOP")
    {
        currentMode = MODE_MANUAL;
        unwindVFD.sendControl(0x0001);
        // pullVFD.sendControl(0x0001);
        Serial.println("-> DỪNG KHẨN");
    }
    else if (cmd == "SAVE")
        saveSettings(settings);

    // Tăng giảm tốc nhanh
    else if (cmd == "+")
        manualOffset += 1.0;
    else if (cmd == "++")
        manualOffset += 5.0;
    else if (cmd == "+++")
        manualOffset += 10.0;
    else if (cmd == "-")
        manualOffset -= 1.0;
    else if (cmd == "--")
        manualOffset -= 5.0;
    else if (cmd == "---")
        manualOffset -= 10.0;

    // Cài đặt tham số ví dụ: uen=0 hoặc DIST_MIN=15
    else if (cmd.indexOf('=') > 0)
    {
        String key = cmd.substring(0, cmd.indexOf('='));
        float val = cmd.substring(cmd.indexOf('=') + 1).toFloat();

        if (key == "UEN")
            settings.uen = (val > 0);
        else if (key == "PEN")
            settings.pen = (val > 0);
        else if (key == "UDMIN")
            settings.udmin = val;
        else if (key == "UDMAX")
            settings.udmax = val;
        else if (key == "UFMIN")
            settings.ufmin = val;
        else if (key == "UFMAX")
            settings.ufmax = val;
        else if (key == "PFMIN")
            settings.pfmin = val;
        else if (key == "PFMAX")
            settings.pfmax = val;
        else if (key == "BOOTFACTOR")
            settings.bootfactor = val;
        else if (key == "BOOSTTIME")
            settings.boost_time = val;
        Serial.printf("-> Đã cập nhật %s = %.1f (Gõ SAVE để lưu)\n", key.c_str(), val);
    }
}

void updateDisplay(float dist, float targetFreq, bool sB, float lastFreq, bool booting)
{
#ifdef OLED_EN
    display.clearDisplay();
    display.setTextColor(SH110X_WHITE);

    // --- TIÊU ĐỀ (HEADER) ---
    display.setTextSize(1);
    display.setCursor(0, 0);
    if (currentMode == MODE_RUN)
        display.print("MODE: AUTO");
    else if (currentMode == MODE_PING)
        display.print("MODE: TEST");
    else
        display.print("MODE: STOP");

    // Icon truyền tin (nhấp nháy khi đang gửi VFD)
    if (isNetTaskRunning)
    {
        display.setCursor(110, 0);
        display.print(">>");
    }
    display.drawLine(0, 10, 128, 10, SH110X_WHITE);

    // --- KHOẢNG CÁCH (DISTANCE) ---
    display.setCursor(0, 15);
    display.print("Distance:");
    display.setTextSize(2);
    display.setCursor(0, 25);
    if (dist > 0)
        display.print(dist, 1);
    else
        display.print("-");
    display.setTextSize(1);
    display.print(" cm");

    display.setCursor(64, 15);
    display.print("Boosting:");
    display.setTextSize(2);
    display.setCursor(64, 25);
    display.print(booting ? "ON" : "OFF");

    // --- TẦN SỐ TÍNH TOÁN (TARGET FREQ) ---
    display.setTextSize(1);
    display.setCursor(0, 45);
    display.print("Target F:");
    display.print(targetFreq, 1);
    display.print(" Hz");

    // --- TRẠNG THÁI SENSOR & VFD THỰC TẾ ---
    display.drawLine(0, 54, 128, 54, SH110X_WHITE);
    display.setCursor(0, 57);
    display.print("B:");
    display.print(sB ? "ON" : "OFF");

    display.setCursor(65, 57);
    display.print("VFD:");
    display.print(lastFreq, 1);
    display.print("Hz");

    display.display();
#endif
}
//---------------------------------------------

void setup()
{
    pinMode(PULL_SENS_B, INPUT_PULLUP);

    Serial.begin(115200);
    Serial2.begin(VFD_BAUD, VFD_PROTOCOL, RX2_PIN, TX2_PIN);

#ifdef OLED_EN
    // Khởi tạo I2C với chân 21, 22
    Wire.begin(I2C_SDA, I2C_SCL);
    // Khởi tạo màn hình
    if (!display.begin(I2C_ADDRESS, true))
    {
        Serial.println(F("SH1106 allocation failed"));
        // Có thể thêm vòng lặp vô hạn ở đây nếu muốn dừng hệ thống khi lỗi màn hình
    }
    display.clearDisplay();
    display.display();
#endif

    // WiFi setup
    WiFi.begin(WIFI_SSID, WIFI_PASS);
    Serial.print("Connecting to WiFi...");
    int attempts = 0;
    while (WiFi.status() != WL_CONNECTED && attempts < 20)
    {
        delay(500);
        Serial.print(".");
        attempts++;
    }
    if (WiFi.status() == WL_CONNECTED)
    {
        Serial.println("\nConnected to WiFi!");
        Serial.print("IP Address: ");
        Serial.println(WiFi.localIP());
    }
    else
    {
        Serial.println("\nFailed to connect to WiFi!");
    }

    loadSettings(settings);

    unwindVFD.begin(UNWIND_VFD_ID, Serial2); // Khởi tạo Modbus cho Serial2
    unwindVFD.sendControl(0x0012);

    //==================================== Webserver routes============================

    server.on("/", HTTP_GET, [](AsyncWebServerRequest *request)
              {
    String html = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>VFD HMI</title>

<style>
body {
    background: #000;
    color: #00ffff;
    font-family: Consolas, monospace;
    margin: 0;
}

.header {
    display: flex;
    justify-content: space-between;
    padding: 8px 12px;
    border-bottom: 2px solid #00ffff;
}

.mode { color: #00ff00; }
.status { color: red; }

.main {
    text-align: center;
    padding: 20px 10px;
}

.big {
    font-size: 40px;
    font-weight: bold;
}

.mid { font-size: 20px; }

.green { color: #00ff00; }
.cyan { color: #00ffff; }
.orange { color: #ffcc00; }

#boost_led {
    font-size: 18px;
    margin-right: 5px;
}

.boost-on { color: #00ff00; }
.boost-off { color: #ffcc00; }

#boost_time {
    margin-left: 8px;
    font-size: 14px;
    color: #888;
}

.panel {
    border-top: 2px solid #00ffff;
    margin-top: 10px;
    padding: 10px;
}

.grid {
    display: grid;
    grid-template-columns: 100px 80px 1fr;
    gap: 6px;
    align-items: center;
    font-size: 14px;
}

input {
    background: #111;
    border: 1px solid #00ffff;
    color: #fff;
    text-align: center;
    padding: 4px;
}

.desc { color: #888; }

button {
    margin-top: 10px;
    padding: 6px 15px;
    background: #008000;
    border: none;
    color: #fff;
    font-weight: bold;
}

.footer {
    border-top: 1px solid #00ffff;
    margin-top: 10px;
    padding: 5px;
    font-size: 12px;
    color: #666;
    text-align: center;
}
</style>
</head>

<body>

<div class="header">
    <div>MODE: <span class="mode">AUTO</span></div>
    <div class="status">&#9679; CONNECTED</div>
</div>

<div class="main">
    <div>Distance</div>
    <div class="big cyan" id="dist">--</div>
    <div class="mid">cm</div>

    <div style="margin-top:10px;">
        BOOST:
        <span id="boost_led">●</span>
        <span id="boost_text" class="boost-off">OFF</span>
        <span id="boost_time">(0.0s)</span>
    </div>

    <div style="margin-top:20px;">Target Frequency</div>
    <div class="big green" id="freq">--</div>
    <div class="mid">Hz</div>

    <div style="margin-top:15px;">
        VFD: <span class="orange" id="vfd">--</span>
    </div>
</div>

<div class="panel">
    <form action="/update" method="POST">
        <div class="grid">
            <div>UDMIN</div>
            <input name="udmin" value=")rawliteral" + String(settings.udmin) + R"rawliteral(">
            <div class="desc">Khoảng cách đầy (cm)</div>

            <div>UDMAX</div>
            <input name="udmax" value=")rawliteral" + String(settings.udmax) + R"rawliteral(">
            <div class="desc">Khoảng cách rỗng (cm)</div>

            <div>UFMIN</div>
            <input name="ufmin" value=")rawliteral" + String(settings.ufmin) + R"rawliteral(">
            <div class="desc">Tần số min (Hz)</div>

            <div>UFMAX</div>
            <input name="ufmax" value=")rawliteral" + String(settings.ufmax) + R"rawliteral(">
            <div class="desc">Tần số max (Hz)</div>

            <div>BOOST</div>
            <input name="bootfactor" value=")rawliteral" + String(settings.bootfactor) + R"rawliteral(">
            <div class="desc">% tăng tốc</div>

            <div>BOOST T</div>
            <input name="boost_time" value=")rawliteral" + String(settings.boost_time) + R"rawliteral(">
            <div class="desc">Thời gian (s)</div>
        </div>

        <center><button type="submit">APPLY</button></center>
    </form>
</div>

<div class="footer">
    ESP32 VFD Controller
</div>

<script>
setInterval(() => {
    fetch('/data')
    .then(r => r.json())
    .then(d => {

        document.getElementById('dist').textContent = d.dist.toFixed(1);
        document.getElementById('freq').textContent = d.freq.toFixed(1);
        document.getElementById('vfd').textContent = d.freq.toFixed(1) + " Hz";

        let led = document.getElementById('boost_led');
        let text = document.getElementById('boost_text');
        let time = document.getElementById('boost_time');

        if (d.boost) {
            text.textContent = "ON";
            led.className = "boost-on";
            text.className = "boost-on";

            time.textContent = "(" + d.boost_remain.toFixed(1) + "s)";
            time.style.color = "#00ff00";
        } else {
            text.textContent = "OFF";
            led.className = "boost-off";
            text.className = "boost-off";

            time.textContent = "(0.0s)";
            time.style.color = "#888";
        }

    });
}, 300);
</script>

</body>
</html>
)rawliteral";
    request->send(200, "text/html", html); });

    server.on("/data", HTTP_GET, [](AsyncWebServerRequest *request)
              {
    String json = "{";
json += "\"dist\":" + String(last_dist) + ",";
json += "\"freq\":" + String(last_uFreq) + ",";
json += "\"boost\":" + String(is_boosting ? "true" : "false") + ",";
json += "\"boost_remain\":" + String(boost_remain,1);
json += "}";
    request->send(200, "application/json", json); });

    server.on("/update", HTTP_POST, [](AsyncWebServerRequest *request)
              {
    if (request->hasParam("udmin", true)) settings.udmin = request->getParam("udmin", true)->value().toFloat();
    if (request->hasParam("udmax", true)) settings.udmax = request->getParam("udmax", true)->value().toFloat();
    if (request->hasParam("ufmin", true)) settings.ufmin = request->getParam("ufmin", true)->value().toFloat();
    if (request->hasParam("ufmax", true)) settings.ufmax = request->getParam("ufmax", true)->value().toFloat();
    if (request->hasParam("bootfactor", true)) settings.bootfactor = request->getParam("bootfactor", true)->value().toFloat();
    if (request->hasParam("boost_time", true)) settings.boost_time = request->getParam("boost_time", true)->value().toFloat();
    saveSettings(settings);
    request->redirect("/"); });

    server.begin();

    Serial.println("\n=== HỆ THỐNG ĐIỀU KHIỂN ĐỘNG CƠ KÉO/NHẢ V2.0 ===");
}

//============================ MAIN LOOP ============================//

void loop()
{
    if (Serial.available())
        processCommand(Serial.readStringUntil('\n'));

    // Read the sensors, calculate the factor
    bool sensorB = digitalRead(PULL_SENS_B);
    // Cảm biến thường mở: mặc định HIGH, LOW khi kích hoạt.
    // Boost bắt đầu khi sensor trở về trạng thái thường (HIGH) và giữ trong boost_time.
    if (sensorB && !lastSensorB)
    {
        is_boosting = true;
        boost_start_time = millis();
    }
    if (!sensorB)
    {
        // Khi sensor đang kích hoạt, không boost.
        is_boosting = false;
    }
    if (is_boosting && (millis() - boost_start_time >= settings.boost_time * 1000))
    {
        is_boosting = false;
    }
    ///////////////////
    if (is_boosting)
    {
        float elapsed = (millis() - boost_start_time) / 1000.0;
        boost_remain = max(0.0f, settings.boost_time - elapsed);
    }
    else
    {
        boost_remain = 0.0;
    }
    ////////////////////
    boot_factor = is_boosting ? 1.0 + (settings.bootfactor / 100.0) : 1.0;
    lastSensorB = sensorB;

    float dist = getDistance();
    float uFreq = 0.0;

    if (dist > 0)
    {
        dist = 0.7 * last_dist + 0.3 * dist; // Lọc nhiễu bằng cách lấy trung bình động
        uFreq = getFreq(dist);
        uFreq *= boot_factor;
        last_dist = dist;
    }

    // Update the freq
    switch (currentMode)
    {
    case MODE_RUN:
    {
        // Update the freq
        if (millis() - lastTime > UPDATE_CYCLE)
        {
            lastTime = millis();

            if (dist > 0)
            {
                last_uFreq = uFreq;
                // uFreq = constrain(uFreq + manualOffset, 0, 60);
                if (settings.uen && !isNetTaskRunning)
                {
                    // unwindVFD.setFrequency(uFreq);
                    // unwindVFD.sendControl(0x0012);

                    isNetTaskRunning = true;
                    NetJob *job = new NetJob();
                    job->freq = last_uFreq;
                    job->callback = [](bool result) {};
                    xTaskCreatePinnedToCore(backgroundNetTask, "NetTask", 3072, job, 1, NULL, 0);
                }
                if (settings.pen)
                {
                    // pullVFD.setFrequency(pFreq);
                    // pullVFD.sendControl(0x0012);
                }

                Serial.printf("[RUN] D:%.1f | F:%.1fHz | Boost:%s\n", dist, last_uFreq, is_boosting ? "ON" : "OFF");
            }
        }
        break;
    }
    case MODE_PING:
        Serial.printf("[PING] D: %.1f cm | B:%s\n", dist, sensorB ? "ON" : "OFF");
        break;
    }
    updateDisplay(dist, uFreq, sensorB, last_uFreq, is_boosting);
    delay(20);
}
