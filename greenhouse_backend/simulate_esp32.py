"""
simulate_esp32.py
Script giả lập ESP32 gửi dữ liệu cảm biến qua MQTT, dùng để TEST hệ thống
khi chưa có phần cứng thật. Chạy song song với backend.

Cách chạy:
    python simulate_esp32.py
(Yêu cầu đã có MQTT Broker chạy ở localhost:1883, ví dụ Mosquitto)
"""

import json
import random
import time

import paho.mqtt.client as mqtt

BROKER = "localhost"
PORT = 1883
TOPIC = "greenhouse/vegetable/sensor/data"

client = mqtt.Client()
client.connect(BROKER, PORT, 60)

print("🚀 Bắt đầu giả lập ESP32 gửi dữ liệu cảm biến... (Ctrl+C để dừng)")

try:
    while True:
        payload = {
            "node_id": "ESP32_GREENHOUSE_01",
            "temp": round(random.uniform(24, 36), 1),
            "hum": round(random.uniform(45, 85), 1),
            "soil": round(random.uniform(15, 90), 1),
            "lux": round(random.uniform(500, 20000), 0),
        }
        client.publish(TOPIC, json.dumps(payload))
        print("📤 Đã gửi:", payload)
        time.sleep(3)
except KeyboardInterrupt:
    print("\n🛑 Dừng giả lập.")
