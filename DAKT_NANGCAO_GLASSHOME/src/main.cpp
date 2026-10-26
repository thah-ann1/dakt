#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <ArduinoJson.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <DHT.h>
#include "config.h"
#include "web_page.h"
// ===== KHAI BAO CHAN PHAN CUNG =====
#define DHTPIN 4
#define DHTTYPE DHT22
DHT dht(DHTPIN, DHTTYPE);
#define PUMP_PIN      5
#define HEATER_PIN    18
#define HUM_FAN_PIN   32
#define CO2_FAN_IN1   19
#define CO2_FAN_IN2   23
#define ROOF_IN3      27
#define ROOF_IN4      13

#define LIGHT_SENSOR_PIN 14
#define LIMIT_SW_1_PIN   25
#define LIMIT_SW_2_PIN   26
#define MHZ19_PWM_PIN    16
#define OLED_SDA 21
#define OLED_SCL 22
volatile int  g_cachedClients      = 1;
unsigned long g_lastClientRefresh  = 0;
const unsigned long CLIENT_REFRESH_MS = 5000UL;

// ===== FIX #3: timeout ngắn khi take mutex =====
const TickType_t MUTEX_TIMEOUT = pdMS_TO_TICKS(300);

WebServer server(80);
SemaphoreHandle_t stateMutex;

// ===== CO2 ISR =====
volatile unsigned long riseTime   = 0;
volatile unsigned long highTime   = 0;
volatile bool          newCO2Data = false;

void IRAM_ATTR pwmISR() {
  if (digitalRead(MHZ19_PWM_PIN) == HIGH) {
    riseTime = micros();
  } else {
    if (riseTime != 0) {
      unsigned long dt = micros() - riseTime;
      if (dt >= 1500UL && dt <= 1005000UL) {
        highTime   = dt;
        newCO2Data = true;
      }
      riseTime = 0;
    }
  }
}

volatile unsigned long lastRelayChangeMs = 0;
const    unsigned long CO2_FREEZE_MS     = 300;

class CO2MedianFilter {
private:
  long buf[3]; int count; long lastAccepted; bool initialized;
public:
  CO2MedianFilter() : count(0), lastAccepted(400), initialized(false) {}
  long push(long ppm) {
    if (ppm < 0)    ppm = 0;
    if (ppm > 5000) ppm = 5000;
    if (initialized) {
      long d = ppm - lastAccepted;
      if (d < 0) d = -d;
      if (d > 400) return -1;
    }
    buf[0] = buf[1]; buf[1] = buf[2]; buf[2] = ppm;
    if (count < 3) count++;
    long med;
    if (count < 3) med = ppm;
    else {
      long a = buf[0], b = buf[1], c = buf[2];
      if ((a <= b && b <= c) || (c <= b && b <= a))      med = b;
      else if ((b <= a && a <= c) || (c <= a && a <= b)) med = a;
      else                                                med = c;
    }
    lastAccepted = med; initialized = true; return med;
  }
};
CO2MedianFilter co2Filter;

class KalmanFilter {
private:
  float _q, _r, _x, _p, _k; bool _initialized;
public:
  KalmanFilter(float q = 0.05, float r = 1.0, float p = 1.0, float init = 0.0)
    : _q(q), _r(r), _p(p), _x(init), _k(0.0), _initialized(false) {}
  float update(float m) {
    if (!_initialized) { _x = m; _initialized = true; return _x; }
    _p = _p + _q; _k = _p / (_p + _r);
    _x = _x + _k * (m - _x);
    _p = (1.0f - _k) * _p;
    return _x;
  }
};
KalmanFilter kalmanTemp(0.05, 1.0, 1.0, 25.0);
KalmanFilter kalmanHum (0.05, 2.0, 1.0, 65.0);
KalmanFilter kalmanCO2 (6.0,  5.0, 10.0, 250.0);

Adafruit_SSD1306 display(128, 64, &Wire, -1);

// ===== BIEN TOAN CUC =====
float  t_k = 25.0, h_k = 65.0, co2_k = 250.0;
int    light_raw    = 1;
String light_status = "TOI";

bool   is_auto_mode   = true;
String pump_status    = "OFF";
String heat_status    = "OFF";
String hum_fan_status = "OFF";
String co2_fan_status = "DUNG";
String roof_status    = "DUNG";

struct SharedSnapshot {
  float t, h, co2;
  int   light_raw;
  String light_status;
  bool   auto_mode;
  String pump_status, heat_status, hum_fan_status, co2_fan_status, roof_status;
  int    clients;
  unsigned long sec;
  bool   valid;
} g_snap;

struct HistoryPoint { float t, h; int co2; bool light; unsigned long sec; };
HistoryPoint history[HISTORY_SIZE];
int historyCount = 0;
int historyHead  = 0;

void addHistoryPoint() {
  HistoryPoint p;
  p.t     = t_k;
  p.h     = h_k;
  p.co2   = (int)co2_k;
  p.light = (light_status == "SANG");
  p.sec   = millis() / 1000;
  history[historyHead] = p;
  historyHead = (historyHead + 1) % HISTORY_SIZE;
  if (historyCount < HISTORY_SIZE) historyCount++;
}

// ===== FIX #4: prototype bool =====
bool executeHardware(String device, String action);
void applyAutoLogic();
void updateSnapshot();
long readCO2PWM();
void sensorTask(void *parameter);
void handleRoot();
void handleLatest();
void handleHistory();
void handleControl();

long readCO2PWM() {
  if (!newCO2Data) return -1;
  noInterrupts();
  unsigned long t = highTime;
  newCO2Data = false;
  interrupts();
  float t_ms = t / 1000.0f;
  long  ppm  = (long)((t_ms - 2.0) * 5.0);
  if (ppm < 0)    ppm = 0;
  if (ppm > 5000) ppm = 5000;
  return ppm;
}

// ===== LOGIC TU DONG =====
void applyAutoLogic() {
  int sw1_val = digitalRead(LIMIT_SW_1_PIN);
  int sw2_val = digitalRead(LIMIT_SW_2_PIN);
  String prev;

  prev = pump_status;
  if (t_k > 31.0 || h_k < 70.0) { digitalWrite(PUMP_PIN, LOW);  pump_status = "ON";  }
  else                          { digitalWrite(PUMP_PIN, HIGH); pump_status = "OFF"; }
  if (prev != pump_status) lastRelayChangeMs = millis();

  prev = heat_status;
  if (t_k < 20.0) { digitalWrite(HEATER_PIN, LOW);  heat_status = "ON";  }
  else            { digitalWrite(HEATER_PIN, HIGH); heat_status = "OFF"; }
  if (prev != heat_status) lastRelayChangeMs = millis();

  prev = hum_fan_status;
  if (h_k > 85.0 || t_k > 30.0) { digitalWrite(HUM_FAN_PIN, LOW);  hum_fan_status = "ON";  }
  else                          { digitalWrite(HUM_FAN_PIN, HIGH); hum_fan_status = "OFF"; }
  if (prev != hum_fan_status) lastRelayChangeMs = millis();

  prev = co2_fan_status;
  if (co2_k < 400) {
    digitalWrite(CO2_FAN_IN1, HIGH); digitalWrite(CO2_FAN_IN2, LOW);
    co2_fan_status = "VAO";
  } else if (co2_k > 500) {
    digitalWrite(CO2_FAN_IN1, LOW);  digitalWrite(CO2_FAN_IN2, HIGH);
    co2_fan_status = "RA";
  } else {
    digitalWrite(CO2_FAN_IN1, LOW);  digitalWrite(CO2_FAN_IN2, LOW);
    co2_fan_status = "DUNG";
  }
  if (prev != co2_fan_status) lastRelayChangeMs = millis();
// 10 mở,01 đóng
// Mai che theo LDR + cong tac hanh trinh
  prev = roof_status;
  if (light_raw == LOW) { // SANG -> mo mai
    if (sw1_val == HIGH) { digitalWrite(ROOF_IN3, HIGH); digitalWrite(ROOF_IN4, LOW);  roof_status = "MO";    }
    else                 { digitalWrite(ROOF_IN3, LOW);  digitalWrite(ROOF_IN4, LOW);  roof_status = "DA MO"; }
  } else {                // TOI -> dong mai
    if (sw2_val == HIGH) { digitalWrite(ROOF_IN3, LOW);  digitalWrite(ROOF_IN4, HIGH); roof_status = "DONG";    }
    else                 { digitalWrite(ROOF_IN3, LOW);  digitalWrite(ROOF_IN4, LOW);  roof_status = "DA DONG"; }
  }
  if (prev != roof_status) lastRelayChangeMs = millis();
}

// ===== SNAPSHOT =====
void updateSnapshot() {
  g_snap.t              = t_k;
  g_snap.h              = h_k;
  g_snap.co2            = co2_k;
  g_snap.light_raw      = light_raw;
  g_snap.light_status   = light_status;
  g_snap.auto_mode      = is_auto_mode;
  g_snap.pump_status    = pump_status;
  g_snap.heat_status    = heat_status;
  g_snap.hum_fan_status = hum_fan_status;
  g_snap.co2_fan_status = co2_fan_status;
  g_snap.roof_status    = roof_status;
  g_snap.clients        = g_cachedClients;
  g_snap.sec            = millis() / 1000;
  g_snap.valid          = true;
}

// ===== SETUP =====
void setup() {
  Serial.begin(115200);
  delay(300);

  pinMode(PUMP_PIN,     OUTPUT); digitalWrite(PUMP_PIN, HIGH);
  pinMode(HEATER_PIN,   OUTPUT); digitalWrite(HEATER_PIN, HIGH);
  pinMode(HUM_FAN_PIN,  OUTPUT); digitalWrite(HUM_FAN_PIN, HIGH);

  pinMode(CO2_FAN_IN1, OUTPUT); pinMode(CO2_FAN_IN2, OUTPUT);
  pinMode(ROOF_IN3,    OUTPUT); pinMode(ROOF_IN4,    OUTPUT);
  digitalWrite(CO2_FAN_IN1, LOW); digitalWrite(CO2_FAN_IN2, LOW);
  digitalWrite(ROOF_IN3,    LOW); digitalWrite(ROOF_IN4,    LOW);

  pinMode(LIGHT_SENSOR_PIN, INPUT);
  pinMode(LIMIT_SW_1_PIN,   INPUT_PULLUP);
  pinMode(LIMIT_SW_2_PIN,   INPUT_PULLUP);
  pinMode(MHZ19_PWM_PIN,    INPUT);
  attachInterrupt(digitalPinToInterrupt(MHZ19_PWM_PIN), pwmISR, CHANGE);

  light_raw    = digitalRead(LIGHT_SENSOR_PIN);
  light_status = (light_raw == LOW) ? "SANG" : "TOI";

  Wire.begin(OLED_SDA, OLED_SCL);
  display.begin(SSD1306_SWITCHCAPVCC, 0x3C);
  display.setTextColor(SSD1306_WHITE);
  display.setTextSize(1);
  display.clearDisplay();
  display.setCursor(0, 0);
  display.println(F("GlassHome booting..."));
  display.println(F("OLED: SDA21 SCL22"));
  display.display();

  WiFi.mode(WIFI_AP);
  WiFi.softAP(AP_SSID, AP_PASS);
  IPAddress ip = WiFi.softAPIP();
  (void)ip;

  display.clearDisplay();
  display.setCursor(0, 0);
  display.println("WiFi: GlassHome_ESP32");
  display.println("IP  : 192.168.4.1");
  display.println("PASS: 12345678");
  display.println();
  display.println("Mo trinh duyet de");
  display.println("dieu khien he thong");
  display.display();
  delay(2000);

  dht.begin();

  // ===== ROUTES =====
  server.on("/",             HTTP_GET,  handleRoot);
  server.on("/api/latest",   HTTP_GET,  handleLatest);
  server.on("/api/history",  HTTP_GET,  handleHistory);
  server.on("/api/control",  HTTP_POST, handleControl);

  // ===== FIX #5: endpoint debug =====
  server.on("/api/debug", HTTP_GET, []() {
    server.client().setNoDelay(true);
    JsonDocument doc;
    doc["gpio_pump"]    = digitalRead(PUMP_PIN);
    doc["gpio_heater"]  = digitalRead(HEATER_PIN);
    doc["gpio_fan1"]    = digitalRead(HUM_FAN_PIN);
    doc["gpio_co2_1"]   = digitalRead(CO2_FAN_IN1);
    doc["gpio_co2_2"]   = digitalRead(CO2_FAN_IN2);
    doc["gpio_roof_1"]  = digitalRead(ROOF_IN3);
    doc["gpio_roof_2"]  = digitalRead(ROOF_IN4);
    doc["status_pump"]  = pump_status;
    doc["status_heater"]= heat_status;
    doc["status_fan1"]  = hum_fan_status;
    doc["status_fan2"]  = co2_fan_status;
    doc["status_roof"]  = roof_status;
    doc["mode"]         = is_auto_mode ? "AUTO" : "MANUAL";
    bool mutex_free = (xSemaphoreTake(stateMutex, 0) == pdTRUE);
    if (mutex_free) xSemaphoreGive(stateMutex);
    doc["mutex_free"]   = mutex_free;

    String out;
    serializeJson(doc, out);
    server.sendHeader("Connection", "close");
    server.sendHeader("Cache-Control", "no-store");
    server.send(200, "application/json", out);
  });

  server.begin();

  stateMutex = xSemaphoreCreateMutex();

  xSemaphoreTake(stateMutex, portMAX_DELAY);
  updateSnapshot();
  xSemaphoreGive(stateMutex);

  xTaskCreatePinnedToCore(sensorTask, "SensorTask", 8192, NULL, 1, NULL, 0);
}

// ===== TASK CORE 0 =====
void sensorTask(void *parameter) {
  unsigned long lastLogTime     = 0;
  unsigned long lastDHTRead     = 0;
  unsigned long lastOLEDDraw    = 0;
  unsigned long lastLogic       = 0;

  const unsigned long DHT_INTERVAL    = 2000;
  const unsigned long OLED_INTERVAL   = 200;
  const unsigned long LOOP_INTERVAL   = 200;
  const unsigned long LOGIC_INTERVAL  = 200;

  int           ldrDebouncePrev = digitalRead(LIGHT_SENSOR_PIN);
  int           ldrDebounceCnt  = 0;
  int           ldrStableRaw    = ldrDebouncePrev;
  unsigned long ldrLastChange   = 0;
  const int           LDR_DEBOUNCE_N = 2;
  const unsigned long LDR_MIN_HOLD   = 1000;

  for (;;) {
    unsigned long now = millis();

    // ===== FIX #1: refresh client OUTSIDE mutex =====
    if (now - g_lastClientRefresh >= CLIENT_REFRESH_MS) {
      g_lastClientRefresh = now;
      g_cachedClients     = WiFi.softAPgetStationNum();
    }

    // ---- 1. DHT22 ----
    if (now - lastDHTRead >= DHT_INTERVAL) {
      lastDHTRead = now;
      float h_raw = dht.readHumidity();
      float t_raw = dht.readTemperature();
      if (!isnan(h_raw) && !isnan(t_raw)) {
        xSemaphoreTake(stateMutex, portMAX_DELAY);
        t_k = kalmanTemp.update(t_raw);
        h_k = kalmanHum.update(h_raw);
        xSemaphoreGive(stateMutex);
      }
    }

    // ---- 2. CO2 ----
    long co2_raw = readCO2PWM();
    if (co2_raw >= 0) {
      bool frozen = (millis() - lastRelayChangeMs) < CO2_FREEZE_MS;
      if (!frozen) {
        long co2_filtered = co2Filter.push(co2_raw);
        if (co2_filtered >= 0) {
          xSemaphoreTake(stateMutex, portMAX_DELAY);
          co2_k = kalmanCO2.update((float)co2_filtered);
          xSemaphoreGive(stateMutex);
        }
      }
    }

    // ---- 3. LDR debounce ----
    int ldrRead = digitalRead(LIGHT_SENSOR_PIN);
    bool ldrChanged = false;

    if (ldrRead == ldrDebouncePrev) {
      if (ldrDebounceCnt < LDR_DEBOUNCE_N) ldrDebounceCnt++;
    } else {
      ldrDebouncePrev = ldrRead;
      ldrDebounceCnt  = 0;
    }

    if (ldrDebounceCnt >= LDR_DEBOUNCE_N && ldrRead != ldrStableRaw) {
      if (now - ldrLastChange >= LDR_MIN_HOLD) {
        ldrStableRaw  = ldrRead;
        ldrLastChange = now;
        ldrChanged    = true;
      }
    }

    // ---- 4. Logic ----
    xSemaphoreTake(stateMutex, portMAX_DELAY);

    if ((int)light_raw != ldrStableRaw) {
      light_raw    = ldrStableRaw;
      light_status = (light_raw == LOW) ? "SANG" : "TOI";
    }

    if (is_auto_mode && (ldrChanged || (now - lastLogic >= LOGIC_INTERVAL))) {
      lastLogic = now;
      applyAutoLogic();
    }
    else if (!is_auto_mode) {
      int sw1_val = digitalRead(LIMIT_SW_1_PIN);
      int sw2_val = digitalRead(LIMIT_SW_2_PIN);
      if (roof_status == "MO"   && sw1_val == LOW) { digitalWrite(ROOF_IN3, LOW); digitalWrite(ROOF_IN4, LOW); roof_status = "DA MO";   lastRelayChangeMs = millis(); }
      if (roof_status == "DONG" && sw2_val == LOW) { digitalWrite(ROOF_IN3, LOW); digitalWrite(ROOF_IN4, LOW); roof_status = "DA DONG"; lastRelayChangeMs = millis(); }
    }

    // ---- 5. Ghi lich su ----
    if (millis() - lastLogTime >= LOG_INTERVAL_MS) {
      lastLogTime = millis();
      addHistoryPoint();
    }

    // ---- 6. Snapshot ----
    updateSnapshot();
    xSemaphoreGive(stateMutex);

    // ---- 7. OLED ----
    if (now - lastOLEDDraw >= OLED_INTERVAL) {
      lastOLEDDraw = now;

      xSemaphoreTake(stateMutex, portMAX_DELAY);
      SharedSnapshot s = g_snap;
      xSemaphoreGive(stateMutex);

      display.clearDisplay();
      display.setTextColor(SSD1306_WHITE);

      display.setTextSize(1);
      display.fillRect(0, 1, 3, 9, SSD1306_WHITE);
      display.setCursor(7, 2);
      display.print(F("GLASSHOME"));
      if (s.auto_mode) {
        display.fillRoundRect(85, 1, 43, 12, 3, SSD1306_WHITE);
        display.setTextColor(SSD1306_BLACK);
        display.setCursor(94, 3);
        display.print(F("AUTO"));
        display.setTextColor(SSD1306_WHITE);
      } else {
        display.drawRoundRect(85, 1, 43, 12, 3, SSD1306_WHITE);
        display.setCursor(97, 3);
        display.print(F("MAN"));
      }
      display.drawLine(0, 14, 127, 14, SSD1306_WHITE);

      display.setTextSize(2);
      char tBuf[8];
      snprintf(tBuf, sizeof(tBuf), "%.1f", s.t);
      display.setCursor(2, 16);
      display.print(tBuf);
      int degX = 2 + strlen(tBuf) * 12 + 2;
      display.drawCircle(degX, 20, 2, SSD1306_WHITE);
      display.setCursor(degX + 6, 16);
      display.print(F("C"));

      char hBuf[8];
      snprintf(hBuf, sizeof(hBuf), "%.0f%%", s.h);
      int hW = strlen(hBuf) * 12;
      display.setCursor(126 - hW, 16);
      display.print(hBuf);

      display.setTextSize(1);
      display.setCursor(0, 34);
      display.print(F("CO2 "));
      display.print((int)(s.co2 + 0.5f));
      display.print(F("ppm"));

      const int iconX = 88;
      if (s.light_status == "SANG") {
        display.drawCircle(iconX, 38, 3, SSD1306_WHITE);
        display.drawLine(iconX, 33, iconX, 34, SSD1306_WHITE);
        display.drawLine(iconX, 42, iconX, 43, SSD1306_WHITE);
        display.drawLine(iconX - 5, 38, iconX - 4, 38, SSD1306_WHITE);
        display.drawLine(iconX + 4, 38, iconX + 5, 38, SSD1306_WHITE);
        display.setCursor(98, 34);
        display.print(F("SANG"));
      } else {
        display.fillCircle(iconX, 38, 3, SSD1306_WHITE);
        display.fillCircle(iconX + 2, 36, 3, SSD1306_BLACK);
        display.setCursor(98, 34);
        display.print(F("TOI"));
      }

      display.drawLine(0, 42, 127, 42, SSD1306_WHITE);

      display.setCursor(0, 44);
      display.print(F("BOM:"));
      display.print(s.pump_status == "ON" ? F("ON") : F("--"));
      display.print(F(" S:"));
      display.print(s.heat_status == "ON" ? F("ON") : F("--"));
      display.print(F(" Q:"));
      display.print(s.hum_fan_status == "ON" ? F("ON") : F("--"));

      display.setCursor(0, 53);
      display.print(F("C:"));
      if      (s.co2_fan_status == "VAO") display.print(F("VAO"));
      else if (s.co2_fan_status == "RA")  display.print(F("RA"));
      else                                 display.print(F("--"));
      display.print(F(" M:"));
      if      (s.roof_status == "MO")      display.print(F("MO"));
      else if (s.roof_status == "DONG")    display.print(F("DG"));
      else if (s.roof_status == "DA MO")   display.print(F("DM"));
      else if (s.roof_status == "DA DONG") display.print(F("DG"));
      else                                  display.print(F("--"));

      display.display();
    }
    vTaskDelay(pdMS_TO_TICKS(LOOP_INTERVAL));
  }
}

void loop() {
  server.handleClient();
}
bool executeHardware(String device, String action) {
  if (xSemaphoreTake(stateMutex, MUTEX_TIMEOUT) != pdTRUE) {
    return false;   // mutex timeout
  }

  if (device == "mode") {
    if (action == "AUTO") {
      is_auto_mode = true;
      applyAutoLogic();
      updateSnapshot();
    } else if (action == "MANUAL") {
      is_auto_mode = false;
      updateSnapshot();
    }
  }
  else if (!is_auto_mode) {
    if (device == "pump") {
      if (action == "ON")       { digitalWrite(PUMP_PIN, LOW);  pump_status = "ON";  lastRelayChangeMs = millis(); }
      else if (action == "OFF") { digitalWrite(PUMP_PIN, HIGH); pump_status = "OFF"; lastRelayChangeMs = millis(); }
    }
    else if (device == "heater") {
      if (action == "ON")       { digitalWrite(HEATER_PIN, LOW);  heat_status = "ON";  lastRelayChangeMs = millis(); }
      else if (action == "OFF") { digitalWrite(HEATER_PIN, HIGH); heat_status = "OFF"; lastRelayChangeMs = millis(); }
    }
    else if (device == "fan_1") {
      if (action == "ON")       { digitalWrite(HUM_FAN_PIN, LOW);  hum_fan_status = "ON";  lastRelayChangeMs = millis(); }
      else if (action == "OFF") { digitalWrite(HUM_FAN_PIN, HIGH); hum_fan_status = "OFF"; lastRelayChangeMs = millis(); }
    }
    else if (device == "fan_2") {
      if (action == "FWD")       { digitalWrite(CO2_FAN_IN1, HIGH); digitalWrite(CO2_FAN_IN2, LOW);  co2_fan_status = "VAO";  lastRelayChangeMs = millis(); }
      else if (action == "REV")  { digitalWrite(CO2_FAN_IN1, LOW);  digitalWrite(CO2_FAN_IN2, HIGH); co2_fan_status = "RA";   lastRelayChangeMs = millis(); }
      else if (action == "STOP") { digitalWrite(CO2_FAN_IN1, LOW);  digitalWrite(CO2_FAN_IN2, LOW);  co2_fan_status = "DUNG"; lastRelayChangeMs = millis(); }
    }
    else if (device == "roof") {
      if (action == "FWD")       { digitalWrite(ROOF_IN3, HIGH); digitalWrite(ROOF_IN4, LOW);  roof_status = "MO";   lastRelayChangeMs = millis(); }
      else if (action == "REV")  { digitalWrite(ROOF_IN3, LOW);  digitalWrite(ROOF_IN4, HIGH); roof_status = "DONG"; lastRelayChangeMs = millis(); }
      else if (action == "STOP") { digitalWrite(ROOF_IN3, LOW);  digitalWrite(ROOF_IN4, LOW);  roof_status = "DUNG"; lastRelayChangeMs = millis(); }
    }
    else {
      // device không hợp lệ
      xSemaphoreGive(stateMutex);
      return false;
    }
  }
  else {
    // AUTO mode — từ chối lệnh thiết bị
    xSemaphoreGive(stateMutex);
    return false;
  }
  updateSnapshot();
  xSemaphoreGive(stateMutex);
  return true;
}

// ===== WEB HANDLERS =====
void handleLatest() {
  server.client().setNoDelay(true);
  if (xSemaphoreTake(stateMutex, MUTEX_TIMEOUT) != pdTRUE) {
    server.sendHeader("Connection", "close");
    server.send(503, "application/json", "{\"error\":\"busy\"}");
    return;
  }
  SharedSnapshot s = g_snap;
  xSemaphoreGive(stateMutex);

  JsonDocument doc;
  doc["temperature"] = serialized(String(s.t, 1));
  doc["humidity"]    = serialized(String(s.h, 1));
  doc["co2"]         = (int)s.co2;
  doc["light"]       = s.light_status;
  doc["light_raw"]   = s.light_raw;
  doc["mode"]        = s.auto_mode ? "AUTO" : "MANUAL";
  doc["pump"]        = s.pump_status;
  doc["heater"]      = s.heat_status;
  doc["fan_1"]       = s.hum_fan_status;
  doc["fan_2"]       = s.co2_fan_status;
  doc["roof"]        = s.roof_status;
  doc["clients"]     = s.clients;
  doc["uptime"]      = s.sec;
  String out;
  serializeJson(doc, out);
  server.sendHeader("Connection", "close");
  server.sendHeader("Cache-Control", "no-store");
  server.send(200, "application/json", out);
}

void handleHistory() {
  server.client().setNoDelay(true);
  if (xSemaphoreTake(stateMutex, MUTEX_TIMEOUT) != pdTRUE) {
    server.sendHeader("Connection", "close");
    server.send(503, "application/json", "[]");
    return;
  }
  JsonDocument doc;
  JsonArray arr = doc.to<JsonArray>();
  int startIdx = (historyCount < HISTORY_SIZE) ? 0 : historyHead;
  for (int i = 0; i < historyCount; i++) {
    int idx = (startIdx + i) % HISTORY_SIZE;
    JsonObject o = arr.add<JsonObject>();
    o["t"]   = serialized(String(history[idx].t, 1));
    o["h"]   = serialized(String(history[idx].h, 1));
    o["co2"] = history[idx].co2;
    o["sec"] = history[idx].sec;
  }
  String out;
  serializeJson(doc, out);
  xSemaphoreGive(stateMutex);
  server.sendHeader("Connection", "close");
  server.sendHeader("Cache-Control", "no-store");
  server.send(200, "application/json", out);
}
void handleControl() {
  server.client().setNoDelay(true);
  if (!server.hasArg("plain")) {
    server.sendHeader("Connection", "close");
    server.send(400, "application/json", "{\"error\":\"missing body\"}");
    return;
  }
  JsonDocument doc;
  DeserializationError err = deserializeJson(doc, server.arg("plain"));
  if (err) {
    server.sendHeader("Connection", "close");
    server.send(400, "application/json", "{\"error\":\"invalid json\"}");
    return;
  }
  String device = doc["device"].as<String>();
  String action = doc["action"].as<String>();
  bool ok = executeHardware(device, action);
  server.sendHeader("Connection", "close");
  if (ok) {
    server.send(200, "application/json", "{\"status\":\"ok\"}");
  } else {
    server.send(503, "application/json", "{\"status\":\"fail\",\"reason\":\"busy or wrong mode\"}");
  }
}
void handleRoot() {
  server.sendHeader("Connection", "close");
  server.sendHeader("Cache-Control", "no-store");
  server.send_P(200, "text/html", INDEX_HTML);
}
