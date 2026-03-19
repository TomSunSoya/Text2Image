FROM python:3.11-slim

ENV PYTHONUNBUFFERED=1
ENV PIP_NO_CACHE_DIR=1
ENV MODEL_SERVICE_PORT=8081
ENV MODEL_PATH=/app/models/Z-Image-Turbo
ENV MODEL_SERVICE_LOG_DIR=/app/logs
ENV MODEL_SERVICE_TEMP_DIR=/app/temp

WORKDIR /app

COPY requirements.txt ./

RUN python -m pip install --upgrade pip \
    && pip install -r requirements.txt

COPY model_service.py main.py train_lora.py ./

RUN mkdir -p /app/logs /app/temp /app/models /app/outputs

EXPOSE 8081

CMD ["python", "model_service.py"]
