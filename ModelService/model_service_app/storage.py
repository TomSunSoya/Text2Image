import logging
import time
from pathlib import Path

from fastapi import HTTPException

from .config import TEMP_PATH

logger = logging.getLogger(__name__)


def resolve_temp_file(filename: str) -> Path:
    candidate = (TEMP_PATH / filename).resolve()
    if candidate.parent != TEMP_PATH or candidate.name != filename:
        raise HTTPException(status_code=400, detail="Invalid filename")
    return candidate


def cleanup_temp_files_once(max_age_hours: int) -> None:
    deleted_count = 0
    cutoff_time = time.time() - max_age_hours * 3600

    for filepath in TEMP_PATH.iterdir():
        if not filepath.is_file():
            continue
        try:
            if filepath.stat().st_mtime <= cutoff_time:
                filepath.unlink()
                deleted_count += 1
        except FileNotFoundError:
            continue
        except OSError as exc:
            logger.warning("Failed to clean temp file %s: %s", filepath, exc)

    if deleted_count > 0:
        logger.info("Deleted %s expired temp file(s)", deleted_count)
