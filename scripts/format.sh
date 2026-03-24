#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT_DIR"

MODE="${1:-write}"

if [[ "$MODE" == "--check" || "$MODE" == "check" ]]; then
  PRETTIER_MODE="--check"
  CLANG_MODE=(--dry-run --Werror)
  BLACK_MODE=(--check)
  RUFF_MODE=(--check)
else
  PRETTIER_MODE="--write"
  CLANG_MODE=(-i)
  BLACK_MODE=()
  RUFF_MODE=()
fi

command -v clang-format >/dev/null 2>&1 || {
  echo "Missing clang-format on PATH" >&2
  exit 1
}

if command -v python3 >/dev/null 2>&1; then
  PYTHON="python3"
elif command -v python >/dev/null 2>&1; then
  PYTHON="python"
else
  echo "Missing python3/python on PATH" >&2
  exit 1
fi

while IFS= read -r -d '' file; do
  clang-format "${CLANG_MODE[@]}" "$file"
done < <(find Backend \( -name '*.cpp' -o -name '*.h' \) -print0)

npx --yes prettier@3.6.2 "$PRETTIER_MODE" \
  README.md \
  .prettierrc.json \
  ZImageFrontend/package.json \
  ZImageFrontend/jsconfig.json \
  "ZImageFrontend/src/**/*.{js,vue}"

"$PYTHON" -m black "${BLACK_MODE[@]}" ModelService
"$PYTHON" -m ruff format "${RUFF_MODE[@]}" ModelService
