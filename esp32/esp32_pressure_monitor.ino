/*
 * =============================================================================
 * ระบบตรวจจับแรงกด (Pressure Monitoring System) — ESP32 Firmware
 * =============================================================================
 * ฮาร์ดแวร์: ESP32 + RFP-602 Sensor + Conversion Module
 * ฟีเจอร์: Moving Average, Firebase RTDB, Telegram Alert, LED Status
 * WiFiManager (ตั้งค่า Wi-Fi ผ่านมือถือ) + AP_STA Mode (ปล่อย Wi-Fi)
 * * *** Reset Wi-Fi : กดปุ่ม BOOT (GPIO0) ค้างไว้ 3 วินาที (ระดับ Nuclear: NVS Format)
 * ***
 * =============================================================================
 */

#include <ArduinoJson.h>
#include <Firebase_ESP_Client.h>
#include <HTTPClient.h>
#include <Preferences.h>
#include <WiFi.h>
#include <WiFiManager.h>
#include <nvs_flash.h> // ← เพิ่มไลบรารีล้างความจำระดับฮาร์ดแวร์
#include <time.h>

// Token generation helper (Firebase)
#include <addons/RTDBHelper.h>
#include <addons/TokenHelper.h>

#include "config.h"

// =============================================================================
//  Global Objects
// =============================================================================
FirebaseData fbdo;
FirebaseAuth auth;
FirebaseConfig config;
WiFiManager wm;

// =============================================================================
//  Moving Average Filter (ตัวกรองค่าเฉลี่ยเคลื่อนที่)
// =============================================================================
int readings[MOVING_AVG_SAMPLES]; // บัฟเฟอร์วงกลมสำหรับค่า ADC
int readIndex = 0;                // ตำแหน่งปัจจุบันในบัฟเฟอร์
long totalReadings = 0;           // ผลรวมสะสม
int averagedADC = 0;              // ค่า ADC ที่ผ่านการกรองแล้ว

// =============================================================================
//  ตัวแปรสถานะ
// =============================================================================
float currentWeightKg = 0.0;    // น้ำหนักปัจจุบัน (กก.)
float currentPressureKpa = 0.0; // แรงดันปัจจุบัน (kPa)
bool pressureDetected = false;  // ตรวจพบแรงกดหรือไม่?
bool alertSent = false;         // ส่งแจ้งเตือนไปแล้วหรือยัง (กัน Spam)

// ตัวจับเวลา
unsigned long pressureStartTime = 0;
unsigned long lastDataPush = 0;
unsigned long lastHeartbeat = 0;
unsigned long lastSensorRead = 0;
unsigned long lastAlertSent = 0;
unsigned long bootTime = 0;

// ตัวนับ
unsigned long totalReadingsCount = 0;
unsigned long totalAlertsSent = 0;

// แฟล็ก
bool firebaseReady = false;

// =============================================================================
//  Setup
// =============================================================================
void setup() {
  Serial.begin(115200);
  Serial.println("\n========================================");
  Serial.println("  ระบบตรวจจับแรงกด v2.4 (Nuclear Wi-Fi Reset)");
  Serial.println("========================================\n");

  // --- ตั้งค่าขาพิน ---
  pinMode(ANALOG_PIN, INPUT);
  pinMode(DIGITAL_PIN, INPUT);
  pinMode(WIFI_LED_PIN, OUTPUT);
  pinMode(PRESS_LED_PIN, OUTPUT);
  pinMode(LED_STATUS_PIN, OUTPUT);
  pinMode(WIFI_RESET_PIN, INPUT_PULLUP);

  digitalWrite(WIFI_LED_PIN, LOW);
  digitalWrite(PRESS_LED_PIN, LOW);
  digitalWrite(LED_STATUS_PIN, LOW);

  // เตรียมบัฟเฟอร์ Moving Average
  for (int i = 0; i < MOVING_AVG_SAMPLES; i++) {
    readings[i] = ADC_NO_LOAD;
  }
  totalReadings = (long)ADC_NO_LOAD * MOVING_AVG_SAMPLES;

  // ตรวจสอบปุ่ม Reset ก่อนเชื่อมต่อ Wi-Fi
  checkResetButton();

  // เชื่อมต่อ WiFi ผ่าน WiFiManager (Captive Portal)
  connectWiFi();

  // =========================================================================
  // เปิดโหมดทำงาน 2 ระบบ (รับเน็ต STA + ปล่อย Wi-Fi AP)
  // =========================================================================
  WiFi.mode(WIFI_AP_STA);
  WiFi.softAP(WIFI_AP_NAME, WIFI_AP_PASSWORD);

  Serial.print("[WIFI AP] ปล่อยสัญญาณ Wi-Fi สำเร็จ! Local IP (AP): ");
  Serial.println(WiFi.softAPIP());
  // =========================================================================

  // ตั้งเวลา NTP (เขตเวลา +7 สำหรับประเทศไทย)
  configTime(7 * 3600, 0, "pool.ntp.org", "time.nist.gov");
  Serial.println("[NTP] กำลังซิงค์เวลา...");
  delay(2000);

  // เริ่มต้น Firebase
  initFirebase();

  // บันทึกเวลาเปิดเครื่อง
  bootTime = millis();

  // ส่งแจ้งเตือนว่าเปิดเครื่องแล้ว
  sendTelegramMessage("✅ *ระบบตรวจจับแรงกดออนไลน์*\n"
                      "📍 ตำแหน่ง: " DEVICE_LOCATION "\n"
                      "🆔 อุปกรณ์: " DEVICE_ID "\n"
                      "📡 IP: " +
                      WiFi.localIP().toString());

  Serial.println("\n[SYSTEM] เริ่มต้นระบบเสร็จสมบูรณ์ กำลังเฝ้าระวัง...\n");
}

// =============================================================================
//  Main Loop
// =============================================================================
void loop() {
  unsigned long now = millis();

  // --- 0. ตรวจสอบปุ่ม BOOT ตลอดเวลา (กดค้าง 3 วิ เพื่อล้างรหัส Wi-Fi) ---
  checkResetButton();

  // --- 1. จัดการสถานะ WiFi + LED สีฟ้า (Pin 18) ---
  handleWiFiStatus();

  // --- 2. อ่านค่า Sensor ตามช่วงเวลาที่ตั้งไว้ ---
  if (now - lastSensorRead >= SENSOR_READ_INTERVAL_MS) {
    lastSensorRead = now;
    readSensor();
  }

  // --- 3. ส่งข้อมูลไป Firebase ---
  if (now - lastDataPush >= DATA_PUSH_INTERVAL_MS) {
    lastDataPush = now;
    if (firebaseReady && Firebase.ready()) {
      pushDataToFirebase();
    }
  }

  // --- 4. ส่ง Heartbeat ---
  if (now - lastHeartbeat >= HEARTBEAT_INTERVAL_MS) {
    lastHeartbeat = now;
    if (firebaseReady && Firebase.ready()) {
      sendHeartbeat();
    }
  }

  // --- 5. ตรวจสอบแจ้งเตือนแรงกด ---
  checkPressureAlerts(now);
}

// =============================================================================
//  จัดการสถานะ WiFi + LED สีฟ้า (Pin 18)
// =============================================================================
void handleWiFiStatus() {
  if (WiFi.status() == WL_CONNECTED) {
    digitalWrite(WIFI_LED_PIN, HIGH); // WiFi เชื่อมต่อแล้ว → ไฟฟ้าติด
  } else {
    digitalWrite(WIFI_LED_PIN, LOW); // WiFi หลุด → ไฟฟ้าดับ

    static unsigned long lastWiFiRetry = 0;
    if (millis() - lastWiFiRetry > 5000) {
      lastWiFiRetry = millis();
      Serial.println("[WIFI] หลุดการเชื่อมต่อ กำลัง Reconnect...");
      WiFi.disconnect();
      WiFi.reconnect();
    }
  }
}

// =============================================================================
//  ตรวจสอบปุ่ม Reset Wi-Fi (ระดับ Nuclear: NVS Format)
// =============================================================================
void checkResetButton() {
  if (digitalRead(WIFI_RESET_PIN) == LOW) {
    Serial.println("\n[RESET] ตรวจพบการกดปุ่ม BOOT! ค้างไว้ 3 วินาที...");

    // กะพริบ LED รัว ๆ เพื่อแจ้งผู้ใช้
    for (int i = 0; i < 30; i++) {
      digitalWrite(LED_STATUS_PIN, !digitalRead(LED_STATUS_PIN));
      delay(100);

      // ถ้าปล่อยมือก่อน 3 วินาที ให้ยกเลิก
      if (digitalRead(WIFI_RESET_PIN) == HIGH) {
        Serial.println("[RESET] ปล่อยปุ่มแล้ว — ยกเลิกการล้าง Wi-Fi");
        digitalWrite(LED_STATUS_PIN, LOW);
        return;
      }
    }

    // --- เริ่มกระบวนการล้างข้อมูลขั้นสูงสุด ---
    Serial.println("[RESET] กำลังล้างรหัส Wi-Fi ขั้นสูงสุด (NVS Format)!");

    // 1. ล้างข้อมูล WiFi ทั่วไป
    WiFi.disconnect(true, true);
    wm.resetSettings();
    delay(500);

    // 2. ไม้ตายสุดยอด: Format หน่วยความจำถาวรทิ้งทั้งหมด
    nvs_flash_erase();
    nvs_flash_init();

    digitalWrite(LED_STATUS_PIN, HIGH);
    Serial.println("[RESET] สำเร็จ! ความจำเสื่อมแน่นอน กำลังรีบูต...");
    delay(1000);
    ESP.restart(); // เปิดเครื่องใหม่
  }
}

// =============================================================================
//  เชื่อมต่อ WiFi ผ่าน WiFiManager (Captive Portal)
// =============================================================================
void connectWiFi() {
  wm.setTitle("ตั้งค่า Wi-Fi เซ็นเซอร์แรงกด");
  wm.setConfigPortalTimeout(180);
  wm.setConnectTimeout(30);
  wm.setClass("invert");

  Serial.println("[WIFI] กำลังเชื่อมต่อ Wi-Fi ด้วย WiFiManager...");

  bool connected = wm.autoConnect(WIFI_AP_NAME, WIFI_AP_PASSWORD);

  if (connected) {
    Serial.printf("[WIFI] เชื่อมต่อสำเร็จ! IP: %s (RSSI: %d dBm)\n",
                  WiFi.localIP().toString().c_str(), WiFi.RSSI());
    digitalWrite(WIFI_LED_PIN, HIGH);
  } else {
    Serial.println("[WIFI] หมดเวลาตั้งค่า กำลัง Restart...");
    ESP.restart();
  }
}

// =============================================================================
//  เริ่มต้น Firebase
// =============================================================================
void initFirebase() {
  Serial.println("[FIREBASE] กำลังเริ่มต้น...");

  config.api_key = FIREBASE_API_KEY;
  config.database_url = FIREBASE_HOST;
  auth.user.email = FIREBASE_USER_EMAIL;
  auth.user.password = FIREBASE_USER_PASSWORD;
  config.token_status_callback = tokenStatusCallback;

  fbdo.setBSSLBufferSize(4096, 1024);
  fbdo.setResponseSize(2048);

  Firebase.begin(&config, &auth);
  Firebase.reconnectNetwork(true);

  Serial.println("[FIREBASE] กำลังยืนยันตัวตน...");
  unsigned long authStart = millis();
  while (!Firebase.ready() && millis() - authStart < 15000) {
    delay(100);
  }

  if (Firebase.ready()) {
    firebaseReady = true;
    Serial.println("[FIREBASE] เชื่อมต่อและยืนยันตัวตนสำเร็จ!");
  } else {
    Serial.println("[FIREBASE] ยืนยันตัวตนไม่สำเร็จ กรุณาตรวจสอบข้อมูล");
  }
}

// =============================================================================
//  อ่านค่า Sensor + Moving Average + ควบคุม LED Pin 19
// =============================================================================
void readSensor() {
  int rawADC = analogRead(ANALOG_PIN);

  totalReadings -= readings[readIndex];
  readings[readIndex] = rawADC;
  totalReadings += rawADC;
  readIndex = (readIndex + 1) % MOVING_AVG_SAMPLES;

  averagedADC = totalReadings / MOVING_AVG_SAMPLES;
  currentWeightKg = mapADCToWeight(averagedADC);
  currentPressureKpa = currentWeightKg * 9.81 / 0.0001 / 1000.0;

  totalReadingsCount++;

  if (averagedADC < ADC_PRESS_THRESHOLD) {
    digitalWrite(PRESS_LED_PIN, HIGH);
  } else {
    digitalWrite(PRESS_LED_PIN, LOW);
  }

  if (totalReadingsCount % 10 == 0) {
    Serial.printf(
        "[SENSOR] RAW: %d | AVG: %d | น้ำหนัก: %.2f kg | แรงดัน: %.1f kPa\n",
        rawADC, averagedADC, currentWeightKg, currentPressureKpa);
  }
}

// =============================================================================
//  แปลงค่า ADC เป็นน้ำหนัก
// =============================================================================
float mapADCToWeight(int adcValue) {
  adcValue = constrain(adcValue, ADC_FULL_LOAD, ADC_NO_LOAD);

  float weight = (float)(ADC_NO_LOAD - adcValue) /
                 (float)(ADC_NO_LOAD - ADC_FULL_LOAD) * SENSOR_MAX_KG;

  if (weight < 0.05)
    weight = 0.0;
  return weight;
}

// =============================================================================
//  ส่งข้อมูลไป Firebase
// =============================================================================
void pushDataToFirebase() {
  String basePath = "/sensors/" + String(DEVICE_ID);

  FirebaseJson json;
  json.set("weight_kg", currentWeightKg);
  json.set("pressure_kpa", currentPressureKpa);
  json.set("adc_raw", averagedADC);
  json.set("timestamp/.sv", "timestamp");

  if (!Firebase.RTDB.setJSON(&fbdo, basePath + "/current", &json)) {
    Serial.printf("[FIREBASE] ส่งข้อมูลผิดพลาด: %s\n", fbdo.errorReason().c_str());
  }

  FirebaseJson historyEntry;
  historyEntry.set("weight_kg", currentWeightKg);
  historyEntry.set("pressure_kpa", currentPressureKpa);
  historyEntry.set("adc_raw", averagedADC);
  historyEntry.set("timestamp/.sv", "timestamp");

  if (!Firebase.RTDB.pushJSON(&fbdo, basePath + "/history", &historyEntry)) {
    Serial.printf("[FIREBASE] บันทึกประวัติผิดพลาด: %s\n",
                  fbdo.errorReason().c_str());
  }
}

// =============================================================================
//  ส่ง Heartbeat ไป Firebase
// =============================================================================
void sendHeartbeat() {
  String statusPath = "/sensors/" + String(DEVICE_ID) + "/status";

  FirebaseJson statusJson;
  statusJson.set("online", true);
  statusJson.set("last_seen/.sv", "timestamp");
  statusJson.set("ip", WiFi.localIP().toString());
  statusJson.set("rssi", WiFi.RSSI());
  statusJson.set("uptime_sec", (millis() - bootTime) / 1000);
  statusJson.set("free_heap", ESP.getFreeHeap());
  statusJson.set("device_id", DEVICE_ID);
  statusJson.set("location", DEVICE_LOCATION);
  statusJson.set("total_readings", (int)totalReadingsCount);
  statusJson.set("total_alerts", (int)totalAlertsSent);

  if (!Firebase.RTDB.setJSON(&fbdo, statusPath, &statusJson)) {
    Serial.printf("[FIREBASE] Heartbeat ผิดพลาด: %s\n",
                  fbdo.errorReason().c_str());
  }

  FirebaseJson offlineJson;
  offlineJson.set("online", false);
  offlineJson.set("last_seen/.sv", "timestamp");
  Firebase.RTDB.setJSON(&fbdo, statusPath + "/.onDisconnect", &offlineJson);
}

// =============================================================================
//  ตรวจสอบแจ้งเตือนแรงกด
// =============================================================================
void checkPressureAlerts(unsigned long now) {
  bool currentlyPressed = (currentWeightKg >= PRESSURE_THRESHOLD_KG);

  // --- กรณี 1: แรงกดผิดปกติ ---
  if (currentWeightKg >= ABNORMAL_PRESSURE_KG) {
    if (canSendAlert(now)) {
      String msg = "🚨 *แจ้งเตือน: แรงกดผิดปกติ!*\n\n"
                   "⚠️ น้ำหนัก: " +
                   String(currentWeightKg, 2) +
                   " kg\n"
                   "📊 แรงดัน: " +
                   String(currentPressureKpa, 1) +
                   " kPa\n"
                   "📍 ตำแหน่ง: " DEVICE_LOCATION "\n"
                   "🆔 อุปกรณ์: " DEVICE_ID "\n"
                   "⏰ เกินค่าที่กำหนด " +
                   String(ABNORMAL_PRESSURE_KG, 1) + " kg";
      sendTelegramMessage(msg);
      logAlertToFirebase("ABNORMAL_PRESSURE", currentWeightKg);
      lastAlertSent = now;
      totalAlertsSent++;
    }
  }

  // --- กรณี 2: กดค้างเกินเวลา ---
  if (currentlyPressed) {
    if (!pressureDetected) {
      pressureDetected = true;
      pressureStartTime = now;
      alertSent = false;
      Serial.printf("[ALERT] ตรวจพบแรงกด: %.2f kg — เริ่มจับเวลา\n",
                    currentWeightKg);
    } else {
      unsigned long durationMs = now - pressureStartTime;
      unsigned long thresholdMs =
          (unsigned long)CONTINUOUS_PRESSURE_SECONDS * 1000UL;

      if (durationMs >= thresholdMs && canSendAlert(now)) {
        unsigned long durationSec = durationMs / 1000;
        String msg = "⏱️ *แจ้งเตือน: กดค้างนานเกินไป!*\n\n"
                     "⚠️ ระยะเวลา: " +
                     String(durationSec) +
                     " วินาที\n"
                     "📊 น้ำหนักปัจจุบัน: " +
                     String(currentWeightKg, 2) +
                     " kg\n"
                     "📍 ตำแหน่ง: " DEVICE_LOCATION "\n"
                     "🆔 อุปกรณ์: " DEVICE_ID "\n"
                     "💡 กรุณาตรวจสอบและเปลี่ยนท่านอนให้ผู้ป่วย";
        sendTelegramMessage(msg);
        logAlertToFirebase("CONTINUOUS_PRESSURE", currentWeightKg);
        lastAlertSent = now;
        totalAlertsSent++;

        pressureStartTime = now;
      }
    }
  } else {
    if (pressureDetected) {
      unsigned long durationSec = (now - pressureStartTime) / 1000;
      Serial.printf("[ALERT] แรงกดหายไป หลังจาก %lu วินาที\n", durationSec);
      pressureDetected = false;
      pressureStartTime = 0;
      alertSent = false;
    }
  }
}

// =============================================================================
//  ตรวจสอบ Cooldown ก่อนส่ง Alert
// =============================================================================
bool canSendAlert(unsigned long now) {
  return (now - lastAlertSent >=
          (unsigned long)ALERT_COOLDOWN_SECONDS * 1000UL);
}

// =============================================================================
//  บันทึก Alert ไป Firebase
// =============================================================================
void logAlertToFirebase(const char *alertType, float weightKg) {
  if (!firebaseReady || !Firebase.ready())
    return;

  String alertPath = "/sensors/" + String(DEVICE_ID) + "/alerts";

  FirebaseJson alertJson;
  alertJson.set("type", alertType);
  alertJson.set("weight_kg", weightKg);
  alertJson.set("pressure_kpa", currentPressureKpa);
  alertJson.set("location", DEVICE_LOCATION);
  alertJson.set("device_id", DEVICE_ID);
  alertJson.set("timestamp/.sv", "timestamp");
  alertJson.set("resolved", false);

  if (!Firebase.RTDB.pushJSON(&fbdo, alertPath, &alertJson)) {
    Serial.printf("[FIREBASE] บันทึก Alert ผิดพลาด: %s\n",
                  fbdo.errorReason().c_str());
  } else {
    Serial.println("[FIREBASE] บันทึก Alert สำเร็จ");
  }
}

// =============================================================================
//  ส่งข้อความ Telegram eiei
// =============================================================================
void sendTelegramMessage(String message) {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("[TELEGRAM] ส่งไม่ได้ — WiFi ไม่ได้เชื่อมต่อ");
    return;
  }

  HTTPClient http;
  String url = "https://api.telegram.org/bot" + String(TELEGRAM_BOT_TOKEN) +
               "/sendMessage";

  http.begin(url);
  http.addHeader("Content-Type", "application/json");

  JsonDocument doc;
  doc["chat_id"] = TELEGRAM_CHAT_ID;
  doc["text"] = message;
  doc["parse_mode"] = "Markdown";

  String payload;
  serializeJson(doc, payload);

  int httpCode = http.POST(payload);

  if (httpCode == 200) {
    Serial.println("[TELEGRAM] ส่งแจ้งเตือนสำเร็จ!");
  } else {
    Serial.printf("[TELEGRAM] ส่งไม่สำเร็จ! HTTP code: %d\n", httpCode);
  }

  http.end();
}