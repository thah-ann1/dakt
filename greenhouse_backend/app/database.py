"""
database.py
Thiết lập kết nối Database bằng SQLAlchemy (mặc định SQLite, dễ đổi sang PostgreSQL).
Đây là nơi lưu trữ VĨNH VIỄN dữ liệu cảm biến & lịch sử điều khiển thiết bị,
khác với bộ nhớ RAM tạm (cache) chỉ dùng để trả lời nhanh cho API /latest.
"""

from sqlalchemy import create_engine
from sqlalchemy.orm import sessionmaker, declarative_base

from app.config import settings

connect_args = {}
if settings.DATABASE_URL.startswith("sqlite"):
    # SQLite cần tham số này khi dùng chung 1 connection cho nhiều thread
    connect_args = {"check_same_thread": False}

engine = create_engine(settings.DATABASE_URL, connect_args=connect_args, echo=False)
SessionLocal = sessionmaker(autocommit=False, autoflush=False, bind=engine)
Base = declarative_base()


def get_db():
    """Dependency dùng trong FastAPI để lấy 1 phiên làm việc (session) với DB."""
    db = SessionLocal()
    try:
        yield db
    finally:
        db.close()


def init_db():
    """Tạo toàn bộ bảng trong DB nếu chưa tồn tại. Gọi 1 lần lúc khởi động app."""
    from app import models  # import ở đây để tránh vòng lặp import
    Base.metadata.create_all(bind=engine)
