import hashlib
import logging
import threading
import uuid
from datetime import datetime

import torch
from diffusers import ZImagePipeline

from .config import (
    BUSY_UNHEALTHY_SECONDS,
    DEVICE,
    LOCAL_MODEL_PATH,
    MAX_CONCURRENT_GENERATIONS,
    MODEL_DTYPE,
    MODEL_DTYPE_NAME,
)
from .schemas import GenerateRequest
from .state import GenerationState
from .storage import resolve_temp_file

logger = logging.getLogger(__name__)


def prompt_fingerprint(prompt: str) -> str:
    return hashlib.sha256(prompt.encode("utf-8")).hexdigest()[:12]


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

        self.pipe = None
        self.state = GenerationState()
        self._load_lock = threading.Lock()
        self._generation_semaphore = threading.Semaphore(MAX_CONCURRENT_GENERATIONS)
        self._initialized = True

    @property
    def last_error(self) -> str:
        return self.state.last_error

    def get_health_snapshot(self) -> dict:
        return self.state.snapshot(
            model_loaded=self.pipe is not None,
            busy_unhealthy_seconds=BUSY_UNHEALTHY_SECONDS,
        )

    def initialize(self, force: bool = False) -> bool:
        return self.load_model(force=force)

    def load_model(self, force: bool = False) -> bool:
        if self.pipe is not None and not force:
            return True

        with self._load_lock:
            if self.pipe is not None and not force:
                return True

            self.state.set_loading(True)
            self.state.set_last_error("")
            logger.info("Loading model from %s", LOCAL_MODEL_PATH)

            try:
                self.pipe = ZImagePipeline.from_pretrained(
                    LOCAL_MODEL_PATH, torch_dtype=MODEL_DTYPE, local_files_only=True
                ).to(DEVICE)
                logger.info("Model loaded. Device: %s, dtype: %s", DEVICE, MODEL_DTYPE_NAME)
                return True
            except Exception as exc:
                self.pipe = None
                self.state.set_last_error(str(exc))
                logger.exception("Failed to load model")
                return False
            finally:
                self.state.set_loading(False)

    def generate_image(self, request: GenerateRequest) -> dict:
        if self.pipe is None:
            loaded = self.initialize()
            if not loaded or self.pipe is None:
                detail = self.get_health_snapshot().get("detail") or "model is unavailable"
                raise RuntimeError(f"Model not loaded: {detail}")

        with self._generation_semaphore:
            self.state.mark_generation_started("generate")
            start_time = datetime.now()
            request_id = request.request_id or str(uuid.uuid4())

            try:
                self.state.set_last_error("")
                generator = None
                if request.seed is not None:
                    generator = torch.Generator(device=DEVICE).manual_seed(request.seed)

                logger.info(
                    "Generating image request_id=%s prompt_chars=%s prompt_hash=%s "
                    "steps=%s size=%sx%s",
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
                generation_time = (datetime.now() - start_time).total_seconds()

                filename = f"{request_id}.png"
                filepath = resolve_temp_file(filename)
                image.save(filepath)

                logger.info(
                    "Generated image successfully: request_id=%s, time=%s",
                    request_id,
                    generation_time,
                )

                return {
                    "status": "success",
                    "request_id": request_id,
                    "image_url": f"/temp/{filename}",
                    "message": "Successfully generated image",
                    "generation_time": generation_time,
                    "timestamp": datetime.now().isoformat(),
                }

            except Exception as exc:
                self.state.set_last_error(str(exc))
                logger.exception("Failed to generate image for request_id=%s", request_id)
                raise
            finally:
                self.state.mark_generation_finished()
