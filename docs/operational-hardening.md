# Operational Hardening Notes

## Request Trace IDs

Backend and ModelService both support `X-Request-Id`.

- Backend reads a safe incoming header or generates a UUIDv4 trace id.
- Backend echoes the trace id on every HTTP response.
- Backend forwards the trace id to ModelService `/generate`.
- ModelService echoes the trace id and adds it to log records through a context variable.

Client-supplied ids are accepted only when they match `[A-Za-z0-9._:-]` and are at most 128
characters. Unsafe values are replaced so header and log injection attempts do not propagate.

The image task `request_id` body field is a business idempotency key. It is separate from the
`X-Request-Id` trace header, even though the Backend uses the task request id as the outbound trace
id when calling ModelService for `/generate`.

## Audit Logs

Backend writes structured JSON audit events through normal logs with the prefix `audit`.

Covered events:

- `auth.register`
- `auth.login`
- `auth.refresh`
- `auth.logout`
- `auth.me`
- `auth.password_change`
- `image.create`
- `image.delete`
- `image.cancel`
- `image.retry`
- `metrics.cache.read`

Audit fields are intentionally small:

- `type`
- `timestamp`
- `event`
- `outcome`
- `request_id`
- `client_ip`
- `resource_id`, when known
- `status`
- `user_id`, when known

The audit logger does not record passwords, access tokens, refresh tokens, prompts, image bytes,
base64 payloads, generated image contents, or full request bodies.

## DB Session Budget

The current Backend uses thread-local MySQL X sessions rather than a borrow/return pool. Treat
`database.pool_size` / `DB_POOL_SIZE` as the per-replica session budget and operational capacity
guardrail.

Recommended sizing:

```text
DB_POOL_SIZE >= BACKEND_THREADS + TASK_ENGINE_WORKERS + 1
BACKEND_REPLICAS * DB_POOL_SIZE <= MySQL max_connections - reserved_connections
```

The extra `1` covers the process-level session used by startup and health paths. Backend logs a
warning when `DB_POOL_SIZE` is below the estimated per-replica demand and exposes the configured
budget as `db_pool_configured_connections` on `/metrics`.

## LoRA Training Image

The ModelService runtime image intentionally does not copy `train_lora.py`.

Build a training image only when needed:

```powershell
docker build -f .\ModelService\Dockerfile.train -t zimage-lora-trainer .\ModelService
```

Mount model weights, training images, and an output directory when running that image. Keep training
artifacts out of the inference runtime image and out of Git.

## Load Testing

See `ops/load-tests/README.md` for k6 and wrk commands. Run health/read-path baselines first, then
enable task creation only when the ModelService and GPU capacity are intentionally under test.
