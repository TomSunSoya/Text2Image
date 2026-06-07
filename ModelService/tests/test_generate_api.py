def test_generate_happy_path_returns_image_url(client, service, valid_generate_payload):
    response = client.post("/generate", json=valid_generate_payload)

    assert response.status_code == 200
    body = response.json()
    assert body["status"] == "success"
    assert body["request_id"] == "req-123"
    assert body["image_url"] == "/temp/req-123.png"
    assert service.pipe.calls[0]["prompt"] == valid_generate_payload["prompt"]


def test_generate_missing_prompt_returns_422(client):
    response = client.post("/generate", json={"num_steps": 4})

    assert response.status_code == 422


def test_generate_empty_prompt_returns_400(client, valid_generate_payload):
    valid_generate_payload["prompt"] = "   "

    response = client.post("/generate", json=valid_generate_payload)

    assert response.status_code == 400
    assert response.json()["detail"] == "prompt must not be empty"


def test_generate_short_prompt_returns_400(client, valid_generate_payload):
    valid_generate_payload["prompt"] = "hi"

    response = client.post("/generate", json=valid_generate_payload)

    assert response.status_code == 400
    assert "at least" in response.json()["detail"]


def test_generate_too_long_prompt_returns_400(client, valid_generate_payload):
    valid_generate_payload["prompt"] = "x" * 2001

    response = client.post("/generate", json=valid_generate_payload)

    assert response.status_code == 400
    assert "exceeds max length" in response.json()["detail"]


def test_generate_rejects_invalid_image_size(client, valid_generate_payload):
    valid_generate_payload["height"] = 258

    response = client.post("/generate", json=valid_generate_payload)

    assert response.status_code == 400
    assert "multiple" in response.json()["detail"]


def test_generate_rejects_negative_seed(client, valid_generate_payload):
    valid_generate_payload["seed"] = -1

    response = client.post("/generate", json=valid_generate_payload)

    assert response.status_code == 400
    assert "seed" in response.json()["detail"]


def test_generate_rejects_invalid_request_id(client, valid_generate_payload):
    valid_generate_payload["request_id"] = "../bad"

    response = client.post("/generate", json=valid_generate_payload)

    assert response.status_code == 400
    assert "request_id" in response.json()["detail"]
