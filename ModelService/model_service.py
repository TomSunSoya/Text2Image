import uvicorn

from model_service_app.api import app
from model_service_app.config import PORT, logger


if __name__ == "__main__":
    logger.info("Model service started, port: %s", PORT)
    uvicorn.run(app, host="0.0.0.0", port=PORT, log_level="info")
