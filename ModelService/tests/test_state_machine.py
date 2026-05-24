import time

from model_service_app.state import GenerationState


def test_generation_state_transitions_back_to_idle():
    state = GenerationState()

    state.mark_generation_started("generate")
    busy = state.snapshot(model_loaded=True, busy_unhealthy_seconds=60)
    assert busy["status"] == "busy"
    assert busy["active_kind"] == "generate"
    assert busy["active_generations"] == 1

    state.mark_generation_finished()
    idle = state.snapshot(model_loaded=True, busy_unhealthy_seconds=60)
    assert idle["status"] == "healthy"
    assert idle["active_kind"] == "none"
    assert idle["active_generations"] == 0


def test_generation_state_reports_overdue_active_work_as_unhealthy():
    state = GenerationState()
    state.mark_generation_started("generate")
    with state._state_lock:
        state._active_generation_starts = [time.monotonic() - 61]

    snapshot = state.snapshot(model_loaded=True, busy_unhealthy_seconds=60)

    assert snapshot["status"] == "unhealthy"
    assert snapshot["is_generating"] is True
    assert "exceeded" in snapshot["detail"]
