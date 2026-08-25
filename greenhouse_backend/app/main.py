"""
main.py
Entrypoint chính của Backend hệ thống nhà kính trồng rau thông minh.

Chạy:  uvicorn app.main:app --reload --host 0.0.0.0 --port 8000
Xem API Docs:  http://localhost:8000/docs
Xem Dashboard: http://localhost:8000/
"""

from datetime import datetime, timedelta
from typing import Optional

from fastapi import FastAPI, HTTPException, Depends
from fastapi.middleware.cors import CORSMiddleware
from fastapi.staticfiles import StaticFiles
from sqlalchemy.orm import Session
from sqlalchemy import desc

from app.database import init_db, get_db
from app.models import SensorReading, ActuatorLog
from app.schemas import ControlRequest, SensorReadingOut, ActuatorLogOut, ApiResponse
from app import mqtt_client

app = FastAPI(
    title="Greenhouse Vegetable IoT Backend",
    description="Backend lưu trữ dữ liệu cảm biến & điều khiển thiết bị cho nhà kính trồng rau",
    version="1.0",
)

app.add_middleware(
    CORSMiddleware,
    allow_origins=["*"],
    allow_methods=["*"],
    allow_headers=["*"],
)


@app.on_event("startup")
def on_startup():
    init_db()
    mqtt_client.start_mqtt_background_thread()


# ================= REALTIME (cache RAM) =================

@app.get("/api/v1/sensors/latest", response_model=ApiResponse, tags=["Sensors - Realtime"])
def get_latest():
    """Trả về dữ liệu cảm biến mới nhất nhận qua MQTT (từ cache RAM, cực nhanh)."""
    if not mqtt_client.latest_data:
        raise HTTPException(status_code=404, detail="Chưa có dữ liệu cảm biến nào được ghi nhận")
    return ApiResponse(status="success", data=mqtt_client.latest_data)


@app.get("/api/v1/sensors/history/cache", response_model=ApiResponse, tags=["Sensors - Realtime"])
def get_history_cache(limit: int = 20):
    """Lịch sử gần nhất từ cache RAM (nhẹ, phù hợp cho biểu đồ realtime tần suất cao)."""
    data = mqtt_client.sensor_history_cache[-limit:]
    return ApiResponse(status="success", data=data)


# ================= LỊCH SỬ TỪ DATABASE (lưu trữ lâu dài) =================

@app.get("/api/v1/sensors/history", response_model=ApiResponse, tags=["Sensors - Database"])
def get_history_db(
    limit: int = 100,
    hours: Optional[int] = None,
    db: Session = Depends(get_db),
):
    """
    Lịch sử dữ liệu cảm biến lấy từ Database (lưu trữ lâu dài, dùng để vẽ biểu đồ xu hướng).
    - limit: số bản ghi tối đa trả về (mới nhất trước)
    - hours: chỉ lấy dữ liệu trong N giờ gần nhất (tùy chọn)
    """
    query = db.query(SensorReading)
    if hours:
        since = datetime.utcnow() - timedelta(hours=hours)
        query = query.filter(SensorReading.received_at >= since)

    records = query.order_by(desc(SensorReading.received_at)).limit(limit).all()
    result = [SensorReadingOut.model_validate(r).model_dump() for r in records]
    return ApiResponse(status="success", data=result)


@app.get("/api/v1/sensors/stats", response_model=ApiResponse, tags=["Sensors - Database"])
def get_stats(hours: int = 24, db: Session = Depends(get_db)):
    """Thống kê nhanh: giá trị trung bình / min / max trong N giờ gần nhất."""
    from sqlalchemy import func

    since = datetime.utcnow() - timedelta(hours=hours)
    row = (
        db.query(
            func.avg(SensorReading.temperature).label("avg_temp"),
            func.min(SensorReading.temperature).label("min_temp"),
            func.max(SensorReading.temperature).label("max_temp"),
            func.avg(SensorReading.humidity).label("avg_hum"),
            func.avg(SensorReading.soil_moisture).label("avg_soil"),
            func.count(SensorReading.id).label("count"),
        )
        .filter(SensorReading.received_at >= since)
        .first()
    )
    return ApiResponse(
        status="success",
        data={
            "period_hours": hours,
            "avg_temp": round(row.avg_temp, 2) if row.avg_temp else None,
            "min_temp": row.min_temp,
            "max_temp": row.max_temp,
            "avg_humidity": round(row.avg_hum, 2) if row.avg_hum else None,
            "avg_soil_moisture": round(row.avg_soil, 2) if row.avg_soil else None,
            "sample_count": row.count,
        },
    )


# ================= ĐIỀU KHIỂN THIẾT BỊ =================

@app.post("/api/v1/actuators/control", response_model=ApiResponse, tags=["Control"])
def control_actuator(req: ControlRequest):
    """Điều khiển thủ công thiết bị (bơm/quạt/đèn/phun sương) từ Web/App qua MQTT."""
    if req.action not in ("ON", "OFF"):
        raise HTTPException(status_code=400, detail="action phải là 'ON' hoặc 'OFF'")
    if req.device not in ("pump", "fan", "lamp", "mist"):
        raise HTTPException(status_code=400, detail="device không hợp lệ")

    try:
        mqtt_client.publish_manual_command(req.device, req.action)
    except RuntimeError as e:
        raise HTTPException(status_code=503, detail=str(e))

    return ApiResponse(status="success", message=f"Đã gửi lệnh {req.action} tới {req.device}")


@app.get("/api/v1/actuators/logs", response_model=ApiResponse, tags=["Control"])
def get_actuator_logs(limit: int = 50, db: Session = Depends(get_db)):
    """Lịch sử các lệnh điều khiển thiết bị (cả tự động lẫn thủ công)."""
    records = (
        db.query(ActuatorLog).order_by(desc(ActuatorLog.created_at)).limit(limit).all()
    )
    result = [ActuatorLogOut.model_validate(r).model_dump() for r in records]
    return ApiResponse(status="success", data=result)


# ================= HEALTH CHECK =================

@app.get("/api/v1/health", tags=["Health Check"])
def health():
    return {"status": "online", "system": "Greenhouse Vegetable Backend"}


# ================= PHỤC VỤ FRONTEND DASHBOARD =================
# Truy cập http://localhost:8000/ để xem giao diện dashboard trực quan
app.mount("/", StaticFiles(directory="frontend", html=True), name="frontend")
