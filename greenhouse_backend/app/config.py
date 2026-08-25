"""
config.py
Chứa toàn bộ cấu hình hệ thống: MQTT Broker, Database, ngưỡng cảnh báo (rule engine).
Có thể override bằng biến môi trường (environment variables) khi deploy thật.
"""

import os


class Settings:
    # ----- MQTT Broker -----
    MQTT_BROKER: str = os.getenv("MQTT_BROKER", "localhost")
    MQTT_PORT: int = int(os.getenv("MQTT_PORT", 1883))
    MQTT_USERNAME: str = os.getenv("MQTT_USERNAME", "")
    MQTT_PASSWORD: str = os.getenv("MQTT_PASSWORD", "")

    # Topic nhận dữ liệu cảm biến từ ESP32 / Node
    TOPIC_SENSOR: str = "greenhouse/vegetable/sensor/data"
    # Topic gửi lệnh điều khiển thiết bị (bơm, quạt, đèn, phun sương...)
    TOPIC_ACTUATOR_PREFIX: str = "greenhouse/vegetable/actuator"

    # ----- Database -----
    # Mặc định dùng SQLite (nhẹ, không cần cài server riêng, phù hợp đồ án).
    # Khi triển khai thật có thể đổi sang PostgreSQL:
    # "postgresql+psycopg2://user:password@localhost:5432/greenhouse"
    DATABASE_URL: str = os.getenv(
        "DATABASE_URL", "sqlite:///./greenhouse.db"
    )

    # ----- Ngưỡng cảnh báo / Rule Engine cho rau nhà kính -----
    SOIL_MOISTURE_MIN: float = 30.0      # % - dưới ngưỡng này -> bật bơm tưới
    SOIL_MOISTURE_MAX: float = 80.0      # % - trên ngưỡng này -> tắt bơm (tránh úng)
    TEMP_MAX: float = 34.0               # °C - vượt ngưỡng -> bật quạt/mở mái
    TEMP_MIN: float = 15.0               # °C - dưới ngưỡng -> cảnh báo lạnh
    HUMIDITY_MIN: float = 50.0           # % - dưới ngưỡng -> bật phun sương
    LIGHT_LUX_MIN: float = 2000.0        # lux - dưới ngưỡng ban ngày -> bật đèn LED

    # Giới hạn số bản ghi giữ trong bộ nhớ RAM (cache) cho API /latest
    MAX_HISTORY_CACHE: int = 200


settings = Settings()
