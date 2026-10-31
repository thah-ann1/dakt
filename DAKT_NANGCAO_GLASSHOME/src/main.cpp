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

// ===== KHAI BÁO CHÂN PHẦN CỨNG =====
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

// === CẢM BIẾN ĐẤT VÀ PHAO TỪ ===
#define SOIL_PIN         34   // Chân Analog đọc độ ẩm đất
#define FLOAT_PIN        33   // Chân Digital đọc phao từ (Nối COM với GND, NO với GPIO 33)

// Hiệu chuẩn cảm biến đất
#define SOIL_DRY_VAL     3500
#define SOIL_WET_VAL     1200

#define OLED_SDA 21
#define OLED_SCL 22

volatile int  g_cachedClients      = 1;
unsigned long g_lastClientRefresh  = 0;
const unsigned long CLIENT_REFRESH_MS = 5000UL;

const TickType_t MUTEX_TIMEOUT = pdMS_TO_TICKS(150); // Giảm timeout để luồng chạy mượt hơn

WebServer server(80);
SemaphoreHandle_t stateMutex;

// ===== CO2 ISR =====
volatile unsigned long riseTime   = 0; //thời gian tăng: chiều cao của xung
volatile unsigned long highTime   = 0;// thời gian cao: chiều rộng của xung
volatile bool          newCO2Data = false; // dữ liệu mới co2 

void IRAM_ATTR pwmISR() { 
  if (digitalRead(MHZ19_PWM_PIN) == HIGH) { // đọc tín hiệu chân digital mức cao
    riseTime = micros();                    // thì thời gian tăng tính bằng us
  } else {
    if (riseTime != 0) { // nếu tín hiệu chân digital mức thấp thì tính thời gian cao
      unsigned long dt = micros() - riseTime; // tính thời gian cao (chiều rộng xung) bằng us - thời gian tăng
      if (dt >= 1500UL && dt <= 1005000UL) { // giới hạn thời gian cao từ 1.5ms đến 1005ms (tương ứng 0-5000ppm)
        highTime   = dt;  // lưu thời gian cao
        newCO2Data = true;  // đánh dấu có dữ liệu mới
      }
      riseTime = 0; // reset thời gian tăng về 0 để chuẩn bị cho lần đo tiếp theo
    }
  }
}

volatile unsigned long lastRelayChangeMs = 0; // Thời gian thay đổi trạng thái rơ le gần nhất (ms)
const    unsigned long CO2_FREEZE_MS     = 300;// Thời gian đóng băng dữ liệu CO2 sau khi thay đổi trạng thái rơ le (ms) để tránh dao động liên tục

// ===== KALMAN FILTER TỐC ĐỘ CAO =====
class KalmanFilter {
private:
  float _q, _r, _x, _p, _k; bool _initialized;
public:
  KalmanFilter(float q, float r, float p, float init)
    : _q(q), _r(r), _p(p), _x(init), _k(0.0), _initialized(false) {}
  float update(float m) {
    if (!_initialized) { _x = m; _initialized = true; return _x; }
    _p = _p + _q; _k = _p / (_p + _r);
    _x = _x + _k * (m - _x);
    _p = (1.0f - _k) * _p;
    return _x;
  }
};

// Đã tinh chỉnh lại hệ số Q rất cao, R rất thấp để cập nhật giá trị TỨC THỜI
KalmanFilter kalmanTemp(2.0, 0.5, 1.0, 18.0); 
KalmanFilter kalmanHum (2.0, 0.5, 1.0, 65.0);
KalmanFilter kalmanCO2 (10.0, 1.0, 1.0, 700.0);
KalmanFilter kalmanSoil(8.0, 0.1, 1.0, 70.0); // Siêu nhạy với biến động của nước

Adafruit_SSD1306 display(128, 64, &Wire, -1);

// ===== BIẾN TOÀN CỤC =====
float  t_k = 18.0; 
float  hum_k = 65.0;
float  co2_k = 700.0;
float  soil_pct = 70.0;
bool   tank_has_water = true;

int    light_raw    = 1;     // 1 = sáng, 0 = tối
String light_status = "TOI"; // "SANG" hoặc "TOI"

bool   is_auto_mode   = true; // true = tự động, false = thủ công
String pump_status    = "OFF"; //máy bơm chế độ OFF
String heat_status    = "OFF"; // đèn sưởi chế độ OFF
String hum_fan_status = "OFF"; // quạt hút ẩm chế độ OFF
String co2_fan_status = "DUNG"; // quạt hút CO2 chế độ DUNG
String roof_status    = "DUNG"; // mái che chế độ DUNG

struct SharedSnapshot { 
  float t, hum, co2, soil_pct;
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
// công thức tính ppm từ chân pwn co2
long readCO2PWM() {            // Đọc dữ liệu CO2 từ cảm biến MH-Z19 qua chân PWM
  if (!newCO2Data) return -1 ; // nếu không có dữ liệu mới thì trả về -1
  noInterrupts();              // tạm dừng ngắt để đọc dữ liệu an toàn
  unsigned long t = highTime;  // lấy thời gian cao (chiều rộng xung) tính bằng us
  newCO2Data = false;          // đánh dấu đã đọc dữ liệu mới
  interrupts();                // bật lại ngắt
  float t_ms = t / 1000.0f;    // chuyển đổi sang ms
  long  ppm  = (long)((t_ms - 2.0) * 5.0); // công thức tính ppm từ thời gian cao (ms)
  if (ppm < 0)    ppm = 0;     // giới hạn ppm >= 0
  if (ppm > 5000) ppm = 5000;  // giới hạn ppm <= 5000
  return ppm;                  // trả về giá trị ppm
}

// ===== LOGIC TỰ ĐỘNG =====
void applyAutoLogic() {                     // Thực hiện logic điều khiển tự động
  int sw1_val = digitalRead(LIMIT_SW_1_PIN);// đọc trạng thái công tắc giới hạn 1
  int sw2_val = digitalRead(LIMIT_SW_2_PIN);// đọc trạng thái công tắc giới hạn 2
  String prev;                              // lưu trạng thái trước đó của thiết bị để so sánh

// 1. MÁY BƠM
  prev = pump_status;                 // lưu trạng thái trước đó của máy bơm
  if (!tank_has_water) {              // Ưu tiên 1: Hết nước -> Tắt máy bơm
    digitalWrite(PUMP_PIN, HIGH); pump_status = "OFF"; 
  } else if (soil_pct > 85.0) {       // Ưu tiên 2: Độ ẩm đất > 85% -> CHẮC CHẮN TẮT (bỏ qua mọi điều kiện dưới)
    digitalWrite(PUMP_PIN, HIGH); pump_status = "OFF"; 
  } else if (soil_pct < 70.0 || hum_k < 65.0) { // Ưu tiên 3: Đất khô (< 70%) HOẶC Không khí khô (< 65%) -> Bật bơm
    digitalWrite(PUMP_PIN, LOW);  pump_status = "ON";  
  }
  if (prev != pump_status) lastRelayChangeMs = millis(); // cập nhật thời gian thay đổi rơ le
  
// 2. ĐÈN QUANG HỢP (Mốc ranh giới: 28 độ C)
  prev = heat_status;
  
  if (light_raw == LOW) {   
    // 1. CÓ ÁNH SÁNG TỰ NHIÊN -> Chắc chắn TẮT ĐÈN
    digitalWrite(HEATER_PIN, HIGH); heat_status = "OFF";
  } 
  else if (light_raw == HIGH && t_k > 23.0f) { 
    // 2. TRỜI TỐI + NÓNG (> 23) -> Ban ngày mây đen -> BẬT ĐÈN bù sáng
    digitalWrite(HEATER_PIN, LOW);  heat_status = "ON";
  } 
  else { 
    // 3. TRỜI TỐI + MÁT (<= 23.0f) -> Đã về đêm -> TẮT ĐÈN cho cây ngủ
    digitalWrite(HEATER_PIN, HIGH); heat_status = "OFF";
  }
  
  if (prev != heat_status) lastRelayChangeMs = millis();
  // 3. QUẠT HÚT ẨM
  prev = hum_fan_status; 
  if (hum_k > 75.0 || t_k > 23.0) { // nếu độ ẩm không khí > 75% hoặc nhiệt độ > 23°C thì 
    digitalWrite(HUM_FAN_PIN, LOW);  hum_fan_status = "ON";  //chân tính hiệu mức thấp, bật quạt hút ẩm
  }
  else if (hum_k <= 65.0 && t_k <= 23.0) {  // nếu độ ẩm không khí <= 65% và nhiệt độ <= 23°C thì
    digitalWrite(HUM_FAN_PIN, HIGH); hum_fan_status = "OFF"; //chân tính hiệu mức cao, tắt quạt hút ẩm
  }
  if (prev != hum_fan_status) lastRelayChangeMs = millis();  // nếu trạng thái quạt hút ẩm thay đổi thì cập nhật thời gian thay đổi rơ le

  // 4. QUẠT CO2
  prev = co2_fan_status;
  if (co2_k < 700) {  // nếu nồng độ CO2 < 700ppm thì 
    digitalWrite(CO2_FAN_IN1, HIGH); digitalWrite(CO2_FAN_IN2, LOW); //chân IN1 cao IN2 THẤP, bật quạt hút CO2 vào
    co2_fan_status = "VAO"; // quạt hút CO2 hút vào
  } else if (co2_k > 1000) { // nếu nồng độ CO2 > 1000ppm thì
    digitalWrite(CO2_FAN_IN1, LOW);  digitalWrite(CO2_FAN_IN2, HIGH); //chân IN1 THẤP, IN2 cao, bật quạt hút CO2 ra
    co2_fan_status = "RA"; // quạt hút CO2 hút ra
  } else {
    digitalWrite(CO2_FAN_IN1, LOW);  digitalWrite(CO2_FAN_IN2, LOW); //chân IN1 THẤP, IN2 THẤP, tắt quạt hút CO2
    co2_fan_status = "DUNG"; // quạt hút CO2 dừng
  }
  if (prev != co2_fan_status) lastRelayChangeMs = millis();

  // 5. MÁI CHE
  int roof_cmd = 0; // 1 = mở, -1 = đóng, 0 = dừng
  if      (t_k >= 23.0f)                       roof_cmd = -1; // nếu nhiệt độ >= 23°C thì đóng mái che
  else if (light_raw == LOW && t_k <= 23.0f)   roof_cmd =  1; // nếu trời sáng(mức thấp) và nhiệt độ <= 23°C thì mở mái che
  else if (light_raw == HIGH)                  roof_cmd = -1; // nếu trời tối(mức cao) thì đóng mái che
  else                                         roof_cmd =  0; // nếu không có điều kiện nào phù hợp thì dừng

  if (roof_cmd == 1) {     // lệnh mở mái che thì
    if (sw1_val == HIGH) { // nếu công tắc giới hạn 1 chưa kích hoạt thì mở mái che
      digitalWrite(ROOF_IN3, HIGH); digitalWrite(ROOF_IN4, LOW); roof_status = "MO";
    } else {
      digitalWrite(ROOF_IN3, LOW);  digitalWrite(ROOF_IN4, LOW); roof_status = "DA MO";
    }
  }
  else if (roof_cmd == -1) { // lệnh đóng mái che thì
    if (sw2_val == HIGH) {   // nếu công tắc giới hạn 2 chưa kích hoạt thì đóng mái che
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

// ===== SNAPSHOT =====
void updateSnapshot() { // Cập nhật dữ liệu snapshot toàn cục
  g_snap.t              = t_k; 
  g_snap.hum            = hum_k;
  g_snap.co2            = co2_k;
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

// ===== SETUP =====
void setup() { // Khởi tạo phần cứng và thiết lập ban đầu
  Serial.begin(115200);
  delay(300);

  pinMode(PUMP_PIN,     OUTPUT); digitalWrite(PUMP_PIN, HIGH); //chân tính hiệu mức cao, tắt máy bơm
  pinMode(HEATER_PIN,   OUTPUT); digitalWrite(HEATER_PIN, HIGH); //chân tính hiệu mức cao, tắt đèn sưởi
  pinMode(HUM_FAN_PIN,  OUTPUT); digitalWrite(HUM_FAN_PIN, HIGH); //chân tính hiệu mức cao, tắt quạt hút ẩm

  pinMode(CO2_FAN_IN1, OUTPUT); pinMode(CO2_FAN_IN2, OUTPUT); //chân tính hiệu mức cao, tắt quạt hút CO2
  pinMode(ROOF_IN3,    OUTPUT); pinMode(ROOF_IN4,    OUTPUT); //chân tính hiệu mức cao, tắt mái che
  digitalWrite(CO2_FAN_IN1, LOW); digitalWrite(CO2_FAN_IN2, LOW); //chân tính hiệu mức thấp, tắt quạt hút CO2
  digitalWrite(ROOF_IN3,    LOW); digitalWrite(ROOF_IN4,    LOW); //chân tính hiệu mức thấp, tắt mái che

  pinMode(LIGHT_SENSOR_PIN, INPUT); // Chân đọc cảm biến ánh sáng (LDR)
  pinMode(LIMIT_SW_1_PIN,   INPUT_PULLUP); // Chân đọc công tắc giới hạn 1 (Nối COM với GND, NO với GPIO 25)
  pinMode(LIMIT_SW_2_PIN,   INPUT_PULLUP); // Chân đọc công tắc giới hạn 2 (Nối COM với GND, NO với GPIO 26)
  pinMode(MHZ19_PWM_PIN,    INPUT); // Chân đọc tín hiệu PWM từ cảm biến CO2 MH-Z19
  
  pinMode(SOIL_PIN, INPUT); // Chân đọc cảm biến độ ẩm đất (Analog)
  analogReadResolution(12); // Đặt độ phân giải ADC 12 bit (0-4095)
  analogSetAttenuation(ADC_11db); // Đặt độ suy giảm tín hiệu ADC 11dB (0-3.3V)
  pinMode(FLOAT_PIN, INPUT_PULLUP); // Chân đọc phao từ (Nối COM với GND, NO với GPIO 33)

  attachInterrupt(digitalPinToInterrupt(MHZ19_PWM_PIN), pwmISR, CHANGE); // Gắn ngắt cho chân PWM của cảm biến CO2 MH-Z19

  light_raw    = digitalRead(LIGHT_SENSOR_PIN); // đọc trạng thái cảm biến ánh sáng (LDR)
  light_status = (light_raw == LOW) ? "SANG" : "TOI"; // nếu mức thấp thì trời sáng, nếu mức cao thì trời tối

  Wire.begin(OLED_SDA, OLED_SCL); // Khởi tạo giao tiếp I2C cho màn hình OLED chân SDA, SCL
  display.begin(SSD1306_SWITCHCAPVCC, 0x3C); // Khởi tạo màn hình OLED với địa chỉ I2C 0x3C
  display.setTextColor(SSD1306_WHITE); // Đặt màu chữ trắng trên nền đen
  display.setTextSize(1); // Đặt kích thước chữ 1 (mặc định)
  display.clearDisplay(); // Xóa màn hình OLED
  display.setCursor(0, 0); // Đặt vị trí đầu viết ở góc bên trái màn hình 
  display.println(F("GlassHome booting...")); // Hiển thị thông báo khởi động GlassHome
  display.println(F("OLED: SDA21 SCL22")); // Hiển thị thông tin chân kết nối màn hình OLED
  display.display(); // Trả về kết quả thànhc công/thất bại
//Đoạn này thiết lập Wi‑Fi, hiển thị thông tin kết nối trên OLED, rồi khởi động cảm biến, máy chủ web và tác vụ đọc cảm biến.
  WiFi.mode(WIFI_AP); //đặt ESP32 ở chế độ Access Point để thiết bị khác kết nối trực tiếp vào ESP32.
  WiFi.softAP(AP_SSID, AP_PASS); //tạo mạng Wi‑Fi với tên và mật khẩu lấy từ config.h
  IPAddress ip = WiFi.softAPIP(); //lấy địa chỉ IP của ESP32 khi ở chế độ AP, mặc định là
  (void)ip; // bỏ qua cảnh báo biến ip không được sử dụng

  display.clearDisplay(); // Xóa màn hình OLED
  display.setCursor(0, 0); // Đặt vị trí đầu viết ở góc bên trái màn hình
  display.println("WiFi: GlassHome_ESP32"); // Hiển thị tên mạng Wi‑Fi trên màn hình OLED
  display.println("IP  : 192.168.4.1"); // Hiển thị địa chỉ IP của ESP32 trên màn hình OLED
  display.println("PASS: 12345678"); // Hiển thị mật khẩu của mạng Wi‑Fi trên màn hình OLED
  display.display(); // Cập nhật màn hình OLED để hiển thị thông tin Wi‑Fi
  delay(2000); // Đợi 2 giây để người dùng có thời gian đọc thông tin Wi‑Fi trên màn hình OLED

  dht.begin(); // Khởi tạo cảm biến DHT22

  server.on("/",             HTTP_GET,  handleRoot); // Khi truy cập vào địa chỉ gốc của máy chủ web, gọi hàm handleRoot để hiển thị trang web chính
  server.on("/api/latest",   HTTP_GET,  handleLatest); // Khi truy cập vào địa chỉ /api/latest, gọi hàm handleLatest để trả về dữ liệu cảm biến mới nhất
  server.on("/api/history",  HTTP_GET,  handleHistory); // Khi truy cập vào địa chỉ /api/history, gọi hàm handleHistory để trả về dữ liệu lịch sử cảm biến
  server.on("/api/control",  HTTP_POST, handleControl); // Khi gửi yêu cầu POST đến địa chỉ /api/control, gọi hàm handleControl để xử lý điều khiển thiết bị

  server.begin(); // Khởi động máy chủ web để bắt đầu 
  stateMutex = xSemaphoreCreateMutex(); // Tạo mutex để bảo vệ truy cập dữ liệu chia sẻ giữa các tác vụ

  xSemaphoreTake(stateMutex, portMAX_DELAY); // Lấy mutex để cập nhật dữ liệu snapshot toàn cục
  updateSnapshot(); // Cập nhật dữ liệu snapshot toàn cục
  xSemaphoreGive(stateMutex); // Giải phóng mutex sau khi cập nhật dữ liệu snapshot toàn cục

  xTaskCreatePinnedToCore(sensorTask, "SensorTask", 8192, NULL, 1, NULL, 0); // Tạo tác vụ sensorTask chạy trên lõi 0 của ESP32 với kích thước ngăn xếp 8192 byte và độ ưu tiên 1
}

// ===== TASK CORE 0 (ÉP XUNG TỐC ĐỘ CAO) =====
void sensorTask(void *parameter) { // Tác vụ đọc cảm biến và thực hiện logic điều khiển tự động
  unsigned long lastLogTime     = 0; // Thời gian ghi log gần nhất (ms)
  unsigned long lastDHTRead     = 0; // Thời gian đọc cảm biến DHT22 gần nhất (ms)
  unsigned long lastOLEDDraw    = 0; // Thời gian vẽ màn hình OLED gần nhất (ms)
  unsigned long lastLogic       = 0; // Thời gian thực hiện logic điều khiển tự động gần nhất (ms)
  unsigned long lastSoilRead    = 0; // Thời gian đọc cảm biến độ ẩm đất gần nhất (ms)

  // GIẢM THỜI GIAN LẤY MẪU ĐỂ TĂNG TỐC ĐỘ PHẢN HỒI
  const unsigned long DHT_INTERVAL    = 2000; // Giới hạn phần cứng DHT22 (ko thể giảm)
  const unsigned long OLED_INTERVAL   = 100;  // Quét màn hình 10 lần/giây
  const unsigned long LOOP_INTERVAL   = 50;   // Vòng lặp task siêu mượt 50ms
  const unsigned long LOGIC_INTERVAL  = 100;  // Kiểm tra điều khiển bơm, rơ le 10 lần/s
  const unsigned long SOIL_INTERVAL   = 150;  // Đọc đất 6 lần/giây (Tức thời)

  int           ldrDebouncePrev = digitalRead(LIGHT_SENSOR_PIN); // Đọc trạng thái cảm biến ánh sáng (LDR) ban đầu
  int           ldrDebounceCnt  = 0; // Bộ đếm debounce cho cảm biến ánh sáng (LDR)
  int           ldrStableRaw    = ldrDebouncePrev; // Trạng thái ổn định của cảm biến ánh sáng (LDR) sau khi debounce
  unsigned long ldrLastChange   = 0; // Thời gian thay đổi trạng thái cảm biến ánh sáng (LDR) gần nhất (ms)

  for (;;) { // Vòng lặp vô hạn của tác vụ sensorTask
    unsigned long now = millis(); // Lấy thời gian hiện tại (ms)

    if (now - g_lastClientRefresh >= CLIENT_REFRESH_MS) { // Cập nhật số lượng client kết nối Wi‑Fi mỗi 5 giây
      g_lastClientRefresh = now; // Cập nhật thời gian lần cuối refresh client
      g_cachedClients     = WiFi.softAPgetStationNum(); // Lấy số lượng client kết nối Wi‑Fi
    }

    // 1. ĐỌC DHT22 .. DHT_INTERVAL =2 giây
    if (now - lastDHTRead >= DHT_INTERVAL) { // nếu đã đủ thời gian chờ để đọc cảm biến DHT22 (2 giây) thì thực hiện đọc dữ liệu
      lastDHTRead = now; // cập nhật thời gian lần cuối đọc cảm biến DHT22
      float t_raw = dht.readTemperature(); // đọc nhiệt độ từ cảm biến DHT22
      float h_raw = dht.readHumidity(); // đọc độ ẩm từ cảm biến DHT22
      xSemaphoreTake(stateMutex, portMAX_DELAY); //mutex khóa lại không cho phép đọc dữ liệu khi cập nhật
      if (!isnan(t_raw)) t_k   = kalmanTemp.update(t_raw); //cập nhật giá trị đã lọc Kalman cho nhiệt độ
      if (!isnan(h_raw) && h_raw >= 0.0f && h_raw <= 100.0f) hum_k = kalmanHum.update(h_raw); //cập nhật giá trị đã lọc Kalman cho độ ẩm nằm trong khoảng 0-100%
      xSemaphoreGive(stateMutex); // khi cập nhật xong dữ liệu mutex mở khóa cho phép đọc dữ liệu lại
    }

    // 2. ĐỌC ĐẤT & PHAO TỪ (TỐC ĐỘ CAO)
    if (now - lastSoilRead >= SOIL_INTERVAL) { // nếu đã đủ thời gian chờ để đọc cảm biến độ ẩm đất và phao từ (150ms) thì thực hiện đọc dữ liệu
      lastSoilRead = now; // cập nhật thời gian lần cuối đọc cảm biến độ ẩm đất và phao từ
      int raw_soil = analogRead(SOIL_PIN); // đọc giá trị analog từ cảm biến độ ẩm đất (0-4095)
      int pct = map(raw_soil, SOIL_DRY_VAL, SOIL_WET_VAL, 0, 100); // chuyển đổi giá trị analog sang phần trăm độ ẩm đất (0-100%)
      if (pct < 0) pct = 0; // giới hạn phần trăm độ ẩm đất >= 0%
      if (pct > 100) pct = 100; // giới hạn phần trăm độ ẩm đất <= 100%
      
      bool current_water = (digitalRead(FLOAT_PIN) == HIGH); // đọc trạng thái phao từ (HIGH = có nước, LOW = hết nước)
      
      xSemaphoreTake(stateMutex, portMAX_DELAY); //mutex khóa lại không cho phép đọc dữ liệu khi cập nhật
      soil_pct = kalmanSoil.update((float)pct);  //cập nhật giá trị đã lọc Kalman cho độ ẩm đất
      tank_has_water = current_water; // cập nhật trạng thái phao từ (có nước hay hết nước)
      xSemaphoreGive(stateMutex); // khi cập nhật xong dữ liệu mutex mở khóa cho phép đọc dữ liệu lại
    }

    // 3. ĐỌC CO2
    long co2_raw = readCO2PWM(); // đọc giá trị CO2 từ cảm biến MH-Z19 qua chân PWM 
    if (co2_raw >= 0) { // nếu giá trị CO2 hợp lệ (>= 0) thì thực hiện cập nhật dữ liệu
      bool frozen = (millis() - lastRelayChangeMs) < CO2_FREEZE_MS; // nếu thời gian kể từ lần thay đổi rơ le gần nhất < 300ms thì đóng băng dữ liệu CO2 để tránh dao động liên tục
      if (!frozen) { // nếu không đóng băng dữ liệu CO2 thì thực hiện cập nhật dữ liệu
        xSemaphoreTake(stateMutex, portMAX_DELAY); //mutex khóa lại không cho phép đọc dữ liệu khi cập nhật
        co2_k = kalmanCO2.update((float)co2_raw);  //cập nhật giá trị đã lọc Kalman cho nồng độ CO2
        xSemaphoreGive(stateMutex); // khi cập nhật xong dữ liệu mutex mở khóa cho phép đọc dữ liệu lại
      }
    }

    // 4. LDR DEBOUNCE (Phản ứng lẹ hơn)
    int ldrRead = digitalRead(LIGHT_SENSOR_PIN); // đọc trạng thái cảm biến ánh sáng (LDR) 
    bool ldrChanged = false; // biến cờ để kiểm tra xem trạng thái cảm biến ánh sáng (LDR) có thay đổi hay không

    if (ldrRead == ldrDebouncePrev) { // nếu trạng thái cảm biến ánh sáng (LDR) hiện tại giống với trạng thái trước đó thì tăng bộ đếm debounce lên 1
      if (ldrDebounceCnt < 2) ldrDebounceCnt++; // giới hạn bộ đếm debounce tối đa là 2
    } else {
      ldrDebouncePrev = ldrRead; ldrDebounceCnt  = 0; // nếu trạng thái cảm biến ánh sáng (LDR) hiện tại khác với trạng thái trước đó thì reset bộ đếm debounce về 0
    }

    if (ldrDebounceCnt >= 2 && ldrRead != ldrStableRaw) { // nếu bộ đếm debounce >= 2 và trạng thái cảm biến ánh sáng (LDR) hiện tại khác với trạng thái ổn định trước đó thì cập nhật trạng thái ổn định mới
      if (now - ldrLastChange >= 200) { // Giảm từ 1000ms xuống 200ms
        ldrStableRaw  = ldrRead; ldrLastChange = now; ldrChanged    = true; // cập nhật trạng thái ổn định mới và đánh dấu trạng thái cảm biến ánh sáng (LDR) đã thay đổi
      }
    }

    // 5. THỰC THI LOGIC
    xSemaphoreTake(stateMutex, portMAX_DELAY);

    if ((int)light_raw != ldrStableRaw) { // nếu trạng thái cảm biến ánh sáng (LDR) hiện tại khác với trạng thái ổn định trước đó thì cập nhật trạng thái ổn định mới
      light_raw    = ldrStableRaw; // cập nhật trạng thái cảm biến ánh sáng (LDR) hiện tại
      light_status = (light_raw == LOW) ? "SANG" : "TOI"; // cập nhật trạng thái ánh sáng (SANG hoặc TOI) dựa trên trạng thái cảm biến ánh sáng (LDR)
    }

    if (is_auto_mode && (ldrChanged || (now - lastLogic >= LOGIC_INTERVAL))) { // nếu đang ở chế độ tự động và trạng thái cảm biến ánh sáng (LDR) đã thay đổi hoặc đã đủ thời gian chờ để thực hiện logic điều khiển tự động (100ms) thì thực hiện logic điều khiển tự động
      lastLogic = now; // cập nhật thời gian lần cuối thực hiện logic điều khiển tự động
      applyAutoLogic(); // thực hiện logic điều khiển tự động
    }
    else if (!is_auto_mode) { // nếu đang ở chế độ thủ công thì chỉ thực hiện logic điều khiển máy bơm dựa trên trạng thái phao từ (có nước hay hết nước)
      if (!tank_has_water && pump_status == "ON") {
        digitalWrite(PUMP_PIN, HIGH); pump_status = "OFF"; lastRelayChangeMs = millis(); // nếu hết nước và máy bơm đang bật thì tắt máy bơm

      }
      if (roof_status == "MO"  && digitalRead(LIMIT_SW_1_PIN) == LOW) { digitalWrite(ROOF_IN3, LOW); digitalWrite(ROOF_IN4, LOW); roof_status = "DA MO";   lastRelayChangeMs = millis(); } // nếu mái che đang mở và công tắc giới hạn 1 đã kích hoạt thì dừng mái che
      if (roof_status == "DONG" && digitalRead(LIMIT_SW_2_PIN) == LOW) { digitalWrite(ROOF_IN3, LOW); digitalWrite(ROOF_IN4, LOW); roof_status = "DA DONG"; lastRelayChangeMs = millis(); } // nếu mái che đang đóng và công tắc giới hạn 2 đã kích hoạt thì dừng mái che
    }

    if (millis() - lastLogTime >= LOG_INTERVAL_MS) { // nếu đã đủ thời gian chờ để ghi log dữ liệu cảm biến (5 giây) thì thực hiện ghi log dữ liệu cảm biến
      lastLogTime = millis(); // cập nhật thời gian lần cuối ghi log dữ liệu cảm biến
      addHistoryPoint(); // thêm điểm dữ liệu cảm biến vào lịch sử
    }

    updateSnapshot(); // cập nhật dữ liệu snapshot toàn cục
    xSemaphoreGive(stateMutex); // khi cập nhật xong dữ liệu mutex mở khóa cho phép đọc dữ liệu lại

    // 6. OLED HIỂN THỊ
    if (now - lastOLEDDraw >= OLED_INTERVAL) { // nếu đã đủ thời gian chờ để vẽ màn hình OLED (100ms) thì thực hiện vẽ màn hình OLED
      lastOLEDDraw = now; // cập nhật thời gian lần cuối vẽ màn hình OLED
      xSemaphoreTake(stateMutex, portMAX_DELAY); //mutex khóa lại không cho phép đọc dữ liệu khi cập nhật
      SharedSnapshot s = g_snap; // sao chép dữ liệu snapshot toàn cục vào biến cục bộ để hiển thị trên màn hình OLED
      xSemaphoreGive(stateMutex); // khi sao chép xong dữ liệu mutex mở khóa cho phép đọc dữ liệu lại

      display.clearDisplay(); // Xóa màn hình OLED
      display.setTextColor(SSD1306_WHITE); // Đặt màu chữ trắng trên nền đen

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

      // HÀNG 2
      display.setTextSize(2);
      char tBuf[8]; snprintf(tBuf, sizeof(tBuf), "%.1f", s.t);
      display.setCursor(2, 16); display.print(tBuf);
      int degX = 2 + strlen(tBuf) * 12 + 2;
      display.drawCircle(degX, 20, 2, SSD1306_WHITE);
      display.setCursor(degX + 6, 16); display.print(F("C"));

      char sBuf[8]; snprintf(sBuf, sizeof(sBuf), "%.0f%%", s.soil_pct);
      int sW = strlen(sBuf) * 12;
      display.setCursor(126 - sW, 16); display.print(sBuf);
      
      // HÀNG 3
      display.setTextSize(1);
      display.setCursor(0, 34);
      display.print(F("CO2 ")); display.print((int)(s.co2 + 0.5f)); display.print(F("ppm"));
      
      display.setCursor(85, 34);
      if (s.tank_water) {
        display.print(F("NUOC:OK"));
      } else {
        display.print(F("HET NUOC")); 
      }
      display.drawLine(0, 42, 127, 42, SSD1306_WHITE);

      // HÀNG 4
      display.setCursor(0, 44);
      display.print(F("LDR:")); display.print(s.light_status);
      display.setCursor(70, 44);
      display.print(F("RH:")); display.print((int)(s.hum + 0.5f)); display.print(F("%"));

      // HÀNG 5
      display.setCursor(0, 53);
      display.print(F("BOM:")); display.print(s.pump_status == "ON" ? F("ON") : F("--")); // B: Máy bơm
      display.print(F(" D:")); display.print(s.heat_status == "ON" ? F("ON") : F("--")); // D: Đèn quang hợp
      display.print(F(" Q:")); display.print(s.hum_fan_status == "ON" ? F("ON") : F("--")); // Q: Quạt hút ẩm
      
      display.display();
    }
    vTaskDelay(pdMS_TO_TICKS(LOOP_INTERVAL)); // Đã giảm xuống 50ms để bắt tín hiệu nhạy nhất
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
  doc["humidity"]    = (int)(s.hum + 0.5f);
  doc["co2"]         = (int)s.co2;
  doc["soil_pct"]    = (int)s.soil_pct;
  doc["tank"]        = s.tank_water ? "CON" : "HET";
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
