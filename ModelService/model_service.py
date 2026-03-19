import asyncio
import contextlib
import hashlib
import logging
import os
import re
import threading
import time
import uuid
from contextlib import asynccontextmanager
from datetime import datetime
from pathlib import Path
from typing import Optional

import torch
import uvicorn
from diffusers import ZImagePipeline
from fastapi import FastAPI, HTTPException
from fastapi.middleware.cors import CORSMiddleware
from fastapi.responses import FileResponse
from pydantic import BaseModel


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
MAX_CONCURRENT_GENERATIONS = max(1, read_int_env("MODEL_SERVICE_MAX_CONCURRENT_GENERATIONS", 1))
DEVICE = "cuda" if torch.cuda.is_available() else "cpu"
MODEL_DTYPE = select_model_dtype(DEVICE)
MODEL_DTYPE_NAME = str(MODEL_DTYPE).replace("torch.", "")
MAX_PROMPT_LENGTH = max(1, read_int_env("MODEL_SERVICE_MAX_PROMPT_LENGTH", 2000))
MAX_NEGATIVE_PROMPT_LENGTH = max(1, read_int_env("MODEL_SERVICE_MAX_NEGATIVE_PROMPT_LENGTH", 2000))
MAX_NUM_STEPS = max(1, read_int_env("MODEL_SERVICE_MAX_NUM_STEPS", 50))
MIN_IMAGE_SIDE = max(64, read_int_env("MODEL_SERVICE_MIN_IMAGE_SIDE", 256))
MAX_IMAGE_SIDE = max(MIN_IMAGE_SIDE, read_int_env("MODEL_SERVICE_MAX_IMAGE_SIDE", 1024))
IMAGE_SIDE_MULTIPLE = max(1, read_int_env("MODEL_SERVICE_IMAGE_SIDE_MULTIPLE", 8))
MAX_IMAGE_PIXELS = max(MIN_IMAGE_SIDE * MIN_IMAGE_SIDE, read_int_env("MODEL_SERVICE_MAX_IMAGE_PIXELS", 1024 * 1024))
TEMP_FILE_MAX_AGE_HOURS = max(1, read_int_env("MODEL_SERVICE_TEMP_FILE_MAX_AGE_HOURS", 24))
TEMP_FILE_CLEANUP_INTERVAL_SECONDS = max(0, read_int_env("MODEL_SERVICE_TEMP_FILE_CLEANUP_INTERVAL_SECONDS", 3600))
REQUEST_ID_PATTERN = re.compile(r"^[A-Za-z0-9][A-Za-z0-9_-]{0,63}$")

TEMP_PATH = Path(TEMP_DIR).resolve()
LOG_PATH = Path(LOG_DIR).resolve()

os.makedirs(TEMP_PATH, exist_ok=True)
os.makedirs(LOG_PATH, exist_ok=True)

logging.basicConfig(
    level=logging.INFO,
    format="%(asctime)s - %(name)s - %(levelname)s - %(message)s",
    handlers=[
        logging.FileHandler(LOG_PATH / "model_service.log", encoding="utf-8"),
        logging.StreamHandler()
    ]
)

logger = logging.getLogger(__name__)

if "*" in ALLOW_ORIGINS:
    logger.warning(
        "CORS allow_origins contains '*' — all origins accepted. "
        "Set MODEL_SERVICE_ALLOW_ORIGINS for production."
    )

PROMPT_MIN_LENGTH = 3
PROMPT_MAX_LENGTH = 1000
NEGATIVE_PROMPT_MAX_LENGTH = 500
MIN_IMAGE_SIZE = 512
MAX_IMAGE_SIZE = 2048
IMAGE_SIZE_STEP = 64
MIN_NUM_STEPS = 1
MAX_NUM_STEPS = 50
REQUEST_ID_PATTERN = re.compile(r"^[A-Za-z0-9][A-Za-z0-9._-]{0,127}$")


def resolve_temp_file(filename: str) -> Path:
    candidate = (TEMP_PATH / filename).resolve()
    if candidate.parent != TEMP_PATH or candidate.name != filename:
        raise HTTPException(status_code=400, detail="Invalid filename")
    return candidate


def prompt_fingerprint(prompt: str) -> str:
    return hashlib.sha256(prompt.encode("utf-8")).hexdigest()[:12]


def validate_generate_request(request: "GenerateRequest") -> "GenerateRequest":
    prompt = request.prompt.strip()
    negative_prompt = (request.negative_prompt or "").strip()
    request_id = (request.request_id or "").strip() or None

    if not prompt:
        raise HTTPException(status_code=400, detail="prompt must not be empty")
    if len(prompt) > MAX_PROMPT_LENGTH:
        raise HTTPException(status_code=400, detail=f"prompt exceeds max length {MAX_PROMPT_LENGTH}")
    if len(negative_prompt) > MAX_NEGATIVE_PROMPT_LENGTH:
        raise HTTPException(status_code=400, detail=f"negative_prompt exceeds max length {MAX_NEGATIVE_PROMPT_LENGTH}")
    if request.num_steps < 1 or request.num_steps > MAX_NUM_STEPS:
        raise HTTPException(status_code=400, detail=f"num_steps must be between 1 and {MAX_NUM_STEPS}")

    for field_name, value in (("height", request.height), ("width", request.width)):
        if value < MIN_IMAGE_SIDE or value > MAX_IMAGE_SIDE:
            raise HTTPException(
                status_code=400,
                detail=f"{field_name} must be between {MIN_IMAGE_SIDE} and {MAX_IMAGE_SIDE}",
            )
        if value % IMAGE_SIDE_MULTIPLE != 0:
            raise HTTPException(
                status_code=400,
                detail=f"{field_name} must be a multiple of {IMAGE_SIDE_MULTIPLE}",
            )

    if request.width * request.height > MAX_IMAGE_PIXELS:
        raise HTTPException(
            status_code=400,
            detail=f"image size exceeds max pixel budget {MAX_IMAGE_PIXELS}",
        )

    if request_id is not None and not REQUEST_ID_PATTERN.fullmatch(request_id):
        raise HTTPException(
            status_code=400,
            detail="request_id must match ^[A-Za-z0-9][A-Za-z0-9_-]{0,63}$",
        )

    return GenerateRequest(
        prompt=prompt,
        negative_prompt=negative_prompt,
        num_steps=request.num_steps,
        height=request.height,
        width=request.width,
        seed=request.seed,
        request_id=request_id,
    )


def cleanup_temp_files_once(max_age_hours: int) -> None:
    deleted_count = 0
    cutoff_time = time.time() - max_age_hours * 3600

    for filepath in TEMP_PATH.iterdir():
        if not filepath.is_file():
            continue
        if filepath.stat().st_mtime <= cutoff_time:
            filepath.unlink()
            deleted_count += 1

    if deleted_count > 0:
        logger.info("Deleted %s expired temp file(s)", deleted_count)


class GenerateRequest(BaseModel):
    prompt: str
    negative_prompt: Optional[str] = ""
    num_steps: int = 8
    height: int = 768
    width: int = 768
    seed: Optional[int] = None
    request_id: Optional[str] = None


class GenerateResponse(BaseModel):
    status: str
    request_id: str
    image_url: Optional[str] = None
    message: str
    timestamp: str
    generation_time: Optional[float] = None


def validate_generate_request(request: GenerateRequest) -> GenerateRequest:
    request.prompt = (request.prompt or "").strip()
    request.negative_prompt = (request.negative_prompt or "").strip()

    if len(request.prompt) < PROMPT_MIN_LENGTH or len(request.prompt) > PROMPT_MAX_LENGTH:
        raise HTTPException(
            status_code=400,
            detail=f"prompt length must be between {PROMPT_MIN_LENGTH} and {PROMPT_MAX_LENGTH} characters",
        )

    if len(request.negative_prompt) > NEGATIVE_PROMPT_MAX_LENGTH:
        raise HTTPException(
            status_code=400,
            detail=f"negative_prompt must be at most {NEGATIVE_PROMPT_MAX_LENGTH} characters",
        )

    if request.num_steps < MIN_NUM_STEPS or request.num_steps > MAX_NUM_STEPS:
        raise HTTPException(
            status_code=400,
            detail=f"num_steps must be between {MIN_NUM_STEPS} and {MAX_NUM_STEPS}",
        )

    for field_name in ("width", "height"):
        value = getattr(request, field_name)
        if value < MIN_IMAGE_SIZE or value > MAX_IMAGE_SIZE:
            raise HTTPException(
                status_code=400,
                detail=f"{field_name} must be between {MIN_IMAGE_SIZE} and {MAX_IMAGE_SIZE}",
            )
        if value % IMAGE_SIZE_STEP != 0:
            raise HTTPException(
                status_code=400,
                detail=f"{field_name} must be a multiple of {IMAGE_SIZE_STEP}",
            )

    if request.seed is not None and request.seed < 0:
        raise HTTPException(status_code=400, detail="seed must be greater than or equal to 0")

    if request.request_id is not None:
        request.request_id = request.request_id.strip()
        if not request.request_id:
            request.request_id = None
        elif not REQUEST_ID_PATTERN.fullmatch(request.request_id):
            raise HTTPException(
                status_code=400,
                detail="request_id may only contain letters, numbers, dot, underscore, and hyphen",
            )

    return request


class ZImageModelService:
    _instance = None
    _instance_lock = threading.Lock()

    def __new__(cls):
        with cls._instance_lock:
            if cls._instance is None:
                cls._instance = super(ZImageModelService, cls).__new__(cls)
        return cls._instance

    def __init__(self):
        if getattr(self, "_initialized", False):
            return

        self.is_loading = False
        self.active_generations = 0
        self.pipe = None
        self.last_error = ""
        self._load_lock = threading.Lock()
        self._generation_semaphore = threading.Semaphore(MAX_CONCURRENT_GENERATIONS)
        self._state_lock = threading.Lock()
        self._initialized = True

    def _set_loading(self, value: bool) -> None:
        with self._state_lock:
            self.is_loading = value

    def _set_last_error(self, value: str) -> None:
        with self._state_lock:
            self.last_error = value

    def _mark_generation_started(self) -> None:
        with self._state_lock:
            self.active_generations += 1

    def _mark_generation_finished(self) -> None:
        with self._state_lock:
            self.active_generations = max(0, self.active_generations - 1)

    def get_health_snapshot(self) -> dict:
        with self._state_lock:
            is_loading = self.is_loading
            active_generations = self.active_generations
            last_error = self.last_error

        model_loaded = self.pipe is not None
        is_generating = active_generations > 0

        if not model_loaded:
            status = "loading" if is_loading else "unhealthy"
        elif is_generating:
            status = "busy"
        else:
            status = "healthy"

        return {
            "status": status,
            "model_loaded": model_loaded,
            "is_loading": is_loading,
            "is_generating": is_generating,
            "active_generations": active_generations,
            "detail": last_error,
        }

    def initialize(self, force: bool = False) -> bool:
        return self.load_model(force=force)

    def load_model(self, force: bool = False) -> bool:
        if self.pipe is not None and not force:
            return True

        with self._load_lock:
            if self.pipe is not None and not force:
                return True

            self._set_loading(True)
            self._set_last_error("")
            logger.info("Loading model from %s", LOCAL_MODEL_PATH)

            try:
                self.pipe = ZImagePipeline.from_pretrained(
                    LOCAL_MODEL_PATH,
                    torch_dtype=MODEL_DTYPE,
                    local_files_only=True
                ).to(DEVICE)
                logger.info("Model loaded. Device: %s, dtype: %s", DEVICE, MODEL_DTYPE_NAME)
                return True
            except Exception as e:
                self.pipe = None
                self._set_last_error(str(e))
                logger.exception("Failed to load model")
                return False
            finally:
                self._set_loading(False)

    def generate_image(self, request: GenerateRequest) -> dict:
        if self.pipe is None:
            loaded = self.initialize()
            if not loaded or self.pipe is None:
                detail = self.get_health_snapshot().get("detail") or "model is unavailable"
                raise RuntimeError(f"Model not loaded: {detail}")

        with self._generation_semaphore:
            self._mark_generation_started()
            start_time = datetime.now()
            request_id = request.request_id or str(uuid.uuid4())

            try:
                self._set_last_error("")
                generator = None
                if request.seed is not None:
                    generator = torch.Generator(device=DEVICE).manual_seed(request.seed)

                logger.info(
                    "Generating image request_id=%s prompt_chars=%s prompt_hash=%s steps=%s size=%sx%s",
                    request_id,
                    len(request.prompt),
                    prompt_fingerprint(request.prompt),
                    request.num_steps,
                    request.width,
                    request.height,
                )

                result = self.pipe(
                    prompt=request.prompt,
                    negative_prompt=request.negative_prompt,
                    num_inference_steps=request.num_steps,
                    guidance_scale=0.0,
                    height=request.height,
                    width=request.width,
                    generator=generator,
                )

                image = result.images[0]

                end_time = datetime.now()
                generation_time = (end_time - start_time).total_seconds()

                filename = f"{request_id}.png"
                filepath = resolve_temp_file(filename)
                image.save(filepath)

                logger.info("Generated image successfully: request_id=%s, time=%s", request_id, generation_time)

                return {
                    "status": "success",
                    "request_id": request_id,
                    "image_url": f"/temp/{filename}",
                    "message": "Successfully generated image",
                    "generation_time": generation_time,
                    "timestamp": datetime.now().isoformat(),
                }

            except Exception as e:
                self._set_last_error(str(e))
                logger.exception("Failed to generate image for request_id=%s", request_id)
                raise
            finally:
                self._mark_generation_finished()


@asynccontextmanager
async def lifespan(_: FastAPI):
    cleanup_task = None
    if TEMP_FILE_CLEANUP_INTERVAL_SECONDS > 0:
        cleanup_task = asyncio.create_task(
            cleanup_temp_files(
                max_age_hours=TEMP_FILE_MAX_AGE_HOURS,
                interval_seconds=TEMP_FILE_CLEANUP_INTERVAL_SECONDS,
            )
        )
    else:
        logger.info("Temp file cleanup disabled")

    if not model_service.initialize():
        logger.warning("Model preload failed: %s", model_service.last_error or "unknown error")
    logger.info("Model service started completely")

    try:
        yield
    finally:
        if cleanup_task is not None:
            cleanup_task.cancel()
            with contextlib.suppress(asyncio.CancelledError):
                await cleanup_task


app = FastAPI(
    title="Z-Image Turbo Model Service",
    description="Z-Image Turbo Model Image Generator",
    version="1.0.0",
    lifespan=lifespan,
)

app.add_middleware(
    CORSMiddleware,
    allow_origins=ALLOW_ORIGINS,
    allow_credentials=False,
    allow_methods=["*"],
    allow_headers=["*"],
)

model_service = ZImageModelService()


@app.get("/")
async def root():
    health = model_service.get_health_snapshot()
    return {
        "service": "Z-Image Turbo Model Service",
        "status": health["status"],
        "model_loaded": health["model_loaded"],
        "device": DEVICE,
        "dtype": MODEL_DTYPE_NAME,
        "max_concurrent_generations": MAX_CONCURRENT_GENERATIONS,
    }


@app.get("/health")
async def health_check():
    health = model_service.get_health_snapshot()
    body = {
        "status": health["status"],
        "timestamp": datetime.now().isoformat(),
        "device": DEVICE,
        "dtype": MODEL_DTYPE_NAME,
        "model_loaded": health["model_loaded"],
        "is_loading": health["is_loading"],
        "is_generating": health["is_generating"],
        "active_generations": health["active_generations"],
        "max_concurrent_generations": MAX_CONCURRENT_GENERATIONS,
    }
    if health["detail"]:
        body["detail"] = health["detail"]
    return body


@app.post("/generate", response_model=GenerateResponse)
async def generate_image(request: GenerateRequest):
    try:
        validated_request = validate_generate_request(request)
        result = await asyncio.to_thread(model_service.generate_image, validated_request)
        return GenerateResponse(**result)
    except HTTPException:
        raise
    except RuntimeError as e:
        if str(e).startswith("Model not loaded:"):
            raise HTTPException(status_code=503, detail=str(e))
        logger.exception("Runtime error on /generate")
        raise HTTPException(status_code=500, detail=str(e))
    except Exception as e:
        logger.exception("Unhandled error on /generate")
        raise HTTPException(status_code=500, detail=str(e))


@app.get("/temp/{filename}")
async def get_temp_image(filename: str):
    filepath = resolve_temp_file(filename)
    if not filepath.exists():
        raise HTTPException(status_code=404, detail="Image not found")

    return FileResponse(filepath, media_type="image/png")


async def cleanup_temp_files(
    max_age_hours: int = TEMP_FILE_MAX_AGE_HOURS,
    interval_seconds: int = TEMP_FILE_CLEANUP_INTERVAL_SECONDS,
):
    while True:
        try:
            cleanup_temp_files_once(max_age_hours)
        except Exception:
            logger.exception("Failed to delete temp files")

        await asyncio.sleep(interval_seconds)


if __name__ == "__main__":
    logger.info("Model service started, port: %s", PORT)
    uvicorn.run(app, host="0.0.0.0", port=PORT, log_level="info")


