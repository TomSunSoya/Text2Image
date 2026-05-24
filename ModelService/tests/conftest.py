import os
import re
import shutil
from pathlib import Path
from types import SimpleNamespace

import pytest
from fastapi.testclient import TestClient

os.environ.setdefault("ENV", "testing")
os.environ.setdefault("MODEL_SERVICE_ALLOW_ORIGINS", "http://testserver")

from model_service_app import storage
from model_service_app.api import create_app
from model_service_app.pipelines import ZImageModelService


class FakeImage:
    def save(self, path):
        path.write_bytes(b"fake-png")


class FakePipeline:
    def __init__(self, result=None, error=None):
        self.result = result or SimpleNamespace(images=[FakeImage()])
        self.error = error
        self.calls = []

    def __call__(self, **kwargs):
        self.calls.append(kwargs)
        if self.error is not None:
            raise self.error
        return self.result


@pytest.fixture
def model_temp_dir(request):
    safe_name = re.sub(r"[^A-Za-z0-9_.-]+", "_", request.node.nodeid)
    path = Path(__file__).resolve().parents[1] / ".pytest-temp" / safe_name
    shutil.rmtree(path, ignore_errors=True)
    path.mkdir(parents=True, exist_ok=True)
    try:
        yield path.resolve()
    finally:
        shutil.rmtree(path, ignore_errors=True)


@pytest.fixture
def service(monkeypatch, model_temp_dir):
    monkeypatch.setattr(storage, "TEMP_PATH", model_temp_dir)
    with ZImageModelService._instance_lock:
        ZImageModelService._instance = None

    instance = ZImageModelService()
    instance.pipe = FakePipeline()
    return instance


@pytest.fixture
def client(service):
    app = create_app(service, preload_model=False, cleanup_interval_seconds=0)
    with TestClient(app, raise_server_exceptions=False) as test_client:
        yield test_client


@pytest.fixture
def valid_generate_payload():
    return {
        "prompt": "A quiet observatory above the clouds",
        "negative_prompt": "",
        "num_steps": 4,
        "height": 256,
        "width": 256,
        "request_id": "req-123",
    }
