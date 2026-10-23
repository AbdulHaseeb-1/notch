"""Noch's local Mem0 server.

Wraps the open-source `mem0ai` library behind the same HTTP shape as the hosted
Mem0 Platform (Authorization: Token <key>, /v3/memories/add/, /v3/memories/search/),
so Noch talks to this container and to api.mem0.ai with the same client code.

Everything is configured through environment variables (see .env.example).
"""
import hashlib
import hmac
import json
import logging
import os
import threading
from typing import Any, Optional

from fastapi import Depends, FastAPI, Header, HTTPException
from pydantic import BaseModel, Field

log = logging.getLogger("noch-mem0")
logging.basicConfig(level=logging.INFO)

# What Mem0's extractor should keep. Noch's agent sends whole exchanges, most
# of which are commands ("open Spotify", "set a timer") that are not worth
# remembering - so the extractor is told to be picky and to return no facts for
# them. Output format is dictated by mem0 itself ({"facts": [...]}).
CUSTOM_INSTRUCTIONS = """\
You maintain the long-term memory of a desktop assistant called Buddy.
Keep only durable facts about the USER that will still matter in a later
conversation:
- identity and setup: name, language, location, timezone, devices, apps, tools, editor, OS
- stable preferences and habits: tastes, how they like answers, schedules, routines
- ongoing projects, people, goals, decisions and their reasons
- explicit requests such as "remember that ..."
Do NOT keep:
- one-off commands or chit-chat ("open Spotify", "what time is it", "thanks")
- anything the assistant did or said, unless the user confirmed it as a fact about themselves
- file contents, command output, search results, or web pages
- secrets of any kind: passwords, API keys, tokens, card or ID numbers
Write each fact as one short, self-contained sentence in the third person
("User prefers dark themes"). If a new fact contradicts an older one, state the
new one. If nothing qualifies, return no facts.
"""

_memories: dict[str, Any] = {}
_memory_lock = threading.Lock()


def _env(name: str, default: str = "") -> str:
    return os.environ.get(name, default).strip()


class ProviderPart(BaseModel):
    """An OpenAI-compatible endpoint, as configured in the Noch app."""
    base_url: str
    api_key: str = ""
    model: str


class Provider(BaseModel):
    llm: Optional[ProviderPart] = None
    embedder: Optional[ProviderPart] = None


def _resolve(provider: Optional[Provider]) -> tuple[dict[str, Any], dict[str, Any]]:
    """(llm, embedder) endpoint settings: from the request (the app's own provider),
    else from the environment. Raises 503 with a fix-it message when neither is set."""
    llm: dict[str, Any] = {}
    if provider and provider.llm:
        llm = provider.llm.model_dump()
    elif _env("MEM0_LLM_API_KEY"):
        llm = {
            "base_url": _env("MEM0_LLM_BASE_URL", "https://api.openai.com/v1"),
            "api_key": _env("MEM0_LLM_API_KEY"),
            "model": _env("MEM0_LLM_MODEL", "gpt-4o-mini"),
        }
    if not llm:
        raise HTTPException(
            503,
            "No model provider. Noch sends the chat provider from its settings with each request; "
            "this one did not (an Anthropic-only setup has no embeddings API). Pick an OpenAI-compatible "
            "provider in Noch, or set MEM0_LLM_API_KEY in docker/mem0/.env.",
        )
    emb: dict[str, Any] = {}
    if provider and provider.embedder:
        emb = provider.embedder.model_dump()
    else:
        emb = {
            "base_url": _env("MEM0_EMBED_BASE_URL") or llm["base_url"],
            "api_key": _env("MEM0_EMBED_API_KEY") or llm["api_key"],
            "model": _env("MEM0_EMBED_MODEL", "text-embedding-3-small"),
        }
    return llm, emb


def _probe_dims(emb: dict[str, Any]) -> int:
    """Embedding size of the configured model, measured once (the vector column needs it)."""
    if _env("MEM0_EMBED_DIMS"):
        return int(_env("MEM0_EMBED_DIMS"))
    from openai import OpenAI

    try:
        client = OpenAI(api_key=emb["api_key"] or "none", base_url=emb["base_url"], timeout=30)
        vec = client.embeddings.create(model=emb["model"], input="dimension probe").data[0].embedding
    except Exception as e:  # provider errors carry the useful text
        raise HTTPException(
            502, f"Embedding model '{emb['model']}' failed at {emb['base_url']}: {_short(e)}. "
                 "Set an embedding model your provider serves (Noch: Settings > Long-term memory)."
        )
    return len(vec)


def _short(e: Exception) -> str:
    msg = " ".join(str(e).split())
    return msg[:300]


def _build_config(llm: dict[str, Any], emb: dict[str, Any], dims: int) -> dict[str, Any]:
    pg = (
        f"postgresql://{_env('POSTGRES_USER', 'mem0')}:{_env('POSTGRES_PASSWORD')}"
        f"@{_env('POSTGRES_HOST', 'postgres')}:{_env('POSTGRES_PORT', '5432')}"
        f"/{_env('POSTGRES_DB', 'mem0')}"
    )
    # One collection per embedding model: vectors from different models must never mix.
    tag = hashlib.sha1(f"{emb['model']}|{dims}".encode()).hexdigest()[:6]
    collection = f"{_env('MEM0_COLLECTION', 'noch_memories')}_{dims}_{tag}"
    llm_cfg: dict[str, Any] = {
        "model": llm["model"],
        "api_key": llm["api_key"] or "none",
        "openai_base_url": llm["base_url"],
        "max_tokens": int(_env("MEM0_LLM_MAX_TOKENS", "1500")),
    }
    if _env("MEM0_LLM_TEMPERATURE"):
        llm_cfg["temperature"] = float(_env("MEM0_LLM_TEMPERATURE"))
    return {
        "vector_store": {
            "provider": "pgvector",
            "config": {
                "connection_string": pg,
                "collection_name": collection,
                "embedding_model_dims": dims,
                "hnsw": True,
            },
        },
        "llm": {"provider": "openai", "config": llm_cfg},
        "embedder": {
            "provider": "openai",
            "config": {
                "model": emb["model"],
                "api_key": emb["api_key"] or "none",
                "openai_base_url": emb["base_url"],
                "embedding_dims": dims,
            },
        },
        "history_db_path": "/data/history.db",
        "custom_instructions": _env("MEM0_CUSTOM_INSTRUCTIONS") or CUSTOM_INSTRUCTIONS,
    }


def get_memory(provider: Optional[Provider] = None):
    """A Memory for this provider. Instances are cached by a hash of the settings
    (the key itself is never stored or logged by this server)."""
    llm, emb = _resolve(provider)
    ident = hashlib.sha256(json.dumps([llm, emb], sort_keys=True).encode()).hexdigest()
    with _memory_lock:
        mem = _memories.get(ident)
        if mem is None:
            from mem0 import Memory

            dims = _probe_dims(emb)
            try:
                mem = Memory.from_config(_build_config(llm, emb, dims))
            except Exception as e:
                raise HTTPException(502, f"Could not start Mem0: {_short(e)}")
            if len(_memories) >= 8:
                _memories.pop(next(iter(_memories)))
            _memories[ident] = mem
            log.info("mem0 ready: llm=%s embedder=%s dims=%d", llm["model"], emb["model"], dims)
        return mem


def _call(fn, *args, **kwargs):
    """Run a mem0 call, turning provider failures into a readable 502."""
    try:
        return fn(*args, **kwargs)
    except HTTPException:
        raise
    except Exception as e:
        log.warning("mem0 call failed: %s", _short(e))
        raise HTTPException(502, _short(e))


def require_key(
    authorization: Optional[str] = Header(default=None),
    x_api_key: Optional[str] = Header(default=None),
) -> None:
    expected = _env("MEM0_SERVER_KEY")
    if not expected:
        raise HTTPException(500, "MEM0_SERVER_KEY is not set on the server")
    supplied = x_api_key or ""
    if authorization:
        parts = authorization.split(None, 1)
        if len(parts) == 2 and parts[0].lower() in ("token", "bearer"):
            supplied = parts[1]
    if not hmac.compare_digest(supplied.encode(), expected.encode()):
        raise HTTPException(401, "invalid API key")


class Message(BaseModel):
    role: str
    content: str


class AddBody(BaseModel):
    messages: list[Message]
    user_id: Optional[str] = None
    agent_id: Optional[str] = None
    run_id: Optional[str] = None
    metadata: Optional[dict[str, Any]] = None
    infer: bool = True
    provider: Optional[Provider] = None


class SearchBody(BaseModel):
    query: str = Field(min_length=1)
    filters: dict[str, Any]
    top_k: int = Field(default=10, ge=1, le=100)
    threshold: float = Field(default=0.1, ge=0.0, le=1.0)
    rerank: bool = False
    provider: Optional[Provider] = None


class ListBody(BaseModel):
    filters: dict[str, Any]
    top_k: int = Field(default=50, ge=1, le=1000)
    provider: Optional[Provider] = None


app = FastAPI(title="Noch Mem0", docs_url=None, redoc_url=None)


@app.get("/health")
def health() -> dict[str, Any]:
    return {"status": "ok", "auth": bool(_env("MEM0_SERVER_KEY")),
            "env_provider": bool(_env("MEM0_LLM_API_KEY"))}


@app.post("/v3/memories/add/", dependencies=[Depends(require_key)])
def add_memories(body: AddBody) -> dict[str, Any]:
    if not (body.user_id or body.agent_id or body.run_id):
        raise HTTPException(400, "one of user_id, agent_id, run_id is required")
    out = _call(
        get_memory(body.provider).add,
        [m.model_dump() for m in body.messages],
        user_id=body.user_id,
        agent_id=body.agent_id,
        run_id=body.run_id,
        metadata=body.metadata,
        infer=body.infer,
    )
    return {"status": "SUCCEEDED", "results": out.get("results", []) if isinstance(out, dict) else out}


@app.post("/v3/memories/search/", dependencies=[Depends(require_key)])
def search_memories(body: SearchBody) -> dict[str, Any]:
    out = _call(
        get_memory(body.provider).search,
        body.query,
        top_k=body.top_k,
        filters=body.filters,
        threshold=body.threshold,
        rerank=body.rerank,
    )
    return {"results": out.get("results", []) if isinstance(out, dict) else out}


@app.post("/v3/memories/", dependencies=[Depends(require_key)])
def list_memories(body: ListBody) -> dict[str, Any]:
    out = _call(get_memory(body.provider).get_all, filters=body.filters, top_k=body.top_k)
    return {"results": out.get("results", []) if isinstance(out, dict) else out}


@app.delete("/v1/memories/{memory_id}/", dependencies=[Depends(require_key)])
def delete_memory(memory_id: str, provider: Optional[Provider] = None) -> dict[str, str]:
    _call(get_memory(provider).delete, memory_id)
    return {"message": "deleted"}
