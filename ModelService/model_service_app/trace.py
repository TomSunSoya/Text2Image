"""Cross-service request tracing via the X-Request-Id header."""

import contextvars
import logging
import re
import uuid

from starlette.datastructures import Headers, MutableHeaders

REQUEST_ID_HEADER = "X-Request-Id"
MAX_REQUEST_ID_LENGTH = 128
_VALID_REQUEST_ID = re.compile(r"^[A-Za-z0-9._:-]{1,128}$")

request_id_ctx: contextvars.ContextVar[str] = contextvars.ContextVar("request_id", default="-")


def get_request_id() -> str:
    return request_id_ctx.get()


def new_request_id() -> str:
    return uuid.uuid4().hex


def sanitize_request_id(value: str | None) -> str | None:
    """Return a safe trace id, or None so the caller generates one."""
    if not value:
        return None
    candidate = value.strip()
    if not _VALID_REQUEST_ID.fullmatch(candidate):
        return None
    return candidate


def install_request_id_log_factory() -> None:
    """Stamp request_id onto every LogRecord so the log format can render it."""
    factory = logging.getLogRecordFactory()
    if getattr(factory, "_request_id_patched", False):
        return

    def record_factory(*args, **kwargs):
        record = factory(*args, **kwargs)
        record.request_id = request_id_ctx.get()
        return record

    record_factory._request_id_patched = True
    logging.setLogRecordFactory(record_factory)


class RequestIdMiddleware:
    """Pure-ASGI middleware that assigns/propagates an X-Request-Id per request.

    Runs in the same context as the endpoint, so the id is visible to every log
    record emitted while the request is handled and is echoed on the response.
    """

    def __init__(self, app, header_name: str = REQUEST_ID_HEADER):
        self.app = app
        self.header_name = header_name

    async def __call__(self, scope, receive, send):
        if scope["type"] != "http":
            await self.app(scope, receive, send)
            return

        incoming = Headers(scope=scope).get(self.header_name)
        request_id = sanitize_request_id(incoming) or new_request_id()
        token = request_id_ctx.set(request_id)

        async def send_with_request_id(message):
            if message["type"] == "http.response.start":
                MutableHeaders(scope=message)[self.header_name] = request_id
            await send(message)

        try:
            await self.app(scope, receive, send_with_request_id)
        finally:
            request_id_ctx.reset(token)
