# ZImage Workspace

## 1. Scope
This repository currently includes the main delivery path:
- `ZImageFrontend/`: frontend, built with Vue 3 + Vite + Element Plus
- `Backend/`: backend API and task orchestration, built with C++20 + Drogon + MySQL
- `ModelService/`: model execution service, built with FastAPI + Diffusers

Out of scope:
- `PythonProject1/`
- `ZImageBackend/`

## 2. Architecture
The system is split into three layers:

1. `ZImageFrontend` only talks to `Backend`
2. `Backend` owns auth, task lifecycle, history, storage metadata, and model-service orchestration
3. `ModelService` focuses on model execution and execution health

Default ports:
- frontend: `3000`
- backend: `8080`
- model service: `8081`

## 3. Main Flow
Image generation currently works like this:

1. frontend sends `POST /api/images`
2. backend creates a task in MySQL with status `queued`
3. backend workers claim queued tasks and call `POST /generate` on `ModelService`
4. `ModelService` generates the image and returns result metadata
5. backend persists task status, timing, storage metadata, and history
6. frontend polls `GET /api/images/{id}/status` and lazily loads the final image payload

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
- model service health: `GET http://localhost:8081/health`

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
- `plan.md`: long-term optimization plan for the full stack

## 6. Quick Start

### Prerequisites
- `VCPKG_ROOT` environment variable must point to your vcpkg installation (e.g. `C:\Users\you\tools\vcpkg`)

Start the services in this order.

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

### 6.4 VSCode Workflow
If you open the repository root in VSCode, use these commands:

- `CMake: Select Configure Preset` -> `x64-debug`
- `CMake: Delete Cache and Reconfigure` after toolchain changes
- `CMake: Build` to build the backend
- start frontend and model service from the integrated terminal with the commands above
- local `tasks.json` / `launch.json` can be added per developer if you want one-click run or debugging

## 7. Configuration

### 7.1 Backend
Copy the example config and fill in your local values:
```powershell
cp Backend/config.json.example Backend/config.json
# edit Backend/config.json with your database password, JWT secret, etc.
```

Important settings:
- `server`: host, port, thread count
- `database`: MySQL connection and pool settings
- `jwt`: secret and token expiration
- `python_service`: model-service URL and execution timeout
- `task_engine`: worker count, polling, lease, retry policy
- `storage`: local image storage settings

Environment-variable overrides are supported in the backend for common settings such as:
- `BACKEND_PORT`
- `DB_HOST` `DB_PORT` `DB_USERNAME` `DB_PASSWORD` `DB_NAME`
- `JWT_SECRET`
- `PYTHON_SERVICE_URL` `PYTHON_SERVICE_TIMEOUT_SECONDS`

### 7.2 Model Service
Key environment variables:
- `MODEL_SERVICE_PORT`
- `MODEL_PATH`
- `MODEL_SERVICE_ALLOW_ORIGINS`
- `MODEL_SERVICE_LOG_DIR`
- `MODEL_SERVICE_TEMP_DIR`

## 8. Current State
What is already in place:
- frontend, backend, and model service are connected end to end
- backend task state is persisted in MySQL
- frontend supports polling, history, status filtering, cancel, retry, and protected download
- model service health remains responsive during generation
- image binaries are no longer required to be eagerly loaded in list responses

What is not yet finished:
- standardized automated tests across all three projects
- production-grade secrets/config management
- structured observability and metrics
- deployment packaging and one-command local bootstrap
- stronger request validation and operational runbooks

## 9. Security Notes
- do not commit real secrets or production passwords
- `Backend/config.json` is gitignored — use `Backend/config.json.example` as the template
- move credentials and secrets to environment variables before any shared or deployed usage
