def test_get_temp_image_returns_file(client, service, valid_generate_payload):
    client.post("/generate", json=valid_generate_payload)

    response = client.get("/temp/req-123.png")

    assert response.status_code == 200
    assert response.content == b"fake-png"


def test_get_temp_image_rejects_path_traversal(client):
    response = client.get("/temp/%2e%2e%2Fsecret.png")

    assert response.status_code == 404


def test_get_temp_image_returns_404_for_missing_file(client):
    response = client.get("/temp/missing.png")

    assert response.status_code == 404
