"""OpenAI-compatible Kokoro speech server for Noch.

Serves the model files mounted at $KOKORO_MODEL_DIR (default /models), so the
container never downloads weights. Noch calls:

    POST /v1/audio/speech
    {"model": "kokoro", "input": "Hello", "voice": "af_heart",
     "response_format": "wav", "speed": 1.0}

and gets 24 kHz mono WAV back, matching Kokoro-FastAPI's shape.
"""

from __future__ import annotations

import io
import logging
import os
import threading
import wave
from contextlib import asynccontextmanager

import numpy as np
import torch
from fastapi import FastAPI, HTTPException
from fastapi.responses import Response
from pydantic import BaseModel, Field

from kokoro import KPipeline
from kokoro.model import KModel

MODEL_DIR = os.path.abspath(os.environ.get("KOKORO_MODEL_DIR", "/models"))
VOICES_DIR = os.path.join(MODEL_DIR, "voices")
SAMPLE_RATE = 24000
LANG_CODES = set("abefhijpz")

log = logging.getLogger("kokoro")

threads = os.environ.get("KOKORO_THREADS")
if threads and threads.isdigit() and int(threads) > 0:
    torch.set_num_threads(int(threads))


# ---- model, pipes and voices -------------------------------------------------

_model: KModel | None = None
_pipelines: dict[str, KPipeline] = {}
_voice_packs: dict[str, torch.Tensor] = {}
_load_lock = threading.RLock()
_speak_lock = threading.Lock()


def get_model() -> KModel:
    """Load kokoro-v1_0.pth + config.json once, from the mounted folder."""
    global _model
    with _load_lock:
        if _model is not None:
            return _model

        config = os.path.join(MODEL_DIR, "config.json")
        weights = os.path.join(MODEL_DIR, "kokoro-v1_0.pth")
        missing = [p for p in (config, weights) if not os.path.isfile(p)]
        if missing:
            raise RuntimeError("Kokoro model files are missing: " + ", ".join(missing))

        log.info("Loading Kokoro from %s", MODEL_DIR)
        _model = KModel(
            repo_id="hexgrad/Kokoro-82M",
            config=config,
            model=weights,
        ).eval()
        log.info("Kokoro model ready")
        return _model


def get_pipeline(lang_code: str) -> KPipeline:
    """One pipeline per language, all sharing the one model."""
    if lang_code not in LANG_CODES:
        lang_code = "a"
    with _load_lock:
        pipe = _pipelines.get(lang_code)
        if pipe is None:
            try:
                pipe = KPipeline(
                    lang_code=lang_code,
                    repo_id="hexgrad/Kokoro-82M",
                    model=get_model(),
                )
            except TypeError:
                # Older kokoro builds have no `model=` argument; a quiet
                # pipeline with the model attached works the same way.
                pipe = KPipeline(
                    lang_code=lang_code,
                    repo_id="hexgrad/Kokoro-82M",
                    model=False,
                )
                pipe.model = get_model()
            _pipelines[lang_code] = pipe
        return pipe


def get_voice(name: str) -> torch.Tensor:
    name = (name or "").strip()
    if not name:
        raise HTTPException(status_code=400, detail="voice is required")

    with _load_lock:
        if name in _voice_packs:
            return _voice_packs[name]

        file = name if name.endswith(".pt") else f"{name}.pt"
        path = file if os.path.isabs(file) else os.path.join(VOICES_DIR, file)
        if not os.path.isfile(path):
            raise HTTPException(
                status_code=400,
                detail=f"Unknown voice '{name}'. Try /v1/audio/voices.",
            )
        try:
            pack = torch.load(path, map_location="cpu", weights_only=True)
        except TypeError:
            pack = torch.load(path, map_location="cpu")
        _voice_packs[name] = pack
        return pack


# ---- synthesis ---------------------------------------------------------------

def synthesize(text: str, voice: str, speed: float) -> np.ndarray:
    """Run Kokoro over the text and stitch the 24 kHz chunks together."""
    lang_code = voice.strip()[:1].lower()
    pipe = get_pipeline(lang_code)
    pack = get_voice(voice)

    chunks: list[np.ndarray] = []
    with _speak_lock:
        for result in pipe(text, voice=pack, speed=speed):
            audio = result.audio
            if audio is None or not audio.numel():
                continue
            chunks.append(audio.detach().cpu().numpy().astype(np.float32).ravel())

    if not chunks:
        raise HTTPException(status_code=500, detail="Kokoro produced no audio.")

    if len(chunks) == 1:
        return chunks[0]

    gap = np.zeros(int(SAMPLE_RATE * 0.12), dtype=np.float32)
    joined: list[np.ndarray] = []
    for index, chunk in enumerate(chunks):
        if index:
            joined.append(gap)
        joined.append(chunk)
    return np.concatenate(joined)


def to_wav(samples: np.ndarray) -> bytes:
    pcm = np.clip(samples, -1.0, 1.0)
    pcm = np.round(pcm * 32767.0).astype("<i2")
    out = io.BytesIO()
    with wave.open(out, "wb") as wav:
        wav.setnchannels(1)
        wav.setsampwidth(2)
        wav.setframerate(SAMPLE_RATE)
        wav.writeframes(pcm.tobytes())
    return out.getvalue()


def to_pcm(samples: np.ndarray) -> bytes:
    pcm = np.clip(samples, -1.0, 1.0)
    return np.round(pcm * 32767.0).astype("<i2").tobytes()


# ---- HTTP --------------------------------------------------------------------

class SpeechRequest(BaseModel):
    model: str = "kokoro"
    input: str = Field(min_length=1)
    voice: str = "af_heart"
    response_format: str = "wav"
    speed: float = 1.0


def _warm_up() -> None:
    try:
        get_pipeline("a")
        # Run one tiny request so the default voice pack and the oneDNN kernels
        # are warm too: the first sentence Noch asks for should not pay for it.
        synthesize("Ready.", "af_heart", 1.0)
        log.info("Kokoro warm-up complete")
    except Exception:  # health stays up; the error surfaces on the first request
        log.exception("Kokoro warm-up failed")


@asynccontextmanager
async def lifespan(_app: FastAPI):
    threading.Thread(target=_warm_up, name="kokoro-warmup", daemon=True).start()
    yield


app = FastAPI(title="Kokoro TTS for Noch", version="1.0", lifespan=lifespan)


@app.get("/health")
def health() -> dict:
    return {"status": "healthy", "model_loaded": _model is not None}


@app.get("/v1/models")
def models() -> dict:
    return {
        "object": "list",
        "data": [{"id": "kokoro", "object": "model", "owned_by": "local"}],
    }


@app.get("/v1/audio/voices")
def voices() -> dict:
    if not os.path.isdir(VOICES_DIR):
        return {"voices": []}
    names = sorted(n[:-3] for n in os.listdir(VOICES_DIR) if n.endswith(".pt"))
    return {"voices": names}


@app.post("/v1/audio/speech")
def speech(request: SpeechRequest) -> Response:
    text = request.input.strip()
    if not text:
        raise HTTPException(status_code=400, detail="input is empty")

    fmt = (request.response_format or "wav").strip().lower()
    if fmt not in ("wav", "pcm"):
        raise HTTPException(
            status_code=400,
            detail=f"response_format '{fmt}' is not supported here; use wav.",
        )

    speed = min(max(float(request.speed or 1.0), 0.5), 2.0)
    try:
        samples = synthesize(text, request.voice, speed)
    except HTTPException:
        raise
    except Exception as error:
        log.exception("Synthesis failed")
        raise HTTPException(status_code=500, detail=f"Kokoro failed: {error}") from error

    if fmt == "pcm":
        return Response(content=to_pcm(samples), media_type="audio/pcm")
    return Response(content=to_wav(samples), media_type="audio/wav")
