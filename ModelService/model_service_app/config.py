import logging
import os
from pathlib import Path

import torch


def read_int_env(name: str, fallback: int) -> int:
    raw_value = os.getenv(name, "").strip()
    if not raw_value:
        return fallback
    try:
        return int(raw_value)
    except ValueError:
        logging.warning("Invalid integer env %s=%s, fallback=%s", name, raw_value, fallback)
        return fallback


def read_list_env(name: str, fallback: list[str]) -> list[str]:
    raw_value = os.getenv(name, "").strip()
    if not raw_value:
        return fallback

    parsed = [item.strip() for item in raw_value.split(",") if item.strip()]
    return parsed or fallback


def is_local_cors_origin(origin: str) -> bool:
    normalized = origin.lower()
    return (
        "localhost" in normalized
        or "127.0.0.1" in normalized
        or "[::1]" in normalized
        or "0.0.0.0" in normalized
    )


def select_model_dtype(device: str) -> torch.dtype:
    if device == "cuda":
        if hasattr(torch.cuda, "is_bf16_supported") and torch.cuda.is_bf16_supported():
            return torch.bfloat16
        return torch.float16
    return torch.float32


LOCAL_MODEL_PATH = os.getenv("MODEL_PATH", "./models/Z-Image-Turbo")
PORT = read_int_env("MODEL_SERVICE_PORT", 8081)
LOG_DIR = os.getenv("MODEL_SERVICE_LOG_DIR", "./logs")
TEMP_DIR = os.getenv("MODEL_SERVICE_TEMP_DIR", "./temp")
ALLOW_ORIGINS = read_list_env("MODEL_SERVICE_ALLOW_ORIGINS", ["http://localhost:3000"])
ENVIRONMENT = os.getenv("ENV", "development").strip().lower()
MAX_CONCURRENT_GENERATIONS = max(1, read_int_env("MODEL_SERVICE_MAX_CONCURRENT_GENERATIONS", 1))
DEVICE = "cuda" if torch.cuda.is_available() else "cpu"
MODEL_DTYPE = select_model_dtype(DEVICE)
MODEL_DTYPE_NAME = str(MODEL_DTYPE).replace("torch.", "")
PROMPT_MIN_LENGTH = 3
MAX_PROMPT_LENGTH = max(1, read_int_env("MODEL_SERVICE_MAX_PROMPT_LENGTH", 2000))
MAX_NEGATIVE_PROMPT_LENGTH = max(1, read_int_env("MODEL_SERVICE_MAX_NEGATIVE_PROMPT_LENGTH", 2000))
BUSY_UNHEALTHY_SECONDS = max(1, read_int_env("MODEL_SERVICE_BUSY_UNHEALTHY_SECONDS", 60))
MIN_NUM_STEPS = 1
MAX_NUM_STEPS = max(1, read_int_env("MODEL_SERVICE_MAX_NUM_STEPS", 50))
MIN_IMAGE_SIDE = max(64, read_int_env("MODEL_SERVICE_MIN_IMAGE_SIDE", 256))
MAX_IMAGE_SIDE = max(MIN_IMAGE_SIDE, read_int_env("MODEL_SERVICE_MAX_IMAGE_SIDE", 1024))
IMAGE_SIDE_MULTIPLE = max(1, read_int_env("MODEL_SERVICE_IMAGE_SIDE_MULTIPLE", 8))
MAX_IMAGE_PIXELS = max(
    MIN_IMAGE_SIDE * MIN_IMAGE_SIDE, read_int_env("MODEL_SERVICE_MAX_IMAGE_PIXELS", 1024 * 1024)
)
TEMP_FILE_MAX_AGE_HOURS = max(1, read_int_env("MODEL_SERVICE_TEMP_FILE_MAX_AGE_HOURS", 24))
TEMP_FILE_CLEANUP_INTERVAL_SECONDS = max(
    0, read_int_env("MODEL_SERVICE_TEMP_FILE_CLEANUP_INTERVAL_SECONDS", 3600)
)

TEMP_PATH = Path(TEMP_DIR).resolve()
LOG_PATH = Path(LOG_DIR).resolve()

os.makedirs(TEMP_PATH, exist_ok=True)
os.makedirs(LOG_PATH, exist_ok=True)

logging.basicConfig(
    level=logging.INFO,
    format="%(asctime)s - %(name)s - %(levelname)s - %(message)s",
    handlers=[
        logging.FileHandler(LOG_PATH / "model_service.log", encoding="utf-8"),
        logging.StreamHandler(),
    ],
)

logger = logging.getLogger(__name__)

if "*" in ALLOW_ORIGINS:
    if ENVIRONMENT == "production":
        raise RuntimeError("MODEL_SERVICE_ALLOW_ORIGINS must not contain '*' in production")
    logger.warning(
        "CORS allow_origins contains '*' - all origins accepted. "
        "Set MODEL_SERVICE_ALLOW_ORIGINS for production."
    )

if ENVIRONMENT == "production":
    local_origins = [origin for origin in ALLOW_ORIGINS if is_local_cors_origin(origin)]
    if local_origins:
        raise RuntimeError(
            "MODEL_SERVICE_ALLOW_ORIGINS contains local origins in production: "
            + ", ".join(local_origins)
        )
