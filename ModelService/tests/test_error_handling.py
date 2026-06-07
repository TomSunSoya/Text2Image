import torch

from .conftest import FakePipeline


def test_generate_returns_503_when_model_cannot_load(
    client, service, monkeypatch, valid_generate_payload
):
    service.pipe = None
    service.state.set_last_error("load failed")
    monkeypatch.setattr(service, "initialize", lambda force=False: False)

    response = client.post("/generate", json=valid_generate_payload)

    assert response.status_code == 503
    assert "Model not loaded" in response.json()["detail"]


def test_generate_returns_503_for_cuda_oom(client, service, valid_generate_payload):
    service.pipe = FakePipeline(error=torch.cuda.OutOfMemoryError("cuda oom"))

    response = client.post("/generate", json=valid_generate_payload)

    assert response.status_code == 503
    assert "cuda oom" in response.json()["detail"]


def test_generate_returns_500_for_unhandled_pipeline_error(client, service, valid_generate_payload):
    service.pipe = FakePipeline(error=ValueError("pipeline failed"))

    response = client.post("/generate", json=valid_generate_payload)

    assert response.status_code == 500
    assert "pipeline failed" in response.json()["detail"]
