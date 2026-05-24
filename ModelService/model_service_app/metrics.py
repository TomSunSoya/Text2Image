import torch
from prometheus_client import Counter, Gauge, Histogram

GENERATION_SECONDS = Histogram(
    "model_service_generation_seconds",
    "ModelService image generation duration.",
    buckets=(0.1, 0.5, 1, 2, 5, 10, 30, 60),
)
GENERATION_TOTAL = Counter(
    "model_service_generation_total",
    "ModelService generation attempts by final status.",
    ["status"],
)
ACTIVE_GENERATIONS = Gauge(
    "model_service_active_generations",
    "ModelService active generation count.",
)
HEALTH_STATUS = Gauge(
    "model_service_health_status",
    "ModelService health state as one-hot gauges.",
    ["status"],
)
GPU_MEMORY_ALLOCATED_BYTES = Gauge(
    "model_service_gpu_memory_allocated_bytes",
    "CUDA memory allocated by ModelService.",
)


def update_health_metrics(health: dict) -> None:
    ACTIVE_GENERATIONS.set(health.get("active_generations", 0))

    current_status = health.get("status", "unknown")
    for status in ("healthy", "busy", "loading", "unhealthy", "unknown"):
        HEALTH_STATUS.labels(status=status).set(1 if status == current_status else 0)

    if torch.cuda.is_available():
        GPU_MEMORY_ALLOCATED_BYTES.set(torch.cuda.memory_allocated())
    else:
        GPU_MEMORY_ALLOCATED_BYTES.set(0)
