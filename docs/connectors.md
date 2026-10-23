# Connectors: Buddy and your accounts in other apps

Connect an app once in **Settings, Connectors**, then just ask Buddy: *"read my unread
emails"*, *"draft a reply to Anna's invoice mail"*, *"what's the latest in my GitHub
issues"*. Buddy gets the app's actions as tools next to `bash` and `web_fetch`; the
permission gate decides which of them run on their own and which need your OK.

Today: **Gmail** (built in) and **any app that has an MCP server** (GitHub, Notion,
Slack, Linear ... added from Settings, no new code). Google Calendar and Drive are next
and will reuse Gmail's Google sign-in.

## Using it

| You say | What happens |
| --- | --- |
| "Read my unread emails" | `gmail_search_messages` (`is:unread`) and `gmail_read_message`. Reading runs without asking. |
| "Draft a reply to the invoice mail" | Buddy reads the thread, then `gmail_create_draft` with `reply_to_message_id`. You get an approval card showing the exact recipient, subject and text. The draft lands in Gmail's Drafts; nothing is sent. |
| "Send it" | `gmail_send_message`. Approval card again, with the full message. |

**Approval cards.** Anything that creates, sends or changes something asks you **every
time**, in every permission mode, Trusted included (Trusted covers your computer, not your
accounts). The card has *Always allow*: that saves a grant for that one tool (for example
"Gmail: create draft"); it is listed under *Always allowed* on the Connectors page, where
*Ask again* takes it back. A saved grant is ignored for the rest of a task once something
in it looked like a prompt injection.

## What each connector can do

**Gmail** asks Google for two scopes, the narrowest that do the job:

* `gmail.readonly`: search and read messages and threads.
* `gmail.compose`: create drafts and send. It cannot read-modify, label, trash or delete,
  and Noch has no tool for those.

| Tool | Kind | Rule |
| --- | --- | --- |
| `gmail_search_messages` (Gmail search syntax, up to 20) | read | runs without asking |
| `gmail_read_message` | read | runs without asking |
| `gmail_read_thread` | read | runs without asking |
| `gmail_create_draft` (optionally a reply in its thread) | write | asks every time |
| `gmail_send_message` (optionally a reply in its thread) | write | asks every time |

Attachments are listed by name, never downloaded.

**MCP servers.** A tool is *read-only* only if the server says so (`readOnlyHint`);
everything else is treated as a write and asks every time. Tool names are
`mcp_<server>_<tool>`, so a server can never take the name of a Noch tool.

## Setting up Gmail (your own Google OAuth client, about 5 minutes)

Noch does not ship a Google client ID: you create your own, so your mail is between you,
Google and your PC. Nothing is sent anywhere until you press **Connect** and approve in
your browser.

1. Open the [Google Cloud Console](https://console.cloud.google.com/), create a project
   (or pick one).
2. **APIs & Services, Library**: search for **Gmail API** and **Enable** it.
3. **OAuth consent screen** (now *Google Auth platform*): choose **External**, give it a
   name (for example "Noch"), add your own address as a **test user**. Add the two
   scopes above when it asks for scopes (optional: Noch requests them at sign-in anyway).
4. **Credentials, Create credentials, OAuth client ID**, application type
   **Desktop app**. Copy the **client ID** (and the client secret, if Google shows one).
   There is no redirect address to register: Desktop clients accept Google's loopback
   redirect (`http://127.0.0.1:<port>`).
5. In Noch: **Settings, Connectors, Gmail**: paste the client ID (and the secret if you
   have one), press **Connect**, and approve in the browser tab that opens.

Things to know:

* While the consent screen is in **Testing**, Google expires refresh tokens after about
  7 days, so you would have to connect again weekly. Publishing the consent screen
  (*In production*) avoids that. For a client only you use there is no review: Google
  just shows "Google hasn't verified this app" at sign-in, which is expected for your own
  client (choose *Advanced*, then continue). Check Google's current rules if this changes.
* If Connect says the Gmail API is not enabled, step 2 was missed.
* Disconnecting revokes the grant at Google and deletes the tokens here.

## Adding an MCP server

**Settings, Connectors, Add an MCP server.**

* **Web address (https)**: the server's Streamable HTTP endpoint. Put a token in the
  headers box, one `Name: value` line, for example `Authorization: Bearer ...`. Headers
  are sent only over https (or to this computer).
* **Command on this PC**: a command Noch starts and talks to over stdin/stdout, for
  example `npx -y some-mcp-server`. Environment variables (`NAME=value` lines) go in the
  box below.

The **GitHub** button fills in GitHub's own MCP server
(`https://api.githubcopilot.com/mcp/`); create a fine-grained personal access token with
only the repositories and permissions you want Buddy to have.

Limits of the MCP support: protocol `2025-06-18` with `initialize` (servers may answer
with an older version they support); tools only (no resources, prompts, sampling or
server-initiated requests); no browser sign-in for servers that require MCP's own OAuth
flow (use a token header for now); servers that only speak the newest stateless revision
without `initialize` are not supported yet. A command server runs with your rights:
add only servers you trust.

## How it is kept safe

* **Secrets are encrypted at rest.** OAuth tokens, the Google client secret and MCP
  tokens go through Windows DPAPI (`CryptProtectData`, your account only, bound to their
  key) into `<AppData>/connectors/secrets` (Noch's data folder, `%APPDATA%\noch\noch`). They are never in the registry settings
  or any plain file. Where encryption is unavailable they are held in memory only. Noch's
  data folder is private to the agent: Buddy's own tools cannot read it or change it.
* **OAuth is done the way Google documents for desktop apps**: authorization code with
  PKCE (S256), a one-time `state`, and a loopback listener on `127.0.0.1` that takes one
  answer and closes (it also closes on timeout or Cancel). Noch never sees your password.
* **What an app returns is untrusted.** Mail, tickets and pages are fenced in
  `<untrusted_content>`, stripped of invisible characters, checked for prompt-injection
  phrases and capped in size. A hit puts the rest of the task on guard: reading more of
  your apps asks first, and saved "always allow" grants stop applying until the next
  message. The system prompt tells the model that connected-app text is data, and to
  prefer a draft to sending.
* **Writes show exactly what will happen.** The card lists every argument, with
  bidi/zero-width characters made visible. Addresses are validated and cannot carry
  extra headers.
* **MCP tool descriptions** come from the server's author: cleaned and cut to 600
  characters before the model sees them.
* **Plan mode** cannot write to connected apps; the *Let Buddy use connected apps*
  switch removes every connector tool from the model while keeping your sign-ins.

## How it works (for developers)

```text
src/agent/
  Connector.{h,cpp}          the Connector interface, ToolDef (Read | Write), shared helpers
  ConnectorRegistry.{h,cpp}  all connectors; "always allow" grants; saved settings; QML API
  SecretStore.{h,cpp}        DPAPI-encrypted secrets, bound to their key
  OAuthClient.{h,cpp}        PKCE + loopback redirect + token exchange / refresh
  GmailConnector.{h,cpp}     Gmail over OAuthClient: tools, MIME building, HTML to text
  McpConnector.{h,cpp}       MCP client: stdio and Streamable HTTP
  BuddyProviderConnectors.cpp  startConnectors, policy sync, running a tool call
qml/panels/settings/pages/ConnectorsPage.qml   the settings page
```

* A connector exposes `tools()` while connected. `BuddyProvider` adds them to the tools
  array (`BuddyProviderPrompts.cpp`) and to the system prompt, validates calls with a
  schema built from each tool's JSON Schema (`ConnectorRegistry::schemaFor`, through the
  same repair pipeline as built-in tools), and runs them in `executeToolCall`.
* `AgentPolicy::setConnectorTools` tells the gate which tools only read and which write
  (`connector-read`, `ask:connector-write`, `connector-always`, `ask:tainted-connector`).
  `BuddyProvider::syncConnectorPolicy` keeps it current whenever a connector connects,
  disconnects or a grant changes.
* `guardToolOutput` treats every connector tool as external content.
* Config is `connectors.json` (Google client ID, MCP servers, granted tools) beside the
  encrypted `secrets/` folder. Nothing is read or written until `startConnectors()`, so
  tests never touch real files.

**Adding a built-in app** (for example Calendar): subclass `Connector`, return its
`ToolDef`s with the right `Access`, implement `call()`, register it in
`ConnectorRegistry`'s constructor, and add tests against `tests/FakeHttp.h`. For a
Google app, reuse `OAuthClient` with the app's scopes; for anything with an MCP server,
no code is needed.

Tests: `tst_secretstore`, `tst_oauth`, `tst_gmail`, `tst_mcp` (fake Google and MCP
servers, no network), the connector cases in `tst_agentpolicy` and `tst_buddyagent`.

## Design notes

Assistants that connect to your accounts converge on the same two pieces: a per-provider
OAuth 2.0 sign-in for the big first-party apps, and MCP as the open way to reach
everything else without writing an integration per app. Noch does both. The choices:

* **Loopback + PKCE rather than a hosted redirect.** Noch is a desktop app with no
  server; Google documents the loopback redirect for exactly this. With the user's own
  client there is no shared secret to leak.
* **Separate read and write access.** The gate, not the model, decides what needs a
  question, and the answer for writes does not depend on the permission mode.
* **Narrow scopes over convenience.** No delete, label or modify tools, so a mistake or
  an injection cannot lose mail.
* **MCP for breadth.** Any server works; its tools are untrusted by default (writes ask,
  descriptions are cleaned).

Not done yet: Calendar and Drive, MCP servers that need their own OAuth sign-in, MCP
resources/prompts, per-server tool allow-lists.
