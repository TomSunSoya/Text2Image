# OpenAPI / Swagger

The Backend API is described in `docs/openapi.yaml`.

Preview it with Swagger UI:

```powershell
docker run --rm -p 8088:8080 `
  -e SWAGGER_JSON=/openapi.yaml `
  -v "${PWD}\docs\openapi.yaml:/openapi.yaml:ro" `
  swaggerapi/swagger-ui
```

Then open `http://127.0.0.1:8088`.

For browser or gateway tracing, every Backend HTTP response echoes `X-Request-Id`. Clients may
provide a safe request id with characters `[A-Za-z0-9._:-]` up to 128 characters; unsafe values are
replaced by the server.
