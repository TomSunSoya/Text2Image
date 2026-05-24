import asyncio
import contextlib
import logging
import re
from contextlib import asynccontextmanager
from datetime import datetime

import torch
from fastapi import FastAPI, HTTPException
from fastapi.middleware.cors import CORSMiddleware
from fastapi.responses import Response
from fastapi.responses import FileResponse
from prometheus_client import CONTENT_TYPE_LATEST, generate_latest

from .config import (
    ALLOW_ORIGINS,
    DEVICE,
    IMAGE_SIDE_MULTIPLE,
    MAX_CONCURRENT_GENERATIONS,
    MAX_IMAGE_PIXELS,
    MAX_IMAGE_SIDE,
    MAX_NEGATIVE_PROMPT_LENGTH,
    MAX_NUM_STEPS,
    MAX_PROMPT_LENGTH,
    MIN_IMAGE_SIDE,
    MIN_NUM_STEPS,
    MODEL_DTYPE_NAME,
    PROMPT_MIN_LENGTH,
    TEMP_FILE_CLEANUP_INTERVAL_SECONDS,
    TEMP_FILE_MAX_AGE_HOURS,
)
from .metrics import GENERATION_SECONDS, GENERATION_TOTAL, update_health_metrics
from .pipelines import ZImageModelService
from .schemas import GenerateRequest, GenerateResponse
from .storage import cleanup_temp_files_once, resolve_temp_file

logger = logging.getLogger(__name__)

REQUEST_ID_PATTERN = re.compile(r"^[A-Za-z0-9][A-Za-z0-9_-]{0,63}$")


def validate_generate_request(request: GenerateRequest) -> GenerateRequest:
    prompt = request.prompt.strip()
    negative_prompt = (request.negative_prompt or "").strip()
    request_id = (request.request_id or "").strip() or None

    if not prompt:
        raise HTTPException(status_code=400, detail="prompt must not be empty")
    if len(prompt) < PROMPT_MIN_LENGTH:
        raise HTTPException(
            status_code=400,
            detail=f"prompt length must be at least {PROMPT_MIN_LENGTH} characters",
        )
    if len(prompt) > MAX_PROMPT_LENGTH:
        raise HTTPException(
            status_code=400, detail=f"prompt exceeds max length {MAX_PROMPT_LENGTH}"
        )
    if len(negative_prompt) > MAX_NEGATIVE_PROMPT_LENGTH:
        raise HTTPException(
            status_code=400,
            detail=f"negative_prompt exceeds max length {MAX_NEGATIVE_PROMPT_LENGTH}",
        )
    if request.num_steps < MIN_NUM_STEPS or request.num_steps > MAX_NUM_STEPS:
        raise HTTPException(
            status_code=400,
            detail=f"num_steps must be between {MIN_NUM_STEPS} and {MAX_NUM_STEPS}",
        )

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

    if request.seed is not None and request.seed < 0:
        raise HTTPException(status_code=400, detail="seed must be greater than or equal to 0")

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


def build_lifespan(
    service: ZImageModelService,
    preload_model: bool,
    cleanup_interval_seconds: int,
):
    @asynccontextmanager
    async def lifespan(_: FastAPI):
        cleanup_task = None
        if cleanup_interval_seconds > 0:
            cleanup_task = asyncio.create_task(
                cleanup_temp_files(
                    max_age_hours=TEMP_FILE_MAX_AGE_HOURS,
                    interval_seconds=cleanup_interval_seconds,
                )
            )
        else:
            logger.info("Temp file cleanup disabled")

        if preload_model and not service.initialize():
            logger.warning("Model preload failed: %s", service.last_error or "unknown error")
        logger.info("Model service started completely")

        try:
            yield
        finally:
            if cleanup_task is not None:
                cleanup_task.cancel()
                with contextlib.suppress(asyncio.CancelledError):
                    await cleanup_task

    return lifespan


def create_app(
    service: ZImageModelService | None = None,
    *,
    preload_model: bool = True,
    cleanup_interval_seconds: int = TEMP_FILE_CLEANUP_INTERVAL_SECONDS,
) -> FastAPI:
    selected_service = service or ZImageModelService()

    app = FastAPI(
        title="Z-Image Turbo Model Service",
        description="Z-Image Turbo Model Image Generator",
        version="1.0.0",
        lifespan=build_lifespan(selected_service, preload_model, cleanup_interval_seconds),
    )
    app.state.model_service = selected_service

    app.add_middleware(
        CORSMiddleware,
        allow_origins=ALLOW_ORIGINS,
        allow_credentials=False,
        allow_methods=["*"],
        allow_headers=["*"],
    )

    @app.get("/")
    async def root():
        health = selected_service.get_health_snapshot()
        return {
            "service": "Z-Image Turbo Model Service",
            "status": health["status"],
            "model_loaded": health["model_loaded"],
            "active_kind": health["active_kind"],
            "device": DEVICE,
            "dtype": MODEL_DTYPE_NAME,
            "max_concurrent_generations": MAX_CONCURRENT_GENERATIONS,
        }

    @app.get("/health")
    async def health_check():
        health = selected_service.get_health_snapshot()
        update_health_metrics(health)
        body = {
            "status": health["status"],
            "timestamp": datetime.now().isoformat(),
            "device": DEVICE,
            "dtype": MODEL_DTYPE_NAME,
            "model_loaded": health["model_loaded"],
            "is_loading": health["is_loading"],
            "is_generating": health["is_generating"],
            "active_generations": health["active_generations"],
            "active_kind": health["active_kind"],
            "active_seconds": health["active_seconds"],
            "max_concurrent_generations": MAX_CONCURRENT_GENERATIONS,
            "busy_unhealthy_seconds": health["busy_unhealthy_seconds"],
        }
        if health["detail"]:
            body["detail"] = health["detail"]
        return body

    @app.get("/metrics")
    async def prometheus_metrics():
        update_health_metrics(selected_service.get_health_snapshot())
        return Response(generate_latest(), media_type=CONTENT_TYPE_LATEST)

    @app.post("/generate", response_model=GenerateResponse)
    async def generate_image(request: GenerateRequest):
        started_at = asyncio.get_running_loop().time()
        try:
            validated_request = validate_generate_request(request)
            result = await asyncio.to_thread(selected_service.generate_image, validated_request)
            GENERATION_TOTAL.labels(status="success").inc()
            GENERATION_SECONDS.observe(asyncio.get_running_loop().time() - started_at)
            return GenerateResponse(**result)
        except HTTPException:
            raise
        except torch.cuda.OutOfMemoryError as exc:
            GENERATION_TOTAL.labels(status="oom").inc()
            GENERATION_SECONDS.observe(asyncio.get_running_loop().time() - started_at)
            logger.exception("CUDA out of memory on /generate")
            raise HTTPException(status_code=503, detail=str(exc))
        except RuntimeError as exc:
            if str(exc).startswith("Model not loaded:"):
                GENERATION_TOTAL.labels(status="model_unavailable").inc()
                GENERATION_SECONDS.observe(asyncio.get_running_loop().time() - started_at)
                raise HTTPException(status_code=503, detail=str(exc))
            GENERATION_TOTAL.labels(status="runtime_error").inc()
            GENERATION_SECONDS.observe(asyncio.get_running_loop().time() - started_at)
            logger.exception("Runtime error on /generate")
            raise HTTPException(status_code=500, detail=str(exc))
        except Exception as exc:
            GENERATION_TOTAL.labels(status="error").inc()
            GENERATION_SECONDS.observe(asyncio.get_running_loop().time() - started_at)
            logger.exception("Unhandled error on /generate")
            raise HTTPException(status_code=500, detail=str(exc))

    @app.get("/temp/{filename}")
    async def get_temp_image(filename: str):
        filepath = resolve_temp_file(filename)
        if not filepath.exists():
            raise HTTPException(status_code=404, detail="Image not found")

        return FileResponse(filepath, media_type="image/png")

    return app


model_service = ZImageModelService()
app = create_app(model_service)
