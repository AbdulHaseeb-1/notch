# Web search in Noch

`web_search` is served by one of two backends, chosen per provider by
`webSearchBackendFor()` (`src/agent/BuddyProviderInternal.h`):

| Provider | Backend |
| --- | --- |
| `anthropic` | Anthropic's server-side `web_search` tool |
| everything else (OpenAI, OpenAI-compatible/custom, Ollama, OpenCode) | client-side DuckDuckGo scrape + Wikipedia fallback |

The two are never mixed for one provider: on Anthropic the request carries
the server tool definition **instead of** Noch's client `web_search` schema
(same name, different search). On other providers the client schema is sent
and `executeWebSearchTool()` (`src/agent/BuddyProviderWeb.cpp`) runs it.

## Why not the Claude Agent SDK's WebSearch?

The SDK's WebSearch is a built-in tool of the SDK's own agent runtime, not
a callable search function. The SDK (`query()`) spawns a bundled Claude Code
CLI subprocess and talks to it over stdin/stdout JSON; the host never calls
the Anthropic API itself, and search only fires as a side effect of asking
the embedded agent to answer a prompt. It exists only for Python/TypeScript,
is Claude-only, and would replace Noch's own loop, AgentPolicy, HookBus,
plan validation, tool diet, repair pipeline, traces, and ChatSessions.
Shipping it in a C++/Qt app means bundling and supervising a Node CLI
subprocess plus a bridge — an agent-runtime replacement, not a search
upgrade. Rejected.

## Anthropic server-side web search (the implemented backend)

- **Tool definition** (in the `tools` array):
  `{ "type": "web_search_20250305", "name": "web_search", "max_uses": 5 }`.
  The basic 20250305 version is deliberate: it is the only version supported
  on every Anthropic surface that supports search at all (Vertex AI supports
  only this one). Newer versions (`web_search_20260209` / `web_search_20260318`)
  entangle code execution and `allowed_callers` defaults.
- **Response blocks**: a `server_tool_use` block
  `{ id: "srvtoolu_…", name: "web_search", input: { query } }` followed by a
  `web_search_tool_result` block (`tool_use_id`, `content` = list of
  `web_search_result` blocks `{ type, title, url, encrypted_content,
  page_age }`). On a search error, `content` is a single
  `web_search_tool_result_error` object with `error_code` in
  { too_many_requests, invalid_tool_input, max_uses_exceeded, query_too_long,
  request_too_large, unavailable }. Text blocks carry
  `web_search_result_location` citations
  `{ url, title, encrypted_index, cited_text }`.
- **Multi-turn rule (critical)**: assistant content must be sent back
  exactly as received, including `encrypted_content` and `encrypted_index`,
  or the API returns 400. Noch appends the paused/server message verbatim.
- **`pause_turn`**: a long-running search can end the reply with
  `stop_reason: "pause_turn"`. Noch continues by sending the paused assistant
  message back unchanged and dispatching again (same pattern as the
  max_tokens continuation), bounded by the step budget.
- **Mixing rule**: if the model calls web search AND a client tool in the
  same parallel group, the API returns `stop_reason: "tool_use"` and does
  NOT run the search yet. Noch returns the client tool results normally and
  the search runs on the next request.
- **Streaming**: server blocks arrive as normal content blocks
  (`content_block_start` with the block, `input_json_delta` for the query,
  then the result block). `StreamAccumulator` already preserves them.
- **Availability**: Claude API (1P) full, Claude on AWS full, Vertex AI
  basic only, **Amazon Bedrock: not supported at all**. Support is per
  model (Models API `capabilities.server_tools.web_search.supported`);
  an unsupported model yields a 400, which degrades via the existing
  provider-rejects-tools path.
- **Cost**: **$10 USD per 1,000 searches**, plus standard token costs. Each
  search counts once regardless of result count; errored searches are not
  billed. Results count as input tokens in the search turn AND in every
  subsequent turn they remain in context.
- **Policy note**: server tool calls run on Anthropic's side, so they do
  not pass through AgentPolicy the way client calls do (same as the
  pre-existing server_tool_use rendering). Client `web_search`/`web_fetch`
  on other providers are still policy-gated external network reads.

## DuckDuckGo backend (fallback, non-Anthropic providers)

`executeWebSearchTool()` GETs `https://html.duckduckgo.com/html/?q=…`
(browser User-Agent, 15 s timeout), parses results with
`parseDuckDuckGoHtml`, and registers citations. If the scrape yields
nothing, it falls back to the Wikipedia Search API
(`en.wikipedia.org/w/api.php`, `action=query`, `list=search`). `web_fetch`
is a separate tool and is unchanged on all providers.
