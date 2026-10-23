# Noch long-term memory (Mem0)

Buddy remembers what you tell it across conversations. Noch keeps two kinds of
memory:

| | Where | What |
|---|---|---|
| Notes | `memories/` in Noch's data folder | Files Buddy writes with its `memory` tool, searched with BM25. Always on, no setup. |
| Long-term memory | **Mem0** | Facts extracted from your conversations (preferences, setup, projects), searched by meaning. This folder runs it. |

## Start the local stack

Needs Docker Desktop. From this folder:

```powershell
.\setup.ps1
```

It writes `.env` with random secrets (never committed), starts the containers,
waits until the API answers and saves the API key to
`%LOCALAPPDATA%\noch\mem0-local.key`, where Noch picks it up. 

Then in Noch: **Settings → Agent & tools → Long-term memory (Mem0)**. *Local* is
the default; press **Test memory** to check it.

```powershell
.\setup.ps1 -Down     # stop (memories stay in the Docker volumes)
.\setup.ps1 -Reset    # stop and delete every stored memory
```

### What runs

| Container | Role | Reachable from |
|---|---|---|
| `mem0` | FastAPI server around the open-source `mem0ai` library | `127.0.0.1:8765` only |
| `postgres` (pgvector) | vector store + history | the `mem0` container only |

Your memories are stored in Postgres on this PC. Every request needs the API key,
and the port is bound to loopback.

## Models: the provider you already use in Noch

Mem0 needs two models: one that turns "I switched to Neovim last week" into the
fact *User uses Neovim* (and decides whether it updates an older fact), and an
embedding model for searching by meaning. There is nothing to configure here and
no local model to download: Noch hands the server the **chat provider from its own
settings** (address, key and model) with each request, and the server calls that
provider. Only this computer's own server ever receives the key, over loopback;
the server keeps no copy and does not log it.

In Settings → Agent & tools → Long-term memory you can pick a cheaper
**extraction model** than your chat model, and the **embedding model** (default
`text-embedding-3-small`; it must be one your provider serves). If your provider
has no embeddings, give an **embedding address and key** for one that does (for
example OpenAI). Anthropic as the chat provider has no embeddings API, so use an
OpenAI-compatible provider or set a separate embedding address.

What goes to the provider: the conversation text (secrets already redacted) for
extraction, and each fact and search query for embeddings. The embedding size is
measured automatically, and each embedding model gets its own collection, so
switching models starts a fresh memory instead of corrupting the old one.

Other programs that call this server without a provider can use the optional
`MEM0_LLM_*` fallback in `.env` (then `docker compose up -d --force-recreate mem0`).

## Using Mem0 Cloud or your own server instead

In Noch's settings pick **Mem0 Cloud** (key from app.mem0.ai) or **Custom**
(any server that speaks the Mem0 Platform API). The local server here speaks the
same API, so nothing else changes. Note that with those options your
conversations are sent to that service.

## How Noch uses it

1. **Recall** — before each reply, Noch searches Mem0 with your request (2.5 s
   limit) and adds the best matches to Buddy's prompt as notes about you. The
   `memory_search` tool also searches it.
2. **Save** — after each reply, the exchange (secrets redacted, long text cut) is
   sent to Mem0, which keeps only durable facts and merges or updates existing
   ones. Small talk and commands ("open Spotify") produce nothing.
3. **Fail open** — if Mem0 is down, replies are unaffected; two failures in a row
   pause Mem0 for two minutes, and explicit *"remember that …"* facts are kept in
   the local notes instead.

Memories are filed under the *Memory name* in settings (default `noch`).

## Troubleshooting

```powershell
docker compose logs -f mem0         # server log
docker compose ps                   # both should be running
curl http://127.0.0.1:8765/health
```

* *Test memory* says **401**: the key in Noch differs from `MEM0_SERVER_KEY` in `.env`. Clear Noch's key field to use the generated one.
* *Could not connect*: Docker Desktop is not running or the stack is still starting.
* *Test memory* says the **embedding model failed**: your provider does not serve the model named in settings. Pick one it does, or set an embedding address.
* A provider that runs on this PC (Ollama, LM Studio) is reached as `host.docker.internal`; Noch rewrites `localhost` for you.
