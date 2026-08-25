"""
schemas.py
Pydantic Schemas dùng để validate dữ liệu vào/ra của API (request/response).
"""

from datetime import datetime
from typing import Optional, List
from pydantic import BaseModel, Field


class SensorDataIn(BaseModel):
    """Dữ liệu thô gửi lên từ ESP32 qua MQTT (JSON payload)."""
    node_id: str = Field(default="ESP32_GREENHOUSE_01")
    temp: Optional[float] = None
    hum: Optional[float] = None
    soil: Optional[float] = None
    lux: Optional[float] = None
    co2: Optional[float] = None


class SensorReadingOut(BaseModel):
    id: int
    node_id: str
    temperature: Optional[float]
    humidity: Optional[float]
    soil_moisture: Optional[float]
    light_lux: Optional[float]
    co2_ppm: Optional[float]
    received_at: datetime

    class Config:
        from_attributes = True


class ControlRequest(BaseModel):
    device: str   # "pump", "fan", "lamp", "mist"
    action: str   # "ON", "OFF"


class ActuatorLogOut(BaseModel):
    id: int
    device: str
    action: str
    source: str
    created_at: datetime

    class Config:
        from_attributes = True


class ApiResponse(BaseModel):
    status: str
    message: Optional[str] = None
    data: Optional[object] = None
