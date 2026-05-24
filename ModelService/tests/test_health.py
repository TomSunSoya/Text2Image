import time

from model_service_app.config import BUSY_UNHEALTHY_SECONDS


def test_root_reports_healthy_loaded_service(client):
    response = client.get("/")

    assert response.status_code == 200
    body = response.json()
    assert body["status"] == "healthy"
    assert body["model_loaded"] is True
    assert body["active_kind"] == "none"


def test_health_reports_loading_when_model_missing(client, service):
    service.pipe = None
    service.state.set_loading(True)

    response = client.get("/health")

    assert response.status_code == 200
    body = response.json()
    assert body["status"] == "loading"
    assert body["model_loaded"] is False
    assert body["is_loading"] is True


def test_health_reports_busy_generation(client, service):
    service.state.mark_generation_started("generate")

    response = client.get("/health")

    assert response.status_code == 200
    body = response.json()
    assert body["status"] == "busy"
    assert body["active_kind"] == "generate"
    assert body["active_generations"] == 1


def test_health_reports_unhealthy_when_generation_exceeds_threshold(client, service):
    with service.state._state_lock:
        service.state._active_generation_starts = [time.monotonic() - BUSY_UNHEALTHY_SECONDS - 2]
        service.state.active_generations = 1
        service.state.active_kind = "generate"

    response = client.get("/health")

    assert response.status_code == 200
    body = response.json()
    assert body["status"] == "unhealthy"
    assert "exceeded" in body["detail"]


def test_metrics_endpoint_exposes_prometheus_text(client):
    response = client.get("/metrics")

    assert response.status_code == 200
    assert "text/plain" in response.headers["content-type"]
    assert "model_service_active_generations" in response.text
    assert "model_service_health_status" in response.text
