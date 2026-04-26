# ZImage Workspace

## 1. Scope

This repository currently includes the main delivery path:

- `ZImageFrontend/`: frontend, built with Vue 3 + Vite + Element Plus
- `Backend/`: backend API and task orchestration, built with C++20 + Drogon + MySQL
- `ModelService/`: model execution service, built with FastAPI + Diffusers

## 2. Architecture

The system is split into three layers:

1. `ZImageFrontend` only talks to `Backend`
2. `Backend` owns auth, task lifecycle, history, storage metadata, and model-service orchestration
3. `ModelService` focuses on model execution and execution health

Default ports:

- frontend: `80` (nginx in Docker) / `3000` (Vite dev server)
- backend: `8080`
- model service: `8081`

## 3. Main Flow

Image generation currently works like this:

1. frontend sends `POST /api/images`
2. backend creates a task in MySQL with status `queued` and enqueues the task ID to Redis (if available)
3. backend workers dequeue from Redis (or fall back to MySQL polling) and claim the task
4. workers call `POST /generate` on `ModelService`
5. `ModelService` generates the image, stores it in MinIO, and returns result metadata
6. backend persists task status, timing, storage metadata, and history
7. frontend receives real-time status updates via WebSocket (`/api/ws/images`) and lazily loads the final image

## 4. Current Capabilities

### 4.1 Auth

- register: `POST /api/auth/register`
- login: `POST /api/auth/login`
- authenticated image APIs use Bearer token

### 4.2 Image Tasks

- create task: `POST /api/images`
- list current user tasks: `GET /api/images/my-list`
- list by status: `GET /api/images/my-list/status/{status}`
- get task detail: `GET /api/images/{id}`
- get task status: `GET /api/images/{id}/status`
- cancel task: `POST /api/images/{id}/cancel`
- retry task: `POST /api/images/{id}/retry`
- download protected image binary: `GET /api/images/{id}/binary`
- delete task record: `DELETE /api/images/{id}`

Canonical task statuses currently used across the stack:

- `queued`
- `pending`
- `generating`
- `success`
- `failed`
- `cancelled`
- `timeout`

### 4.3 Health

- backend liveness: `GET /health`
- backend proxy model health: `GET /api/images/health`
- model service health: `GET http://<model-service-host>:8081/health`

`ModelService` health now distinguishes:

- `healthy`: model loaded and idle
- `busy`: model loaded and currently generating
- `loading`: process is alive but model is still loading
- `unhealthy`: model unavailable or failed to load

## 5. Repository Layout

- `ZImageFrontend/src/`: pages, components, router, Pinia stores, API wrappers
- `Backend/src/controllers/`: HTTP controllers
- `Backend/src/services/`: business logic, task engine, external-service calls
- `Backend/src/database/`: MySQL access and repositories
- `Backend/src/models/`: task and storage-related data models
- `ModelService/model_service.py`: FastAPI model-service entrypoint
- `ModelService/main.py`: local standalone model script
- `docker-compose.yml`: service orchestration (MySQL, Redis, MinIO, Backend, ModelService, Frontend)
- `docker-compose.prod.yml`: production overlay with resource limits and log rotation
- `init-db/`: initial schema and versioned migration scripts
- `scripts/`: operational utilities (formatting, database migrations)
- `.github/workflows/`: CI pipelines

## 6. Quick Start

### Prerequisites

For Docker deployment (recommended):
- Docker and Docker Compose v2
- Model weights under `ModelService/models/Z-Image-Turbo`

For local development without Docker:
- `VCPKG_ROOT` environment variable pointing to your vcpkg installation
- Node.js 20+, Python 3.11+, CMake 3.21+
- Running MySQL, Redis, and MinIO instances

### 6.0 Docker Compose
```bash
cp .env.example .env
# edit .env and replace every CHANGE_ME_* value before shared or deployed use
vim .env

# ensure model weights are in place
ls ModelService/models/Z-Image-Turbo

docker compose up --build
```

For a brand-new MySQL volume, `init-db/01-schema.sql` creates the latest schema and records the current migration baseline automatically.

For an existing database created before versioned migrations were added, run:

```powershell
docker compose up -d mysql
docker compose --profile ops run --rm db-migrate
```

Generated assets and data:
- MySQL data is stored in the `mysql-data` named volume
- Redis data is stored in the `redis-data` named volume
- backend image files are stored in the `backend-storage` named volume
- model weights are expected under `ModelService/models/`

### 6.1 Model Service

```powershell
cd ModelService
python model_service.py
```

### 6.2 Backend

```powershell
cd Backend
cmake --preset x64-debug
cmake --build out\build\x64-debug --config Debug
.\out\build\x64-debug\Debug\Backend.exe
```

### 6.3 Frontend

```powershell
cd ZImageFrontend
npm install
npm run dev
```

The Vite dev server proxies `/api` and `/health` to the backend. By default it uses
`BACKEND_PORT` from the repository root `.env` and targets `http://127.0.0.1:<BACKEND_PORT>`.
Use `ZImageFrontend/.env.local` or shell environment variables to override:

```powershell
$env:VITE_BACKEND_PROXY_TARGET = "http://127.0.0.1:8082"
$env:VITE_HEALTH_PROXY_TARGET = "http://127.0.0.1:8082"
npm run dev
```

When running the backend in Docker but the model service directly on Windows, set the backend
model-service URL to the host gateway and recreate the backend container:

```powershell
PYTHON_SERVICE_URL=http://host.docker.internal:8081
```

### 6.4 VSCode Workflow

If you open the repository root in VSCode, use these commands:

- `CMake: Select Configure Preset` -> `x64-debug`
- `CMake: Delete Cache and Reconfigure` after toolchain changes
- `CMake: Build` to build the backend
- start frontend and model service from the integrated terminal with the commands above
- local `tasks.json` / `launch.json` can be added per developer if you want one-click run or debugging

### 6.5 Formatting

Formatting is standardized by:

- `.editorconfig`
- `.clang-format`
- `.prettierrc.json`
- `pyproject.toml`

Run formatting with:

```powershell
powershell -ExecutionPolicy Bypass -File .\scripts\format.ps1
```

```bash
bash ./scripts/format.sh
```

Check formatting without rewriting files:

```powershell
powershell -ExecutionPolicy Bypass -File .\scripts\format.ps1 -Check
```

```bash
bash ./scripts/format.sh check
```

Required tools:

- `clang-format`
- `node` / `npx`
- `python` plus `black` and `ruff`

### 6.6 CI Baseline
The repository now includes `.github/workflows/ci.yml` with a lightweight default pipeline:

- frontend: `npm ci` + `npm run build`
- backend: Docker-based Linux build that runs `UnitTests` and `IntegrationTests`
- model service: Python entrypoint compile smoke check
- docker: `docker compose config` plus runtime image builds for `Backend/` and `ZImageFrontend/`

Heavyweight model-image validation is intentionally split into `.github/workflows/model-service-image.yml`, so the default CI stays stable and reasonably fast.

## 7. Configuration

### 7.1 Backend

Copy the example config and fill in your local values:

```powershell
cp Backend/config.json.example Backend/config.json
# edit Backend/config.json with your database password, JWT secret, etc.
```

Primary files:
- `Backend/config.json`
- `.env.example` for Docker Compose defaults

Important settings:

- `server`: host, port, thread count
- `database`: MySQL connection, optional SSL mode, and pool settings
- `jwt`: secret and token expiration
- `python_service`: model-service URL and execution timeout
- `task_engine`: worker count, polling, lease, retry policy
- `redis`: queue coordination, lease keys, timeouts, and enable switch
- `storage`: local image storage settings

Environment-variable overrides are supported in the backend for common settings such as:

- `BACKEND_PORT`
- `DB_HOST` `DB_PORT` `DB_USERNAME` `DB_PASSWORD` `DB_NAME` `DB_SSL`
- `JWT_SECRET`
- `PYTHON_SERVICE_URL` `PYTHON_SERVICE_TIMEOUT_SECONDS`
- `REDIS_ENABLED` `REDIS_HOST` `REDIS_PORT` `REDIS_PASSWORD` `REDIS_DB`
- `REDIS_POOL_SIZE` `REDIS_CONNECT_TIMEOUT_MS` `REDIS_SOCKET_TIMEOUT_MS`
- `REDIS_TASK_QUEUE_KEY` `REDIS_LEASE_KEY_PREFIX`
- `STORAGE_ROOT_DIR` `STORAGE_PUBLIC_URL_PREFIX` `STORAGE_EXTENSION`

The backend is now container-friendly in two ways:
- Docker image builds include `/app/config.json` from `Backend/config.json.example`, so the container always has a file-based baseline config
- `BACKEND_CONFIG_PATH` can point to a mounted config file when you want to override that baseline config

### 7.2 Model Service

Key environment variables:

- `MODEL_SERVICE_PORT`
- `MODEL_PATH`
- `MODEL_SERVICE_ALLOW_ORIGINS`
- `MODEL_SERVICE_LOG_DIR`
- `MODEL_SERVICE_TEMP_DIR`
- `MODEL_SERVICE_MAX_CONCURRENT_GENERATIONS`
- `MODEL_SERVICE_TEMP_FILE_MAX_AGE_HOURS`
- `MODEL_SERVICE_TEMP_FILE_CLEANUP_INTERVAL_SECONDS`

Default container-oriented paths now assume:
- model weights: `./models/Z-Image-Turbo` or a mounted path provided through `MODEL_PATH`
- temp files: `./temp`
- logs: `./logs`

### 7.3 Docker Preparation
The repository now includes:
- `.env.example` with service-to-service defaults for containers
- `.env.production.example` as a production-only template with secret placeholders, replica counts, and resource limits
- `.dockerignore` files at the repository root and per service
- `docker-compose.yml` to orchestrate MySQL, Redis, MinIO, Backend, ModelService, Frontend, and the optional `db-migrate` utility service
- `docker-compose.prod.yml` for production-oriented resource limits, log rotation, and replica defaults
- `init-db/01-schema.sql` for first-run MySQL schema initialization
- `init-db/migrations/*.sql` plus `scripts/run-db-migrations.sh` for versioned schema upgrades on existing databases
- Dockerfiles for `Backend/`, `ModelService/`, and `ZImageFrontend/`
- `ZImageFrontend/nginx.conf` for SPA hosting plus backend/API/WebSocket reverse proxy, gzip, and baseline security headers
- `ModelService/requirements.txt` for Python image builds
- frontend dev proxy targets configurable via `VITE_BACKEND_PROXY_TARGET` and `VITE_HEALTH_PROXY_TARGET`
- GitHub Actions CI for frontend build, backend tests, and Docker validation
- dedicated model-service image workflow for heavyweight runtime image builds

### 7.4 Database Migrations

Versioned database migrations now live under `init-db/migrations/`:

- `001_initial_schema.sql`: legacy baseline schema
- `002_image_generation_task_queue.sql`: task-engine lease, retry, and worker columns plus supporting indexes

Operational notes:

- fresh `docker compose up` runs `init-db/01-schema.sql` and records `001` + `002` + `003` in `schema_migrations`
- the backend still keeps its existing startup-time defensive column/index checks for `image_generations`, but versioned migrations are now the primary upgrade path
- existing databases should be upgraded with `docker compose --profile ops run --rm db-migrate`
- when adding a new migration file, also fold that change into `init-db/01-schema.sql` and append the new version to its baseline `schema_migrations` insert for fresh installs

### 7.5 Production Compose

Recommended production flow:

```powershell
copy .env.production.example .env.production
# replace every CHANGE_ME_* placeholder before deployment
docker compose --env-file .env.production -f docker-compose.yml -f docker-compose.prod.yml up -d --build
```

Notes:

- `docker-compose.prod.yml` sets CPU and memory limits plus container log rotation defaults
- `deploy.replicas` values are provided for backend, frontend, and model-service; if your local Compose setup ignores them, use `docker compose up --scale <service>=<count>` with the same env file

## 8. Current State

What is already in place:

- frontend, backend, and model service are connected end to end
- backend task state is persisted in MySQL
- frontend supports polling, history, status filtering, cancel, retry, and protected download
- model service health remains responsive during generation
- image binaries are no longer required to be eagerly loaded in list responses
- hard-coded workstation paths and committed plaintext backend credentials have been removed from the repo defaults

What is not yet finished:

- end-to-end automated tests across all three projects
- external secrets-manager integration beyond `.env.production`
- structured observability and metrics
- stronger request validation and operational runbooks

## 9. Security Notes

- do not commit real secrets or production passwords
- `Backend/config.json` is gitignored — use `Backend/config.json.example` as the template
- replace every `CHANGE_ME_*` placeholder in `.env` or `.env.production` before any shared or deployed usage
- keep internal-only services such as MySQL and Redis on trusted networks even when using the production compose override
