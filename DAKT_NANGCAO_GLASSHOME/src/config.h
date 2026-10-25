#pragma once

// để truy cập trang điều khiển tại địa chỉ mặc định: http://192.168.4.1
const char* AP_SSID = "GlassHome_ESP32";
const char* AP_PASS = "12345678";   // Mật khẩu WPA2, TỐI THIỂU 8 KÝ TỰ (bắt buộc của ESP32)

// ==========================================
// 2. CHU KỲ HOẠT ĐỘNG
// ==========================================
#define LOG_INTERVAL_MS   30000UL   // Lưu thêm 1 điểm vào lịch sử (RAM) mỗi 30 giây
#define HISTORY_SIZE       30       // Giữ tối đa 30 điểm lịch sử gần nhất (30 * 30s = 15 phút)

