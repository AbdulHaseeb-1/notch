# Small CPU image for Kokoro-82M. The model itself is not baked in: the compose
# file mounts the Kokoro-82M folder that already sits in resources/.
FROM debian:bookworm-slim

ENV DEBIAN_FRONTEND=noninteractive \
    PYTHONUNBUFFERED=1 \
    PIP_DISABLE_PIP_VERSION_CHECK=1 \
    HF_HUB_OFFLINE=1 \
    KOKORO_MODEL_DIR=/models

RUN apt-get update \
 && apt-get install -y --no-install-recommends \
      ca-certificates \
      espeak-ng \
      python3 \
      python3-pip \
      python3-venv \
 && rm -rf /var/lib/apt/lists/*

RUN python3 -m venv /opt/venv
ENV PATH="/opt/venv/bin:$PATH"

# CPU-only torch: much smaller than the CUDA build on PyPI.
RUN pip install --no-cache-dir --upgrade pip \
 && pip install --no-cache-dir torch==2.6.0 --index-url https://download.pytorch.org/whl/cpu \
 && pip install --no-cache-dir "kokoro==0.9.4" "fastapi==0.115.12" "uvicorn==0.34.0"

# misaki's English G2P loads this spaCy model; bake it in so the first
# request never waits on (or depends on) a runtime download.
RUN pip install --no-cache-dir \
      "en-core-web-sm @ https://github.com/explosion/spacy-models/releases/download/en_core_web_sm-3.8.0/en_core_web_sm-3.8.0-py3-none-any.whl"

WORKDIR /app
COPY server.py /app/server.py

EXPOSE 8880
CMD ["uvicorn", "server:app", "--host", "0.0.0.0", "--port", "8880", "--log-level", "info"]
