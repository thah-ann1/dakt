"""
mqtt_client.py
Module kết nối MQTT Broker, lắng nghe dữ liệu cảm biến từ ESP32,
lưu vào Database, cập nhật cache RAM cho API, và chạy Rule Engine
tự động hóa (tưới nước, quạt, đèn, phun sương) cho nhà kính trồng rau.
"""

import json
import threading
from datetime import datetime

import paho.mqtt.client as mqtt

from app.config import settings
from app.database import SessionLocal
from app.models import SensorReading, ActuatorLog

# ----- Cache RAM: trả lời nhanh cho API mà không cần query DB mỗi lần -----
latest_data: dict = {}
sensor_history_cache: list = []

_client: mqtt.Client | None = None


def _publish_actuator(client: mqtt.Client, device: str, action: str, source: str = "AUTO_RULE"):
    """Gửi lệnh điều khiển thiết bị qua MQTT + ghi log vào DB."""
    topic = f"{settings.TOPIC_ACTUATOR_PREFIX}/{device}"
    client.publish(topic, json.dumps({"command": action, "source": source}))

    db = SessionLocal()
    try:
        db.add(ActuatorLog(device=device, action=action, source=source))
        db.commit()
    finally:
        db.close()
    print(f"⚙️  [ACTUATOR] {device.upper()} -> {action} (nguồn: {source})")


def _run_rule_engine(client: mqtt.Client, data: dict):
    """
    Rule Engine tự động hóa cho nhà kính rau:
    - Độ ẩm đất thấp -> bật bơm tưới | Độ ẩm đất quá cao -> tắt bơm (tránh úng rễ)
    - Nhiệt độ quá cao -> bật quạt thông gió
    - Độ ẩm không khí thấp -> bật phun sương
    - Ánh sáng yếu (ban ngày) -> bật đèn LED bổ sung quang hợp
    """
    soil = data.get("soil")
    temp = data.get("temp")
    hum = data.get("hum")
    lux = data.get("lux")

    if soil is not None:
        if soil < settings.SOIL_MOISTURE_MIN:
            _publish_actuator(client, "pump", "ON")
        elif soil > settings.SOIL_MOISTURE_MAX:
            _publish_actuator(client, "pump", "OFF")

    if temp is not None and temp > settings.TEMP_MAX:
        _publish_actuator(client, "fan", "ON")

    if hum is not None and hum < settings.HUMIDITY_MIN:
        _publish_actuator(client, "mist", "ON")

    if lux is not None and lux < settings.LIGHT_LUX_MIN:
        _publish_actuator(client, "lamp", "ON")


def _on_connect(client, userdata, flags, rc):
    print(f"✅ [MQTT] Đã kết nối Broker (code={rc}), subscribe topic: {settings.TOPIC_SENSOR}")
    client.subscribe(settings.TOPIC_SENSOR)


def _on_message(client, userdata, msg):
    global latest_data
    try:
        payload = json.loads(msg.payload.decode("utf-8"))
    except Exception as e:
        print(f"❌ [MQTT] Payload không phải JSON hợp lệ: {e}")
        return

    node_id = payload.get("node_id", "ESP32_GREENHOUSE_01")

    # 1. Lưu vào Database (lưu trữ vĩnh viễn để vẽ biểu đồ lịch sử)
    db = SessionLocal()
    try:
        record = SensorReading(
            node_id=node_id,
            temperature=payload.get("temp"),
            humidity=payload.get("hum"),
            soil_moisture=payload.get("soil"),
            light_lux=payload.get("lux"),
            co2_ppm=payload.get("co2"),
        )
        db.add(record)
        db.commit()
        db.refresh(record)
    finally:
        db.close()

    # 2. Cập nhật cache RAM (phục vụ API /latest, /history nhanh)
    payload["received_at"] = datetime.now().strftime("%Y-%m-%d %H:%M:%S")
    latest_data = payload
    sensor_history_cache.append(payload)
    if len(sensor_history_cache) > settings.MAX_HISTORY_CACHE:
        sensor_history_cache.pop(0)

    print(
        f"📥 [MQTT RECV] {node_id} | Temp={payload.get('temp')}°C "
        f"Hum={payload.get('hum')}% Soil={payload.get('soil')}% Lux={payload.get('lux')}"
    )

    # 3. Rule Engine - tự động hóa
    _run_rule_engine(client, payload)


def start_mqtt_background_thread():
    """Khởi động MQTT client trên 1 thread nền, chạy song song với FastAPI server."""
    global _client

    def _loop():
        global _client
        client = mqtt.Client()
        if settings.MQTT_USERNAME:
            client.username_pw_set(settings.MQTT_USERNAME, settings.MQTT_PASSWORD)
        client.on_connect = _on_connect
        client.on_message = _on_message
        _client = client
        try:
            client.connect(settings.MQTT_BROKER, settings.MQTT_PORT, keepalive=60)
            client.loop_forever()
        except Exception as e:
            print(f"❌ [MQTT] Không thể kết nối Broker {settings.MQTT_BROKER}:{settings.MQTT_PORT} -> {e}")

    thread = threading.Thread(target=_loop, daemon=True)
    thread.start()
    return thread


def publish_manual_command(device: str, action: str):
    """Dùng khi người dùng điều khiển thủ công từ Web/App (gọi từ REST API)."""
    if _client is None:
        raise RuntimeError("MQTT client chưa được khởi động")
    _publish_actuator(_client, device, action, source="MANUAL_WEB")
