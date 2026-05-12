
#pragma once
#include <Preferences.h>
extern Preferences prefs;

// --- Cấu trúc dữ liệu cấu hình ---
struct SystemSettings {
    bool uen = true;
    float udmin = 10.0, udmax = 50.0;
    float ufmin = 20.0, ufmax = 50.0;
    float bootfactor = 20.0; // Phần trăm bù tốc
    float boost_time = 5.0;  // Thời gian boost (giây)

    // Cau hinh boost nhieu cap.
    float boost_level1_pct = 20.0;
    float boost_level2_pct = 35.0;
    float boost_level3_pct = 50.0;
    float boost_level1_hold = 5.0;
    float boost_level2_hold = 4.0;
    float boost_level3_hold = 3.0;
    float boost_escalate_2 = 0.8;   // Giay kich lien tuc de len cap 2
    float boost_escalate_3 = 1.6;   // Giay kich lien tuc de len cap 3
    float boost_decay_time = 5.0;   // Giay on dinh de giam 1 cap
};

struct WiFiConfig {
    String ssid;
    String pass;
    bool hasStored;
};
// extern SystemSettings settings;

inline void sanitizeSettings(SystemSettings& settings) {
    settings.bootfactor = constrain(settings.bootfactor, 0.0f, 200.0f);
    settings.boost_time = constrain(settings.boost_time, 0.0f, 60.0f);

    settings.boost_level1_pct = constrain(settings.boost_level1_pct, 0.0f, 250.0f);
    settings.boost_level2_pct = constrain(settings.boost_level2_pct, settings.boost_level1_pct, 250.0f);
    settings.boost_level3_pct = constrain(settings.boost_level3_pct, settings.boost_level2_pct, 300.0f);

    settings.boost_level1_hold = constrain(settings.boost_level1_hold, 0.0f, 60.0f);
    settings.boost_level2_hold = constrain(settings.boost_level2_hold, 0.0f, 60.0f);
    settings.boost_level3_hold = constrain(settings.boost_level3_hold, 0.0f, 60.0f);

    settings.boost_escalate_2 = constrain(settings.boost_escalate_2, 0.1f, 20.0f);
    settings.boost_escalate_3 = constrain(settings.boost_escalate_3, settings.boost_escalate_2 + 0.1f, 30.0f);
    settings.boost_decay_time = constrain(settings.boost_decay_time, 0.0f, 60.0f);

    if (settings.udmax <= settings.udmin) {
        settings.udmax = settings.udmin + 1.0f;
    }

    if (settings.ufmax <= settings.ufmin) {
        settings.ufmax = settings.ufmin + 1.0f;
    }

    // Dong bo key cu voi cap 1 de giu tuong thich firmware cu/web form cu.
    settings.bootfactor = settings.boost_level1_pct;
    settings.boost_time = settings.boost_level1_hold;

}

// --- QUẢN LÝ BỘ NHỚ ---
inline void saveSettings(SystemSettings& settings) {
    sanitizeSettings(settings);

    if (!prefs.begin("MS", false)) {
        Serial.println("[ERROR] Không thể ghi vào NVS (Flash)!");
        return;
    }

    size_t bytesWritten = 0;
    bytesWritten += prefs.putBool("uen", settings.uen);
    bytesWritten += prefs.putFloat("udmin", settings.udmin);
    bytesWritten += prefs.putFloat("udmax", settings.udmax);
    bytesWritten += prefs.putFloat("ufmin", settings.ufmin);
    bytesWritten += prefs.putFloat("ufmax", settings.ufmax);
    bytesWritten += prefs.putFloat("bootfactor", settings.bootfactor);
    bytesWritten += prefs.putFloat("boost_time", settings.boost_time);
    bytesWritten += prefs.putFloat("b1_pct", settings.boost_level1_pct);
    bytesWritten += prefs.putFloat("b2_pct", settings.boost_level2_pct);
    bytesWritten += prefs.putFloat("b3_pct", settings.boost_level3_pct);
    bytesWritten += prefs.putFloat("b1_hold", settings.boost_level1_hold);
    bytesWritten += prefs.putFloat("b2_hold", settings.boost_level2_hold);
    bytesWritten += prefs.putFloat("b3_hold", settings.boost_level3_hold);
    bytesWritten += prefs.putFloat("b_esc2", settings.boost_escalate_2);
    bytesWritten += prefs.putFloat("b_esc3", settings.boost_escalate_3);
    bytesWritten += prefs.putFloat("b_decay", settings.boost_decay_time);

    prefs.end();

    if (bytesWritten > 0) {
        Serial.println("[SYSTEM] Đã lưu cài đặt an toàn vào Flash!");
    } else {
        Serial.println("[ERROR] Lỗi khi ghi dữ liệu!");
    }
}

inline void loadSettings(SystemSettings& settings) {
    bool isDirty = false; // Cờ đánh dấu nếu có dữ liệu mới cần khởi tạo
    bool migratedLegacyKeys = false;
    
    if (!prefs.begin("MS", false)) {
        Serial.println("[ERROR] Không thể mở NVS namespace 'MS'");
        return;
    }

    // Dọn các key cũ đã bị loại bỏ khỏi firmware hiện tại.
    if (prefs.isKey("pen")) {
        prefs.remove("pen");
        migratedLegacyKeys = true;
    }
    if (prefs.isKey("pfmin")) {
        prefs.remove("pfmin");
        migratedLegacyKeys = true;
    }
    if (prefs.isKey("pfmax")) {
        prefs.remove("pfmax");
        migratedLegacyKeys = true;
    }

    // Kiểm tra và đọc từng tham số
    // Nếu key không tồn tại (isKey trả về false), ta sẽ dùng giá trị mặc định
    if (!prefs.isKey("uen")) { settings.uen = true; isDirty = true; }
    else { settings.uen = prefs.getBool("uen"); }

    if (!prefs.isKey("udmin")) { isDirty = true; }
    else { settings.udmin = prefs.getFloat("udmin"); }

    if (!prefs.isKey("udmax")) { isDirty = true; }
    else { settings.udmax = prefs.getFloat("udmax"); }

    if (!prefs.isKey("ufmin")) { isDirty = true; }
    else { settings.ufmin = prefs.getFloat("ufmin"); }

    if (!prefs.isKey("ufmax")) { isDirty = true; }
    else { settings.ufmax = prefs.getFloat("ufmax"); }

    if (!prefs.isKey("bootfactor")) { isDirty = true; }
    else { settings.bootfactor = prefs.getFloat("bootfactor"); }

    if (!prefs.isKey("boost_time")) { isDirty = true; }
    else { settings.boost_time = prefs.getFloat("boost_time"); }

    if (!prefs.isKey("b1_pct")) { settings.boost_level1_pct = settings.bootfactor; isDirty = true; }
    else { settings.boost_level1_pct = prefs.getFloat("b1_pct"); }

    if (!prefs.isKey("b2_pct")) { isDirty = true; }
    else { settings.boost_level2_pct = prefs.getFloat("b2_pct"); }

    if (!prefs.isKey("b3_pct")) { isDirty = true; }
    else { settings.boost_level3_pct = prefs.getFloat("b3_pct"); }

    if (!prefs.isKey("b1_hold")) { settings.boost_level1_hold = settings.boost_time; isDirty = true; }
    else { settings.boost_level1_hold = prefs.getFloat("b1_hold"); }

    if (!prefs.isKey("b2_hold")) { isDirty = true; }
    else { settings.boost_level2_hold = prefs.getFloat("b2_hold"); }

    if (!prefs.isKey("b3_hold")) { isDirty = true; }
    else { settings.boost_level3_hold = prefs.getFloat("b3_hold"); }

    if (!prefs.isKey("b_esc2")) { isDirty = true; }
    else { settings.boost_escalate_2 = prefs.getFloat("b_esc2"); }

    if (!prefs.isKey("b_esc3")) { isDirty = true; }
    else { settings.boost_escalate_3 = prefs.getFloat("b_esc3"); }

    if (!prefs.isKey("b_decay")) { isDirty = true; }
    else { settings.boost_decay_time = prefs.getFloat("b_decay"); }

    prefs.end();
    sanitizeSettings(settings);

    // --- HIỂN THỊ GIÁ TRỊ ĐÃ TẢI ---
    Serial.println("---[ SETTINGS LOADED ]---");
    Serial.printf("UEN (User En)     : %s\n", settings.uen ? "ON" : "OFF");
    Serial.printf("UD (Min/Max)      : %.2f / %.2f\n", settings.udmin, settings.udmax);
    Serial.printf("UF (Min/Max)      : %.2f / %.2f\n", settings.ufmin, settings.ufmax);
    Serial.printf("Boot Factor       : %.2f\n", settings.bootfactor);
    Serial.printf("Boost Time        : %.2f s\n", settings.boost_time);
    Serial.printf("Boost L1/L2/L3 %%   : %.2f / %.2f / %.2f\n", settings.boost_level1_pct, settings.boost_level2_pct, settings.boost_level3_pct);
    Serial.printf("Boost Hold L1/L2/L3: %.2f / %.2f / %.2f s\n", settings.boost_level1_hold, settings.boost_level2_hold, settings.boost_level3_hold);
    Serial.printf("Boost Esc2/Esc3   : %.2f / %.2f s\n", settings.boost_escalate_2, settings.boost_escalate_3);
    Serial.printf("Boost Decay       : %.2f s\n", settings.boost_decay_time);
    Serial.println("-------------------------");
    if (migratedLegacyKeys) {
        Serial.println("[SYSTEM] Da xoa key cu: pen/pfmin/pfmax");
    }
    
    // Nếu đây là lần chạy đầu tiên (thiếu key), tự động lưu lại giá trị mặc định
    if (isDirty) {
        Serial.println("[SYSTEM] Phát hiện dữ liệu trống, đang khởi tạo giá trị mặc định...");
        saveSettings(settings);
    } else {
        Serial.println("[SYSTEM] Tải cài đặt thành công.");
    }
}

inline bool loadWiFiConfig(WiFiConfig& config) {
    config.ssid = "";
    config.pass = "";
    config.hasStored = false;

    if (!prefs.begin("MS", true)) {
        Serial.println("[ERROR] Khong the doc WiFi config");
        return false;
    }

    String ssid = prefs.getString("wifi_ssid", "");
    String pass = prefs.getString("wifi_pass", "");
    prefs.end();

    if (ssid.length() == 0) {
        return true;
    }

    config.ssid = ssid;
    config.pass = pass;
    config.hasStored = true;
    return true;
}

inline bool saveWiFiConfig(const String& ssid, const String& pass) {
    if (ssid.length() == 0 || ssid.length() > 32 || pass.length() > 64) {
        return false;
    }

    if (!prefs.begin("MS", false)) {
        Serial.println("[ERROR] Khong the ghi WiFi config");
        return false;
    }

    prefs.putString("wifi_ssid", ssid);
    prefs.putString("wifi_pass", pass);
    prefs.end();
    return true;
}

