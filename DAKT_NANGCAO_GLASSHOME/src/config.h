#pragma once

// Truy cập trang điều khiển tại địa chỉ mặc định: http://192.168.4.1
const char* AP_SSID = "GlassHome_ESP32";
const char* AP_PASS = "12345678";   // Mật khẩu WPA2, TỐI THIỂU 8 KÝ TỰ

// ==========================================
// CHU KỲ HOẠT ĐỘNG
// ==========================================
#define LOG_INTERVAL_MS   30000UL
#define HISTORY_SIZE       30

// ==========================================
// NGƯỠNG SINH TRƯỞNG: RAU CẢI (cải xanh, cải ngọt, xà lách)
// Nguồn: Sở KH&CN Tuyên Quang; MDPI Foods 10(7):1524
// Tất cả đều có độ trễ (hysteresis) để relay không đóng/ngắt liên tục
// ==========================================

// --- Nhiệt độ không khí (°C): tối ưu 18-23 ---

#define TEMP_COOL_ON      25.0f   // > 25  -> bật quạt thông gió (hút ẩm/làm mát)
#define TEMP_COOL_OFF     23.0f   // <= 23 -> tắt quạt (nếu ẩm không khí cũng ổn)
#define TEMP_ROOF_HOT     28.0f   // >= 28 -> đóng mái che (nóng + nắng gắt làm cây còi cọc)
#define TEMP_ROOF_COOL    23.0f   // <= 23 -> cho phép mở lại khi trời sáng

// --- Độ ẩm đất (%): 70-85 ---
#define SOIL_PUMP_ON      70.0f   // < 70  -> bật bơm
#define SOIL_PUMP_OFF     85.0f   // >= 85 -> tắt bơm

// --- Độ ẩm không khí (%RH): 65-75 (MDPI: 70-85) ---
#define AIR_HUM_FAN_ON    80.0f   // > 80  -> bật quạt hút ẩm
#define AIR_HUM_FAN_OFF   75.0f   // <= 75 -> tắt quạt hút ẩm

// --- CO2 (ppm): 700-1000 ---
#define CO2_LOW_ON        700     // < 700  -> quạt VÀO (bổ sung CO2)
#define CO2_HIGH_ON       1000    // > 1000 -> quạt RA (thải bớt)
//701-999: quạt dừng (dung)
