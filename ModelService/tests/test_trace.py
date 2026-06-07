import logging

from model_service_app.trace import (
    REQUEST_ID_HEADER,
    get_request_id,
    install_request_id_log_factory,
    new_request_id,
    request_id_ctx,
    sanitize_request_id,
)


def test_sanitize_accepts_uuid_and_business_ids():
    assert sanitize_request_id("550e8400-e29b-41d4-a716-446655440000") == (
        "550e8400-e29b-41d4-a716-446655440000"
    )
    assert sanitize_request_id("req-123") == "req-123"
    assert sanitize_request_id("  trace.42:abc  ") == "trace.42:abc"


def test_sanitize_rejects_unsafe_values():
    assert sanitize_request_id(None) is None
    assert sanitize_request_id("") is None
    assert sanitize_request_id("   ") is None
    assert sanitize_request_id("has space") is None
    assert sanitize_request_id("inject\r\nSet-Cookie: x") is None
    assert sanitize_request_id("a" * 129) is None


def test_new_request_id_is_unique():
    assert new_request_id() != new_request_id()


def test_health_response_generates_request_id(client):
    response = client.get("/health")

    assert response.status_code == 200
    generated = response.headers.get(REQUEST_ID_HEADER)
    assert generated
    assert sanitize_request_id(generated) == generated


def test_request_id_is_echoed_when_supplied(client):
    response = client.get("/health", headers={REQUEST_ID_HEADER: "trace-abc-123"})

    assert response.headers.get(REQUEST_ID_HEADER) == "trace-abc-123"


def test_invalid_request_id_is_replaced(client):
    response = client.get("/health", headers={REQUEST_ID_HEADER: "bad value!!"})

    echoed = response.headers.get(REQUEST_ID_HEADER)
    assert echoed != "bad value!!"
    assert sanitize_request_id(echoed) == echoed


def test_log_records_carry_request_id_from_context(caplog):
    install_request_id_log_factory()
    token = request_id_ctx.set("ctx-trace-99")
    try:
        with caplog.at_level(logging.INFO):
            logging.getLogger("model_service_app.test").info("hello")
        assert get_request_id() == "ctx-trace-99"
        assert any(getattr(r, "request_id", None) == "ctx-trace-99" for r in caplog.records)
    finally:
        request_id_ctx.reset(token)
