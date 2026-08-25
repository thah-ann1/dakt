"""
models.py
Định nghĩa các bảng dữ liệu (ORM Models) lưu trong Database.
"""

from sqlalchemy import Column, Integer, Float, String, DateTime
from sqlalchemy.sql import func

from app.database import Base


class SensorReading(Base):
    """Bảng lưu mỗi lần nhận dữ liệu cảm biến từ nhà kính."""
    __tablename__ = "sensor_readings"

    id = Column(Integer, primary_key=True, index=True)
    node_id = Column(String(50), index=True, default="ESP32_GREENHOUSE_01")

    temperature = Column(Float, nullable=True)      # °C
    humidity = Column(Float, nullable=True)          # % độ ẩm không khí
    soil_moisture = Column(Float, nullable=True)     # % độ ẩm đất
    light_lux = Column(Float, nullable=True)         # cường độ ánh sáng
    co2_ppm = Column(Float, nullable=True)           # nồng độ CO2 (nếu có cảm biến)

    received_at = Column(DateTime(timezone=True), server_default=func.now(), index=True)


class ActuatorLog(Base):
    """Bảng lưu lịch sử điều khiển thiết bị (bơm, quạt, đèn, phun sương)."""
    __tablename__ = "actuator_logs"

    id = Column(Integer, primary_key=True, index=True)
    device = Column(String(30), index=True)   # pump | fan | lamp | mist
    action = Column(String(10))                # ON | OFF
    source = Column(String(20))                 # AUTO_RULE | MANUAL_WEB
    created_at = Column(DateTime(timezone=True), server_default=func.now(), index=True)
