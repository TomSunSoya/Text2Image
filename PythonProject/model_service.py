import asyncio
import io
import logging
import os
import threading
import uuid
from datetime import datetime
from typing import Optional

import torch
import uvicorn
from diffusers import ZImagePipeline
from fastapi import FastAPI, HTTPException
from fastapi.middleware.cors import CORSMiddleware
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


LOCAL_MODEL_PATH = os.getenv(
    "MODEL_PATH",
    "C:/Users/pc1/.cache/modelscope/hub/models/Tongyi-MAI/Z-Image-Turbo"
)
PORT = read_int_env("MODEL_SERVICE_PORT", 8081)
LOG_DIR = os.getenv("MODEL_SERVICE_LOG_DIR", "./logs")
TEMP_DIR = os.getenv("MODEL_SERVICE_TEMP_DIR", "./temp")
ALLOW_ORIGINS = read_list_env("MODEL_SERVICE_ALLOW_ORIGINS", ["*"])
DEVICE = "cuda" if torch.cuda.is_available() else "cpu"

os.makedirs(TEMP_DIR, exist_ok=True)
os.makedirs(LOG_DIR, exist_ok=True)

logging.basicConfig(
    level=logging.INFO,
    format="%(asctime)s - %(name)s - %(levelname)s - %(message)s",
    handlers=[
        logging.FileHandler(os.path.join(LOG_DIR, "model_service.log")),
        logging.StreamHandler()
    ]
)

logger = logging.getLogger(__name__)


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
        self.pipe = None
        self.last_error = ""
        self._load_lock = threading.Lock()
        self._initialized = True

    def initialize(self, force: bool = False) -> bool:
        return self.load_model(force=force)

    def load_model(self, force: bool = False) -> bool:
        if self.pipe is not None and not force:
            return True

        with self._load_lock:
            if self.pipe is not None and not force:
                return True

            self.is_loading = True
            self.last_error = ""
            logger.info(f"Loading model from {LOCAL_MODEL_PATH}")

            try:
                self.pipe = ZImagePipeline.from_pretrained(
                    LOCAL_MODEL_PATH,
                    torch_dtype=torch.bfloat16,
                    local_files_only=True
                ).to(DEVICE)
                logger.info(f"Model loaded! Device: {DEVICE}")
                return True
            except Exception as e:
                self.pipe = None
                self.last_error = str(e)
                logger.error(f"Failed to load model: {e}")
                return False
            finally:
                self.is_loading = False

    def generate_image(self, request: GenerateRequest) -> dict:
        if self.pipe is None:
            raise RuntimeError("pipe is not initialized")

        start_time = datetime.now()
        request_id = request.request_id or str(uuid.uuid4())

        try:
            generator = None
            if request.seed is not None:
                generator = torch.Generator(device=DEVICE).manual_seed(request.seed)

            logger.info(f"Generating image for {request.prompt[:50]}...")

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
            filepath = os.path.join(TEMP_DIR, filename)
            image.save(filepath)

            buffered = io.BytesIO()
            image.save(buffered, format="PNG")

            logger.info(f"Generate image successfully: {request_id}, time: {generation_time}")

            return {
                "status": "success",
                "request_id": request_id,
                "image_url": f"/temp/{filename}",
                "message": "Successfully generated image",
                "generation_time": generation_time,
                "timestamp": datetime.now().isoformat(),
            }

        except Exception as e:
            logger.error(f"Failed to generate image: {e}")
            raise


app = FastAPI(
    title="Z-Image Turbo Model Service",
    description="Z-Image Turbo Model Image Generator",
    version="1.0.0",
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
    return {
        "service": "Z-Image Turbo Model Service",
        "status": "success",
        "model_loaded": model_service.pipe is not None,
        "device": DEVICE,
    }


@app.get("/health")
async def health_check():
    body = {
        "status": "healthy" if model_service.pipe is not None else "unhealthy",
        "timestamp": datetime.now().isoformat(),
        "device": DEVICE,
        "model_loaded": model_service.pipe is not None,
        "is_loading": model_service.is_loading,
    }
    if model_service.last_error:
        body["detail"] = model_service.last_error
    return body


@app.post("/generate", response_model=GenerateResponse)
async def generate_image(request: GenerateRequest):
    try:
        if model_service.pipe is None:
            loaded = model_service.initialize(force=True)
            if not loaded or model_service.pipe is None:
                detail = model_service.last_error or "model is unavailable"
                raise HTTPException(status_code=503, detail=f"Model not loaded: {detail}")

        result = model_service.generate_image(request)
        return GenerateResponse(**result)
    except HTTPException:
        raise
    except Exception as e:
        logger.error(f"Error on API: {e}")
        raise HTTPException(status_code=500, detail=str(e))


@app.get("/temp/{filename}")
async def get_temp_image(filename: str):
    filepath = os.path.join(TEMP_DIR, filename)
    if not os.path.exists(filepath):
        raise HTTPException(status_code=404, detail="Image not found")

    from fastapi.responses import FileResponse
    return FileResponse(filepath, media_type="image/png")


async def cleanup_temp_files(max_age_hours: int = 24):
    import time

    while True:
        try:
            current_time = time.time()
            for filename in os.listdir(TEMP_DIR):
                filepath = os.path.join(TEMP_DIR, filename)
                if os.path.isfile(filepath):
                    file_age = current_time - os.path.getmtime(filepath)
                    if file_age > max_age_hours * 3600:
                        os.remove(filepath)
                        logger.info(f"Deleted temp file: {filepath}")
        except Exception as e:
            logger.error(f"Failed to delete temp file: {e}")

        await asyncio.sleep(3600)


@app.on_event("startup")
async def startup_event():
    asyncio.create_task(cleanup_temp_files())
    if not model_service.initialize():
        logger.warning(f"Model preload failed: {model_service.last_error or 'unknown error'}")
    logger.info("Model service started completely")


if __name__ == "__main__":
    logger.info(f"Model service started, port: {PORT}")
    uvicorn.run(app, host="0.0.0.0", port=PORT, log_level="info")


