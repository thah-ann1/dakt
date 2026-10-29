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

// === CẢM BIẾN MỚI ===
#define SOIL_PIN         34   // Chân Analog đọc độ ẩm đất
#define FLOAT_PIN        35   // Chân Digital đọc phao từ (Nối COM với GND, NO với GPIO 35)

// Hiệu chuẩn cảm biến đất (BẠN CẦN CHỈNH 2 SỐ NÀY THEO THỰC TẾ)
// Đất khô hoàn toàn -> Đo được số lớn (VD 3500)
// Đất ngập nước -> Đo được số nhỏ (VD 1200)
#define SOIL_DRY_VAL     3500
#define SOIL_WET_VAL     1200

#define OLED_SDA 21
#define OLED_SCL 22

volatile int  g_cachedClients      = 1;
unsigned long g_lastClientRefresh  = 0;
const unsigned long CLIENT_REFRESH_MS = 5000UL;

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
KalmanFilter kalmanTemp(0.4, 1.0, 1.0, 22.0);
KalmanFilter kalmanHum (0.4, 2.0, 1.0, 70.0);   // Độ ẩm không khí

KalmanFilter kalmanCO2 (6.0,  5.0, 10.0, 400.0);

Adafruit_SSD1306 display(128, 64, &Wire, -1);

// ===== BIEN TOAN CUC =====
float  t_k = 22.0, co2_k = 400.0;
float  hum_k = 70.0;            // Độ ẩm không khí (%RH)
float  soil_pct = 0.0;          // Độ ẩm đất (%)
bool   tank_has_water = true;   // Trạng thái bể nước (true = Còn nước)

int    light_raw    = 1;
String light_status = "TOI";

bool   is_auto_mode   = true;
String pump_status    = "OFF";
String heat_status    = "OFF";
String hum_fan_status = "OFF";
String co2_fan_status = "DUNG";
String roof_status    = "DUNG";

struct SharedSnapshot {
  float t, co2;
  float hum;
  float soil_pct;
  bool  tank_water;
  int   light_raw;
  String light_status;
  bool   auto_mode;
  String pump_status, heat_status, hum_fan_status, co2_fan_status, roof_status;
  int    clients;
  unsigned long sec;
  bool   valid;
} g_snap;

struct HistoryPoint { float t; float hum; float soil; int co2; bool light; unsigned long sec; };
HistoryPoint history[HISTORY_SIZE];
int historyCount = 0;
int historyHead  = 0;

void addHistoryPoint() {
  HistoryPoint p;
  p.t     = t_k;
  p.hum   = hum_k;
  p.soil  = soil_pct;
  p.co2   = (int)co2_k;
  p.light = (light_status == "SANG");
  p.sec   = millis() / 1000;
  history[historyHead] = p;
  historyHead = (historyHead + 1) % HISTORY_SIZE;
  if (historyCount < HISTORY_SIZE) historyCount++;
}

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

  // BƠM (độ ẩm đất rau cải 70-85%): bật khi <70%, tắt khi >=85%, giữa 2 ngưỡng giữ nguyên.
  // Hết nước trong bể -> luôn tắt để bảo vệ bơm.
  prev = pump_status;
  if (!tank_has_water) {
    digitalWrite(PUMP_PIN, HIGH); pump_status = "OFF";
  } else if (soil_pct < SOIL_PUMP_ON) {
    digitalWrite(PUMP_PIN, LOW);  pump_status = "ON";
  } else if (soil_pct >= SOIL_PUMP_OFF) {
    digitalWrite(PUMP_PIN, HIGH); pump_status = "OFF";
  }
  if (prev != pump_status) lastRelayChangeMs = millis();

  // SƯỞI (nhiệt độ tối ưu 18-23°C): bật khi <18, tắt khi >=20
  prev = heat_status;
  if      (t_k < TEMP_HEAT_ON)   { digitalWrite(HEATER_PIN, LOW);  heat_status = "ON";  }
  else if (t_k >= TEMP_HEAT_OFF) { digitalWrite(HEATER_PIN, HIGH); heat_status = "OFF"; }
  if (prev != heat_status) lastRelayChangeMs = millis();

  // QUẠT HÚT ẨM / LÀM MÁT: bật khi ẩm không khí > 80% HOẶC nhiệt độ > 25°C
  //                         tắt khi ẩm <= 75% VÀ nhiệt độ <= 23°C
  prev = hum_fan_status;
  if (hum_k > AIR_HUM_FAN_ON || t_k > TEMP_COOL_ON) {
    digitalWrite(HUM_FAN_PIN, LOW);  hum_fan_status = "ON";
  } else if (hum_k <= AIR_HUM_FAN_OFF && t_k <= TEMP_COOL_OFF) {
    digitalWrite(HUM_FAN_PIN, HIGH); hum_fan_status = "OFF";
  }
  if (prev != hum_fan_status) lastRelayChangeMs = millis();

  // QUẠT CO2 (tối ưu 700-1000 ppm): <700 -> VÀO, >1000 -> RA, có độ trễ 50 ppm
  prev = co2_fan_status;
  if (co2_k > CO2_HIGH_ON) {
    digitalWrite(CO2_FAN_IN1, LOW);  digitalWrite(CO2_FAN_IN2, HIGH);
    co2_fan_status = "RA";
  } else if (co2_k < CO2_LOW_ON) {
    digitalWrite(CO2_FAN_IN1, HIGH); digitalWrite(CO2_FAN_IN2, LOW);
    co2_fan_status = "VAO";
  } else if ((co2_fan_status == "VAO" && co2_k >= CO2_LOW_OFF) ||
             (co2_fan_status == "RA"  && co2_k <= CO2_HIGH_OFF)) {
    digitalWrite(CO2_FAN_IN1, LOW);  digitalWrite(CO2_FAN_IN2, LOW);
    co2_fan_status = "DUNG";
  }
  if (prev != co2_fan_status) lastRelayChangeMs = millis();

  // MÁI CHE: 1 mở, -1 đóng, 0 dừng
  // Rau cải ưa sáng trung bình: nóng (>=28°C) -> đóng che bớt nắng;
  // sáng + mát (<=23°C) -> mở đón sáng; tối + không nóng -> đóng giữ ấm.
  prev = roof_status;
  int roof_cmd = 0;
  if      (t_k >= TEMP_ROOF_HOT)                          roof_cmd = -1;
  else if (light_raw == LOW  && t_k <= TEMP_ROOF_COOL)    roof_cmd =  1;
  else if (light_raw == HIGH && t_k <= TEMP_COOL_ON)      roof_cmd = -1;
  else                                                    roof_cmd =  0;

  if (roof_cmd == 1) {
    if (sw1_val == HIGH) {
      digitalWrite(ROOF_IN3, HIGH); digitalWrite(ROOF_IN4, LOW); roof_status = "MO";
    } else {
      digitalWrite(ROOF_IN3, LOW);  digitalWrite(ROOF_IN4, LOW); roof_status = "DA MO";
    }
  }
  else if (roof_cmd == -1) {
    if (sw2_val == HIGH) {
      digitalWrite(ROOF_IN3, LOW);  digitalWrite(ROOF_IN4, HIGH); roof_status = "DONG";
    } else {
      digitalWrite(ROOF_IN3, LOW);  digitalWrite(ROOF_IN4, LOW); roof_status = "DA DONG";
    }
  }
  else {
    digitalWrite(ROOF_IN3, LOW); digitalWrite(ROOF_IN4, LOW);
    if      (roof_status == "MO")   roof_status = "DUNG";
    else if (roof_status == "DONG") roof_status = "DUNG";
  }
  if (prev != roof_status) lastRelayChangeMs = millis();
}

void updateSnapshot() {
  g_snap.t              = t_k;
  g_snap.co2            = co2_k;
  g_snap.hum            = hum_k;
  g_snap.soil_pct       = soil_pct;
  g_snap.tank_water     = tank_has_water;
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
  
  // CẤU HÌNH CẢM BIẾN MỚI
  pinMode(SOIL_PIN, INPUT);
  analogReadResolution(12); // 12-bit (0-4095)
  analogSetAttenuation(ADC_11db); // Đọc tối đa 3.3V
  
  // Phao từ: Nối COM xuống GND, NO lên GPIO 35. Dùng trở kéo nội bộ
  pinMode(FLOAT_PIN, INPUT_PULLUP);

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
  display.display();

  WiFi.mode(WIFI_AP);
  WiFi.softAP(AP_SSID, AP_PASS);

  display.clearDisplay();
  display.setCursor(0, 0);
  display.println("WiFi: GlassHome_ESP32");
  display.println("IP  : 192.168.4.1");
  display.println("PASS: 12345678");
  display.display();
  delay(2000);

  dht.begin();

  server.on("/",             HTTP_GET,  handleRoot);
  server.on("/api/latest",   HTTP_GET,  handleLatest);
  server.on("/api/history",  HTTP_GET,  handleHistory);
  server.on("/api/control",  HTTP_POST, handleControl);
  server.begin();

  stateMutex = xSemaphoreCreateMutex();
  xSemaphoreTake(stateMutex, portMAX_DELAY);
  updateSnapshot();
  xSemaphoreGive(stateMutex);

  xTaskCreatePinnedToCore(sensorTask, "SensorTask", 8192, NULL, 1, NULL, 0);
}

void sensorTask(void *parameter) {
  unsigned long lastLogTime     = 0;
  unsigned long lastDHTRead     = 0;
  unsigned long lastOLEDDraw    = 0;
  unsigned long lastLogic       = 0;
  unsigned long lastSoilRead    = 0;

  const unsigned long DHT_INTERVAL    = 2000;
  const unsigned long OLED_INTERVAL   = 200;
  const unsigned long LOOP_INTERVAL   = 200;
  const unsigned long LOGIC_INTERVAL  = 200;
  const unsigned long SOIL_INTERVAL   = 1000;

  int           ldrDebouncePrev = digitalRead(LIGHT_SENSOR_PIN);
  int           ldrDebounceCnt  = 0;
  int           ldrStableRaw    = ldrDebouncePrev;
  unsigned long ldrLastChange   = 0;

  for (;;) {
    unsigned long now = millis();

    if (now - g_lastClientRefresh >= CLIENT_REFRESH_MS) {
      g_lastClientRefresh = now;
      g_cachedClients     = WiFi.softAPgetStationNum();
    }

    // 1. DHT22 (Nhiệt độ + Độ ẩm không khí)
    if (now - lastDHTRead >= DHT_INTERVAL) {
      lastDHTRead = now;
      float t_raw = dht.readTemperature();
      float h_raw = dht.readHumidity();
      xSemaphoreTake(stateMutex, portMAX_DELAY);
      if (!isnan(t_raw)) t_k   = kalmanTemp.update(t_raw);
      if (!isnan(h_raw) && h_raw >= 0.0f && h_raw <= 100.0f) hum_k = kalmanHum.update(h_raw);
      xSemaphoreGive(stateMutex);
    }

    // 2. Đọc Đất & Phao
    if (now - lastSoilRead >= SOIL_INTERVAL) {
      lastSoilRead = now;
      
      // Đọc độ ẩm đất (Analog)
      int raw_soil = analogRead(SOIL_PIN);
      // Map: Khô (3500) -> 0%, Ướt (1200) -> 100%
      int pct = map(raw_soil, SOIL_DRY_VAL, SOIL_WET_VAL, 0, 100);
      if (pct < 0) pct = 0;
      if (pct > 100) pct = 100;
      
      // Đọc phao từ (Digital)
      // Có nước = HIGH, Cạn nước = LOW
      bool current_water = (digitalRead(FLOAT_PIN) == HIGH);
      
      xSemaphoreTake(stateMutex, portMAX_DELAY);
      soil_pct = (float)pct;
      tank_has_water = current_water;
      xSemaphoreGive(stateMutex);
    }

    // 3. CO2
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

    // 4. LDR Debounce
    int ldrRead = digitalRead(LIGHT_SENSOR_PIN);
    bool ldrChanged = false;
    if (ldrRead == ldrDebouncePrev) {
      if (ldrDebounceCnt < 2) ldrDebounceCnt++;
    } else {
      ldrDebouncePrev = ldrRead; ldrDebounceCnt  = 0;
    }
    if (ldrDebounceCnt >= 2 && ldrRead != ldrStableRaw) {
      if (now - ldrLastChange >= 1000) {
        ldrStableRaw  = ldrRead; ldrLastChange = now; ldrChanged    = true;
      }
    }

    // 5. Logic & Snapshot
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
      // An toàn: hết nước thì luôn ngắt bơm kể cả ở chế độ thủ công
      if (!tank_has_water && pump_status == "ON") {
        digitalWrite(PUMP_PIN, HIGH); pump_status = "OFF"; lastRelayChangeMs = millis();
      }
      if (roof_status == "MO"  && digitalRead(LIMIT_SW_1_PIN) == LOW) { digitalWrite(ROOF_IN3, LOW); digitalWrite(ROOF_IN4, LOW); roof_status = "DA MO";   lastRelayChangeMs = millis(); }
      if (roof_status == "DONG" && digitalRead(LIMIT_SW_2_PIN) == LOW) { digitalWrite(ROOF_IN3, LOW); digitalWrite(ROOF_IN4, LOW); roof_status = "DA DONG"; lastRelayChangeMs = millis(); }
    }

    if (millis() - lastLogTime >= LOG_INTERVAL_MS) {
      lastLogTime = millis();
      addHistoryPoint();
    }
    updateSnapshot();
    xSemaphoreGive(stateMutex);

    // 6. OLED
    if (now - lastOLEDDraw >= OLED_INTERVAL) {
      lastOLEDDraw = now;
      xSemaphoreTake(stateMutex, portMAX_DELAY);
      SharedSnapshot s = g_snap;
      xSemaphoreGive(stateMutex);

      display.clearDisplay();
      display.setTextColor(SSD1306_WHITE);

      // HEADER
      display.setTextSize(1);
      display.fillRect(0, 1, 3, 9, SSD1306_WHITE);
      display.setCursor(7, 2); display.print(F("GLASSHOME"));
      if (s.auto_mode) {
        display.fillRoundRect(85, 1, 43, 12, 3, SSD1306_WHITE);
        display.setTextColor(SSD1306_BLACK); display.setCursor(94, 3); display.print(F("AUTO"));
        display.setTextColor(SSD1306_WHITE);
      } else {
        display.drawRoundRect(85, 1, 43, 12, 3, SSD1306_WHITE);
        display.setCursor(97, 3); display.print(F("MAN"));
      }
      display.drawLine(0, 14, 127, 14, SSD1306_WHITE);

      // NHIỆT ĐỘ & ĐỘ ẨM ĐẤT (Hàng giữa)
      display.setTextSize(2);
      char tBuf[8]; snprintf(tBuf, sizeof(tBuf), "%.1f", s.t);
      display.setCursor(2, 16); display.print(tBuf);
      int degX = 2 + strlen(tBuf) * 12 + 2;
      display.drawCircle(degX, 20, 2, SSD1306_WHITE);
      display.setCursor(degX + 6, 16); display.print(F("C"));

      char sBuf[8]; snprintf(sBuf, sizeof(sBuf), "%.0f%%", s.soil_pct);
      int sW = strlen(sBuf) * 12;
      display.setCursor(126 - sW, 16); display.print(sBuf);
      
      // TRẠNG THÁI BỂ NƯỚC & CO2 (Hàng 3)
      display.setTextSize(1);
      display.setCursor(0, 34);
      display.print(F("CO2 ")); display.print((int)(s.co2 + 0.5f)); display.print(F("ppm"));
      
      display.setCursor(85, 34);
      if (s.tank_water) {
        display.print(F("NUOC:OK"));
      } else {
        display.print(F("HET NUOC")); // Cảnh báo hết nước
      }
      
      display.setCursor(0, 44);
      display.print(F("LDR:")); display.print(s.light_status);
      display.setCursor(70, 44);
      display.print(F("RH:")); display.print((int)(s.hum + 0.5f)); display.print(F("%"));

      // TRẠNG THÁI RELAY (Hàng cuối)
      display.setCursor(0, 53);
      display.print(F("BOM:")); display.print(s.pump_status == "ON" ? F("ON") : F("--"));
      display.print(F(" S:")); display.print(s.heat_status == "ON" ? F("ON") : F("--"));
      display.print(F(" Q:")); display.print(s.hum_fan_status == "ON" ? F("ON") : F("--"));
      display.print(F(" M:")); display.print(s.roof_status);

      display.display();
    }
    vTaskDelay(pdMS_TO_TICKS(LOOP_INTERVAL));
  }
}

void loop() {
  server.handleClient();
}

bool executeHardware(String device, String action) {
  if (xSemaphoreTake(stateMutex, MUTEX_TIMEOUT) != pdTRUE) return false;

  if (device == "mode") {
    if (action == "AUTO") { is_auto_mode = true; applyAutoLogic(); updateSnapshot(); }
    else if (action == "MANUAL") { is_auto_mode = false; updateSnapshot(); }
  }
  else if (!is_auto_mode) {
    if (device == "pump") {
      // CHẶN BƠM NẾU HẾT NƯỚC
      if (action == "ON" && tank_has_water) { 
        digitalWrite(PUMP_PIN, LOW);  pump_status = "ON";  lastRelayChangeMs = millis(); 
      }
      else if (action == "OFF" || !tank_has_water) { 
        digitalWrite(PUMP_PIN, HIGH); pump_status = "OFF"; lastRelayChangeMs = millis(); 
      }
    }
    else if (device == "heater") {
      if (action == "ON") { digitalWrite(HEATER_PIN, LOW);  heat_status = "ON";  lastRelayChangeMs = millis(); }
      else if (action == "OFF") { digitalWrite(HEATER_PIN, HIGH); heat_status = "OFF"; lastRelayChangeMs = millis(); }
    }
    else if (device == "fan_1") {
      if (action == "ON") { digitalWrite(HUM_FAN_PIN, LOW);  hum_fan_status = "ON";  lastRelayChangeMs = millis(); }
      else if (action == "OFF") { digitalWrite(HUM_FAN_PIN, HIGH); hum_fan_status = "OFF"; lastRelayChangeMs = millis(); }
    }
    else if (device == "fan_2") {
      if (action == "FWD") { digitalWrite(CO2_FAN_IN1, HIGH); digitalWrite(CO2_FAN_IN2, LOW);  co2_fan_status = "VAO";  lastRelayChangeMs = millis(); }
      else if (action == "REV") { digitalWrite(CO2_FAN_IN1, LOW);  digitalWrite(CO2_FAN_IN2, HIGH); co2_fan_status = "RA";   lastRelayChangeMs = millis(); }
      else if (action == "STOP") { digitalWrite(CO2_FAN_IN1, LOW);  digitalWrite(CO2_FAN_IN2, LOW);  co2_fan_status = "DUNG"; lastRelayChangeMs = millis(); }
    }
    else if (device == "roof") {
      if (action == "FWD") { digitalWrite(ROOF_IN3, HIGH); digitalWrite(ROOF_IN4, LOW);  roof_status = "MO";   lastRelayChangeMs = millis(); }
      else if (action == "REV") { digitalWrite(ROOF_IN3, LOW);  digitalWrite(ROOF_IN4, HIGH); roof_status = "DONG"; lastRelayChangeMs = millis(); }
      else if (action == "STOP") { digitalWrite(ROOF_IN3, LOW);  digitalWrite(ROOF_IN4, LOW);  roof_status = "DUNG"; lastRelayChangeMs = millis(); }
    }
    else { xSemaphoreGive(stateMutex); return false; }
  }
  else { xSemaphoreGive(stateMutex); return false; }

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
  doc["co2"]         = (int)s.co2;
  doc["humidity"]    = (int)(s.hum + 0.5f);
  doc["soil_pct"]    = (int)s.soil_pct;
  doc["tank"]        = s.tank_water ? "CON" : "HET";
  doc["light"]       = s.light_status;
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
    o["t"]    = serialized(String(history[idx].t, 1));
    o["hum"]  = history[idx].hum;
    o["soil"] = history[idx].soil;
    o["co2"]  = history[idx].co2;
    o["sec"]  = history[idx].sec;
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
  if (ok) server.send(200, "application/json", "{\"status\":\"ok\"}");
  else server.send(503, "application/json", "{\"status\":\"fail\"}");
}

void handleRoot() {
  server.sendHeader("Connection", "close");
  server.sendHeader("Cache-Control", "no-store");
  server.send_P(200, "text/html", INDEX_HTML);
}
