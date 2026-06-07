import threading
import time


class GenerationState:
    def __init__(self) -> None:
        self.is_loading = False
        self.active_generations = 0
        self.active_kind = "none"
        self.last_error = ""
        self._state_lock = threading.Lock()
        self._active_generation_starts: list[float] = []

    def set_loading(self, value: bool) -> None:
        with self._state_lock:
            self.is_loading = value

    def set_last_error(self, value: str) -> None:
        with self._state_lock:
            self.last_error = value

    def mark_generation_started(self, kind: str = "generate") -> None:
        with self._state_lock:
            self._active_generation_starts.append(time.monotonic())
            self.active_generations = len(self._active_generation_starts)
            self.active_kind = kind

    def mark_generation_finished(self) -> None:
        with self._state_lock:
            if self._active_generation_starts:
                self._active_generation_starts.pop(0)
            self.active_generations = len(self._active_generation_starts)
            if self.active_generations == 0:
                self.active_kind = "none"

    def snapshot(self, model_loaded: bool, busy_unhealthy_seconds: int) -> dict:
        with self._state_lock:
            is_loading = self.is_loading
            active_generations = self.active_generations
            active_kind = self.active_kind
            active_started_at = min(self._active_generation_starts, default=None)
            last_error = self.last_error

        is_generating = active_generations > 0
        active_seconds = (
            int(max(0, time.monotonic() - active_started_at))
            if active_started_at is not None
            else 0
        )

        if not model_loaded:
            status = "loading" if is_loading else "unhealthy"
        elif is_generating:
            if active_seconds > busy_unhealthy_seconds:
                status = "unhealthy"
                if not last_error:
                    last_error = (
                        f"active {active_kind} exceeded {busy_unhealthy_seconds}s health threshold"
                    )
            else:
                status = "busy"
        else:
            status = "healthy"

        return {
            "status": status,
            "model_loaded": model_loaded,
            "is_loading": is_loading,
            "is_generating": is_generating,
            "active_generations": active_generations,
            "active_kind": active_kind,
            "active_seconds": active_seconds,
            "busy_unhealthy_seconds": busy_unhealthy_seconds,
            "detail": last_error,
        }
