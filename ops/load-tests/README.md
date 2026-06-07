# Load Testing Runbook

Use these scripts to find the single-replica Backend upper bound before raising replicas or worker
counts. Record QPS, p95/p99 latency, HTTP error rate, Backend CPU/memory, MySQL connections, Redis
latency, and ModelService health state for every run.

## k6

Health-only baseline:

```powershell
k6 run .\ops\load-tests\k6_backend.js
```

Authenticated read paths:

```powershell
$env:BASE_URL = "http://127.0.0.1:8080"
$env:LOGIN_USERNAME = "load-user"
$env:LOGIN_PASSWORD = "load-password"
$env:VUS = "20"
$env:DURATION = "2m"
k6 run .\ops\load-tests\k6_backend.js
```

Task creation is intentionally opt-in because it can consume GPU/model-service capacity:

```powershell
$env:ENABLE_CREATE = "true"
$env:CREATE_RATE_SLEEP_MS = "1000"
k6 run .\ops\load-tests\k6_backend.js
```

## wrk

Health endpoint:

```powershell
wrk -t4 -c64 -d60s -s .\ops\load-tests\wrk_health.lua http://127.0.0.1:8080
```

Authenticated list endpoint:

```powershell
wrk -t4 -c64 -d60s -H "Authorization: Bearer <token>" `
  -s .\ops\load-tests\wrk_images_list.lua http://127.0.0.1:8080
```

## Interpreting DB Pool Results

For production compose, check:

```text
BACKEND_REPLICAS * DB_POOL_SIZE <= MySQL max_connections - reserved_connections
DB_POOL_SIZE >= BACKEND_THREADS + TASK_ENGINE_WORKERS + 1
```

The extra `1` covers the process-level DB session used during startup and health checks. The
Backend exposes `db_pool_configured_connections`, `db_pool_active_connections`, and `db_pool_idle`
in `/metrics`; treat them as per-replica capacity gauges for this thread-local session design.

Increase one variable at a time. Stop increasing load when p95 or p99 latency rises sharply, 5xx
responses appear, MySQL connection errors appear, Redis queue depth grows without draining, or the
ModelService health endpoint stays `busy`/`unhealthy` beyond the configured threshold.
