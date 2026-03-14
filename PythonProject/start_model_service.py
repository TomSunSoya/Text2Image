#!/usr/bin/env python3
import os
import subprocess
import sys


DEFAULT_PORT = "8081"


def read_bool_env(name: str, fallback: bool = False) -> bool:
    raw_value = os.getenv(name)
    if raw_value is None:
        return fallback

    value = raw_value.strip().lower()
    return value in {"1", "true", "yes", "on"}


def start_service():
    print("=" * 50)
    print("Z-Image Turbo Model Service")
    print("=" * 50)

    try:
        import torch

        cuda_available = torch.cuda.is_available()
        print("Cuda available?", cuda_available)
        if cuda_available:
            print(f"GPU device: {torch.cuda.get_device_name(0)}")
            print(f"Display Memory: {torch.cuda.get_device_properties(0).total_memory / 1024**3:.2f} GB")
    except ImportError:
        print("WARNING: PyTorch not installed")

    port = os.getenv("MODEL_SERVICE_PORT", DEFAULT_PORT)
    enable_reload = read_bool_env("MODEL_SERVICE_RELOAD", False)

    print(f"\nStarting the model service on port {port}...")
    print(f"Auto-reload enabled: {enable_reload}")
    os.chdir(os.path.dirname(os.path.abspath(__file__)))

    command = [
        sys.executable,
        "-m",
        "uvicorn",
        "model_service:app",
        "--host",
        "0.0.0.0",
        "--port",
        port,
    ]
    if enable_reload:
        command.append("--reload")

    subprocess.run(command)


if __name__ == "__main__":
    start_service()

