# Repository Guidelines

## Project Structure & Module Organization
Core code is in `src/` and public headers are in `include/`.
`src/` is organized by layer:
- `controllers/` HTTP endpoints (`/api/auth/*`, `/api/images`)
- `services/` business logic and external calls
- `database/` MySQL access (`DBManager`, `UserRepo`)
- `middleware/`, `models/`, `utils/` for cross-cutting logic

Build artifacts are generated under `out/` and should not be committed. Runtime settings live in `config.json`.

## Build, Test, and Development Commands
Use CMake presets on Windows (configured in `CMakePresets.json`):

```powershell
cmake --preset x64-debug
cmake --build out/build/x64-debug --config Debug
```

Run locally:

```powershell
.\out\build\x64-debug\Debug\Backend.exe
```

Release build:

```powershell
cmake --preset x64-release
cmake --build out/build/x64-release --config Release
```

Dependency management is via `vcpkg.json` (Drogon, spdlog, nlohmann-json, jwt-cpp, mysql-connector-cpp, curl).

## Coding Style & Naming Conventions
Use C++20 and keep warning-clean builds (`/W4` on MSVC, `-Wall -Wextra` elsewhere).
Follow existing file conventions:
- Files: `snake_case.cpp/.h` (for example, `auth_service.cpp`)
- Types/classes: `PascalCase` (for example, `AuthService`)
- Namespaces: lowercase (for example, `database`, `models`)

Use 4-space indentation and keep style consistent with nearby code when editing.

## Testing Guidelines
Tests live in `tests/` with two suites: `tests/unit/` and `tests/integration/`. CMake defines three CTest targets: `UnitTests`, `IntegrationTests`, and `TaskEngineIntegrationTests`. Test sources are auto-discovered via `GLOB_RECURSE` and linked against the shared backend library (excluding `main.cpp`).

After building with `cmake --build out/build/x64-debug --config Debug`, run all tests via:

```powershell
ctest --test-dir out/build/x64-debug -C Debug --output-on-failure
```

Or run a specific suite:

```powershell
ctest --test-dir out/build/x64-debug -C Debug -R UnitTests --output-on-failure
ctest --test-dir out/build/x64-debug -C Debug -R IntegrationTests --output-on-failure
ctest --test-dir out/build/x64-debug -C Debug -R TaskEngineIntegrationTests --output-on-failure
```

## Commit & Pull Request Guidelines
History is minimal; existing commits use short, imperative summaries (for example, `Complete backend service skeleton`).
Prefer:
- One focused commit per logical change
- Subject line in imperative mood, <= 72 chars
- Body for rationale or breaking/config changes

PRs should include a clear summary, verification steps (commands or curl examples), and linked issue(s) when applicable.

## Security & Configuration Tips
Never commit real secrets. Treat `config.json` values (DB password, JWT secret) as local development defaults only; use environment-specific overrides for shared or production environments.
