# ZImage Full Optimization Plan

## Scope
- Projects in scope:
- `ZImageFrontend`
- `Backend`
- `PythonProject`
- Projects out of scope:
- `PythonProject1`
- `ZImageBackend`

## Goal
Turn the current MVP-style image generation system into a production-style multi-service platform with:
- a unified task lifecycle
- a recoverable backend task engine
- a cleaner separation between metadata and binary storage
- end-to-end observability
- reliable frontend task UX
- deployable and testable multi-service operation

## Target Architecture
- `ZImageFrontend` only talks to `Backend`
- `Backend` is the single owner of business state and task orchestration
- `PythonProject` focuses on model execution and execution-related telemetry
- MySQL stores task metadata, user ownership, status, errors, timing, and output metadata
- object storage or file storage stores image binaries
- every generation request has a stable `task_id` and `request_id`
- the system uses a clear task state machine instead of ad hoc status transitions

## System-Level Decisions
1. Use `Backend` as the control plane.
The backend owns task creation, state transitions, retries, cancellation, timeout recovery, and result persistence.

2. Treat `PythonProject` as a worker service.
The model service should not own user-facing business state. It executes work, reports results, and exposes health and readiness.

3. Use a unified task model across all layers.
Frontend, backend, and model service should agree on task identifiers, status names, error shape, and result shape.

4. Separate image metadata from image binary storage.
The database should not be the long-term primary store for large image payloads.

5. Make reliability visible.
Metrics, structured logs, and traceable identifiers are part of the implementation, not an afterthought.

## Unified Task Model
Define a single task contract used across frontend, backend, and model service.

### Canonical Fields
- `task_id`
- `request_id`
- `user_id`
- `status`
- `prompt`
- `negative_prompt`
- `num_steps`
- `width`
- `height`
- `seed`
- `retry_count`
- `max_retries`
- `failure_code`
- `failure_message`
- `created_at`
- `started_at`
- `completed_at`
- `cancelled_at`
- `generation_time_ms`
- `image_url`
- `thumbnail_url`
- `storage_key`

### Canonical Statuses
- `queued`
- `generating`
- `success`
- `failed`
- `cancelled`
- `timeout`

### State Rules
- only backend can advance task state
- state transitions must be explicit and validated
- duplicate submissions must not create duplicate effective work
- worker recovery must requeue stuck tasks safely

## Backend Plan

### 1. Replace the in-memory queue with a recoverable task engine
- keep task state in MySQL
- add worker claiming logic instead of only process-local queueing
- support restart recovery for `queued` and stale `generating` tasks
- add lease or lock expiration fields so abandoned tasks can be reclaimed
- make worker ownership visible for debugging

### 2. Add idempotency and consistency controls
- use a stable idempotency key or `request_id`
- prevent duplicate task creation for the same effective request
- use conditional updates for state transitions
- prevent repeated workers from overwriting terminal states
- define the behavior of delete, cancel, retry, and duplicate submit clearly

### 3. Add retry and timeout recovery
- classify failures into retryable and non-retryable
- add retry counters and max retry policy
- use exponential backoff or bounded retry delay
- mark tasks as `timeout` when execution exceeds threshold
- add a recovery job for stale tasks

### 4. Build stronger Python service dependency management
- standardize backend-to-model request and response schema
- add separate connect timeout and execution timeout
- classify transport errors, model errors, and bad payload errors
- add limited retry only where safe
- add circuit-breaker style protection or dependency degrade mode

### 5. Add cancellation support
- allow queued tasks to be cancelled immediately
- define best-effort cancellation for in-flight tasks
- keep cancellation auditable in task history
- make frontend and backend behavior consistent for cancelled tasks

### 6. Move binary storage out of the main task record
- stop treating database base64 fields as the primary long-term image store
- write outputs to object storage or file storage
- keep only metadata and retrievable URLs in MySQL
- generate thumbnails or derived assets if needed

### 7. Improve persistence model
- evolve schema to include retry, timeout, lock, and storage metadata fields
- add the right indexes for status, user, created time, and request id
- separate task table and output table if that improves clarity
- keep migration strategy explicit

### 8. Strengthen runtime introspection
- structured logs with `task_id` and `request_id`
- counters for queue depth, task success rate, task failure rate, retries, and timeouts
- latency metrics for DB, Python service calls, and full task duration
- readiness checks for DB and model dependency health

### 9. Strengthen test coverage
- unit tests for service-layer state transitions
- repository tests for persistence behavior
- API tests for auth, create, status, cancel, delete, and retry flows
- fault-path tests for Python dependency failure

### 10. Add performance validation
- benchmark create-task throughput
- benchmark status query throughput
- measure task recovery behavior under restart
- verify DB index effectiveness on history queries

## PythonProject Plan

### 1. Clarify service role
- keep `PythonProject` focused on execution, not user-facing business orchestration
- expose a clear generate contract
- expose clear health and readiness semantics

### 2. Improve model lifecycle management
- separate startup health from model readiness
- preload and warm up the model where possible
- surface model load failures clearly
- make repeated initialization safe

### 3. Add execution control
- define allowed concurrency for GPU execution
- protect the model from unsafe concurrent access
- reject or queue requests when capacity is exceeded
- surface overload in a predictable response shape

### 4. Improve execution telemetry
- log per-task inference start and end
- record generation latency and failure reason
- include `task_id` and `request_id` in logs
- expose execution metrics for success, failure, timeout, and load state

### 5. Improve output management
- define output directory or storage strategy explicitly
- generate deterministic storage keys
- clean up stale temporary artifacts safely
- support output metadata reporting back to backend

### 6. Prepare for model evolution
- allow configurable model path and model profile
- leave room for LoRA or model variant selection
- keep inference-time settings explicit and validated

### 7. Add test coverage
- API contract tests
- startup and readiness tests
- failure-path tests for model load and inference exceptions
- storage cleanup behavior tests

## Frontend Plan

### 1. Turn image generation into a task-centric UX
- replace one-off generation flow with a task-driven flow
- show task lifecycle clearly
- make task status resilient across refresh and navigation

### 2. Build a dedicated task center
- list queued, running, successful, failed, cancelled, and timed-out tasks
- filter by status
- support paging, refresh, and retry
- expose failure reason and task timing

### 3. Improve task updates
- keep polling as baseline or move to `SSE` for server-pushed updates
- restore active task status on page reload
- avoid losing current task context when switching tabs or routes

### 4. Support task operations
- cancel queued or running tasks
- retry failed tasks
- download completed images
- open detail view for full parameters and result metadata

### 5. Improve result loading strategy
- avoid eagerly loading large image payloads into list views
- use metadata list responses and detail fetch on demand
- keep image preview and detail loading efficient

### 6. Improve user feedback
- standardize error messages from backend status and failure code
- distinguish task creation failure from task execution failure
- show timeout, cancellation, and retry states clearly

### 7. Add frontend testing
- component tests for generator and history views
- state-store tests for auth and task recovery behavior
- API integration tests for task lifecycle flows

## Storage Plan

### 1. Introduce dedicated output storage
- choose local file storage first or object storage if available
- store original output, thumbnail, and storage metadata
- keep database records lightweight

### 2. Define storage metadata
- `storage_key`
- `image_url`
- `thumbnail_url`
- `content_type`
- `width`
- `height`
- `size_bytes`
- `checksum`

### 3. Add lifecycle management
- clean up orphaned files
- define retention policy for deleted tasks
- keep history deletion and storage deletion behavior consistent

## API and Contract Plan

### 1. Standardize response shape
- success responses should be predictable
- error responses should include machine-readable codes and human-readable messages
- task detail, list, and status endpoints should share consistent field naming

### 2. Standardize health semantics
- liveness means the process is running
- readiness means dependencies are usable
- dependency detail should be visible without leaking internals unnecessarily

### 3. Standardize naming conventions
- choose one public API naming style and keep it stable
- map internal model-service field names inside the backend, not in the frontend

## Observability Plan

### 1. Structured logging
- use JSON or strongly structured log fields
- include `task_id`, `request_id`, `user_id`, and dependency status

### 2. Metrics
- request count
- request latency
- queue depth
- active workers
- retry count
- timeout count
- success rate
- failure rate
- Python service latency

### 3. Dashboards and alerts
- define minimal operational views for queue buildup, dependency failures, and task latency
- add alert conditions for repeated task failure, model unreadiness, and recovery-loop anomalies

## Deployment Plan

### 1. One-command local environment
- define a reproducible local environment for frontend, backend, DB, and model service
- use environment variables consistently
- document service startup and dependency order

### 2. Environment isolation
- separate local, test, and deploy configuration
- avoid relying on hardcoded machine-specific paths where possible

### 3. Runtime documentation
- architecture diagram
- service dependency diagram
- operational runbook for restart, stuck-task recovery, and common failure cases

## Quality Plan

### 1. Multi-layer test strategy
- unit tests
- repository tests
- API tests
- integration tests across backend and model service
- UI and lifecycle tests in frontend

### 2. Fault-injection coverage
- DB unavailable
- model service unavailable
- slow model execution
- invalid model response
- storage write failure
- task recovery after backend restart

### 3. Performance and capacity validation
- measure task throughput
- measure history query latency
- measure recovery time after restart
- measure model execution saturation behavior

## Recommended Execution Order
1. unify task model and API contract
2. rebuild backend task engine around persistent state
3. add idempotency, retries, timeout recovery, and cancellation
4. move image binary storage out of primary DB records
5. upgrade PythonProject readiness, execution control, and telemetry
6. rebuild frontend around task-center UX and resilient task updates
7. add end-to-end observability
8. add automated tests across all three projects
9. add deployment and runbook support
10. finish with benchmarking, failure drills, and final contract cleanup

## Done Criteria
The plan is complete when:
- a generation task survives backend restart
- duplicate submission does not create duplicate effective work
- failed tasks can be classified, retried, and audited
- cancelled tasks behave consistently across frontend, backend, and model service
- image storage is decoupled from heavy DB payloads
- a single `task_id` can be traced across all services
- the frontend can recover and display task state after refresh
- the system exposes useful metrics and structured logs
- the core task lifecycle has automated test coverage
- the full stack can be started and validated reproducibly

## Expected Outcome
After completing this plan, the project should read as:
"A multi-service AI image generation platform with a C++ task orchestration backend, a model execution service, a resilient task lifecycle, structured observability, and production-style engineering controls."
