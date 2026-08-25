# 🌱 Backend Nhà Kính Trồng Rau Thông Minh

Backend Python (FastAPI + MQTT + SQLite) lưu trữ dữ liệu cảm biến và điều khiển
thiết bị cho hệ thống nhà kính trồng rau, đồng thời phục vụ giao diện Dashboard
Frontend trực tiếp.

## Kiến trúc

```
greenhouse_backend/
├── app/
│   ├── main.py          # FastAPI app: REST API + phục vụ frontend
│   ├── config.py        # Cấu hình MQTT Broker, DB, ngưỡng cảnh báo
│   ├── database.py       # Kết nối SQLAlchemy (SQLite mặc định)
│   ├── models.py         # Bảng SensorReading, ActuatorLog
│   ├── schemas.py         # Pydantic validation
│   └── mqtt_client.py     # Lắng nghe MQTT + Rule Engine tự động hóa
├── frontend/
│   └── index.html         # Dashboard realtime (Chart.js)
├── simulate_esp32.py       # Giả lập ESP32 gửi dữ liệu (test không cần phần cứng)
└── requirements.txt
```

## Luồng dữ liệu

```
ESP32/Cảm biến --MQTT--> mqtt_client.py --lưu--> Database (SQLite)
                                |
                                +--> Rule Engine (tự bật bơm/quạt/đèn/phun sương)
                                |
FastAPI REST API <--query-- Database
        |
        +--> Frontend Dashboard (fetch JSON, vẽ biểu đồ, điều khiển thiết bị)
```

## Cài đặt

```bash
python -m venv venv
source venv/bin/activate      # Windows: venv\Scripts\activate
pip install -r requirements.txt
```

Cần có MQTT Broker chạy sẵn (ví dụ **Mosquitto**):
```bash
# Ubuntu/Debian
sudo apt install mosquitto mosquitto-clients
sudo systemctl start mosquitto
```

## Chạy Backend

```bash
uvicorn app.main:app --reload --host 0.0.0.0 --port 8000
```

- Dashboard: http://localhost:8000/
- Swagger API Docs: http://localhost:8000/docs

## Test không cần phần cứng thật

Mở terminal khác, chạy script giả lập ESP32:
```bash
python simulate_esp32.py
```
Script này gửi dữ liệu ngẫu nhiên (nhiệt độ, độ ẩm, độ ẩm đất, ánh sáng) mỗi 3 giây
qua MQTT topic `greenhouse/vegetable/sensor/data` — giống hệt ESP32 thật sẽ gửi.

## Các API chính

| Method | Endpoint | Mô tả |
|---|---|---|
| GET | `/api/v1/sensors/latest` | Dữ liệu cảm biến mới nhất (cache RAM, nhanh) |
| GET | `/api/v1/sensors/history/cache?limit=20` | Lịch sử gần nhất từ RAM |
| GET | `/api/v1/sensors/history?limit=100&hours=24` | Lịch sử từ Database (lưu trữ lâu dài) |
| GET | `/api/v1/sensors/stats?hours=24` | Thống kê trung bình/min/max |
| POST | `/api/v1/actuators/control` | Điều khiển thiết bị thủ công `{device, action}` |
| GET | `/api/v1/actuators/logs` | Lịch sử lệnh điều khiển (auto + manual) |
| GET | `/api/v1/health` | Health check |

## Rule Engine tự động hóa (điều chỉnh trong `app/config.py`)

| Điều kiện | Hành động |
|---|---|
| Độ ẩm đất < 30% | Bật máy bơm |
| Độ ẩm đất > 80% | Tắt máy bơm (tránh úng rễ) |
| Nhiệt độ > 34°C | Bật quạt thông gió |
| Độ ẩm không khí < 50% | Bật phun sương |
| Ánh sáng < 2000 lux | Bật đèn LED bổ sung |

## Nâng cấp gợi ý cho đồ án

- Đổi `DATABASE_URL` trong `config.py` sang PostgreSQL khi cần nhiều người dùng/dữ liệu lớn.
- Thêm xác thực (JWT) cho các API điều khiển để tránh bị điều khiển trái phép.
- Dùng InfluxDB nếu cần lưu time-series với tần suất rất cao (hàng giây).
- Thêm WebSocket (`/ws`) để đẩy dữ liệu realtime thay vì Frontend phải polling.
