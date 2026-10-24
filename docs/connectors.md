# Connectors: Buddy and your accounts in other apps

Connect an app once in **Settings, Connectors**, then just ask Buddy: *"read my unread
emails"*, *"draft a reply to Anna's invoice mail"*, *"what's the latest in my GitHub
issues"*. Buddy gets the app's actions as tools next to `bash` and `web_fetch`; the
permission gate decides which of them run on their own and which need your OK.

Today: **Gmail** and **GitHub**, both built in. Connecting is the same for every app:
press **Connect**, approve in the browser tab that opens, done — no client ID, secret
or token is ever pasted anywhere, because both apps sign in with Noch's own public
OAuth client IDs (see "Developer setup" below). MCP servers saved by older versions
still load and run, but Settings no longer adds new ones: the built-in apps are the
connectors. Google Calendar and Drive are next and will reuse Gmail's Google sign-in.

## Using it

| You say | What happens |
| --- | --- |
| "Read my unread emails" | `gmail_search_messages` (`is:unread`) and `gmail_read_message`. Reading runs without asking. |
| "Draft a reply to the invoice mail" | Buddy reads the thread, then `gmail_create_draft` with `reply_to_message_id`. You get an approval card showing the exact recipient, subject and text. The draft lands in Gmail's Drafts; nothing is sent. |
| "Send it" | `gmail_send_message`. Approval card again, with the full message. |
| "What's open on my repos?" | `github_list_repos`, then `github_list_issues`. Reading runs without asking. |
| "Open an issue for the crash" | `github_create_issue`. Approval card with the exact title and text first. |

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

**GitHub** signs in with GitHub's OAuth **device flow** and asks for the `repo` and
`read:user` scopes: `repo` is what reading and writing issues in private repositories
takes; there is no narrower GitHub scope for it.

| Tool | Kind | Rule |
| --- | --- | --- |
| `github_list_repos` (up to 30, most recently updated first) | read | runs without asking |
| `github_list_issues` (one repo, or yours across GitHub) | read | runs without asking |
| `github_read_issue` | read | runs without asking |
| `github_search_issues` (GitHub search syntax) | read | runs without asking |
| `github_list_notifications` | read | runs without asking |
| `github_create_issue` | write | asks every time |
| `github_add_comment` | write | asks every time |

**MCP servers** (saved by older versions; no longer added from Settings). A tool is *read-only* only if the server says so (`readOnlyHint`);
everything else is treated as a write and asks every time. Tool names are
`mcp_<server>_<tool>`, so a server can never take the name of a Noch tool.

## Connecting (for users: nothing to set up)

**Settings, Connectors**, press **Connect** on the app. Your browser opens the
provider's own sign-in page; approve there and you are back in Noch.

* **Gmail** uses Google's loopback flow: the browser returns to a one-time listener on
  this PC. Nothing is typed into Noch.
* **GitHub** uses the device flow: the browser opens `github.com/login/device` and the
  Connectors page shows the short code to confirm there (it is usually pre-filled by
  the link). Noch waits, polling quietly, until GitHub answers.
* Changing your mind mid-sign-in is **Cancel** on the card; the flow stops at once.
* Disconnecting Gmail also revokes the grant at Google. GitHub revokes OAuth App
  tokens only with the app secret a public client does not have, so Disconnect forgets
  the tokens here — remove the grant itself at github.com/settings/applications.

## Developer setup (one time, not per user)

Both connectors sign in with **Noch's own OAuth client IDs**, kept as two constants in
`src/agent/BuiltinOAuth.cpp`. A client ID is a public identifier, not a credential —
that is exactly how the GitHub CLI, rclone and Thunderbird ship theirs. Until the IDs
are filled in they are empty, and Connect says so plainly instead of opening a sign-in
that cannot work. Developers can also set `NOCH_GOOGLE_CLIENT_ID` /
`NOCH_GITHUB_CLIENT_ID` in the environment to try a build without recompiling.

**Google (about 5 minutes, once):**

1. [Google Cloud Console](https://console.cloud.google.com/): create the Noch project.
2. **APIs & Services, Library**: enable the **Gmail API**.
3. **OAuth consent screen**: External, name "Noch", and **Publish** it — while it is in
   Testing, Google expires refresh tokens after about 7 days and everyone would have
   to reconnect weekly. (Unverified apps show Google's standard warning at sign-in.)
4. **Credentials, Create credentials, OAuth client ID**, application type
   **Desktop app**. Put the client ID in `BuiltinOAuth.cpp` as `kGoogleClientId`.
   No redirect address is registered: Desktop clients accept Google's loopback
   redirect. Google also shows a client secret: Noch never uses it — a Desktop app
   client is a public client and the token exchange is PKCE-only.

**GitHub (about 2 minutes, once):**

1. github.com, **Settings, Developer settings, OAuth Apps, New OAuth App**: name
   "Noch", any homepage URL, and tick **Enable Device Flow**.
2. Put its client ID in `BuiltinOAuth.cpp` as `kGithubClientId`. The device flow
   needs no client secret and no callback URL.

## MCP servers (legacy)

Older versions could add arbitrary MCP servers from Settings; the add form and its
**Add a server** button are gone, because the built-in apps cover what it was used
for (its one preset was GitHub with a pasted personal access token). Servers already
saved still load from `connectors.json`, connect, and can be removed from their card;
the MCP client itself (`McpConnector`, protocol `2025-06-18`, tools only) is
unchanged.

## How it is kept safe

* **Secrets are encrypted at rest.** OAuth tokens and MCP
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
  BuiltinOAuth.{h,cpp}       Noch's own public OAuth client IDs (one per built-in app)
  OAuthClient.{h,cpp}        PKCE + loopback redirect + token exchange / refresh (Google)
  DeviceFlowClient.{h,cpp}   RFC 8628 device flow: device code, browser page, polling (GitHub)
  GmailConnector.{h,cpp}     Gmail over OAuthClient: tools, MIME building, HTML to text
  GithubConnector.{h,cpp}    GitHub over DeviceFlowClient: REST tools
  McpConnector.{h,cpp}       MCP client: stdio and Streamable HTTP (legacy servers)
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
* Config is `connectors.json` (saved MCP servers, granted tools) beside the
  encrypted `secrets/` folder. Nothing is read or written until `startConnectors()`, so
  tests never touch real files. The built-in apps keep nothing in it: their client IDs
  are in `BuiltinOAuth`, their tokens in `secrets/`.

**Adding a built-in app** (for example Calendar): subclass `Connector`, return its
`ToolDef`s with the right `Access`, implement `call()`, register it in
`ConnectorRegistry`'s constructor, and add tests against `tests/FakeHttp.h`. For a
Google app, reuse `OAuthClient` with the app's scopes and a `BuiltinOAuth` client ID;
for a provider with a device flow, reuse `DeviceFlowClient`.

Tests: `tst_secretstore`, `tst_oauth`, `tst_gmail`, `tst_github`, `tst_mcp` (fake
Google, GitHub and MCP servers, no network), the connector cases in `tst_agentpolicy`
and `tst_buddyagent`.

## Design notes

Assistants that connect to your accounts converge on the same two pieces: a per-provider
OAuth 2.0 sign-in for the big first-party apps, and MCP as the open way to reach
everything else without writing an integration per app. Noch does both. The choices:

* **The app's own public client IDs, so connecting is one click.** Real desktop apps
  (the GitHub CLI, rclone, Thunderbird) ship their OAuth client ID in the open: an ID
  identifies the app, it does not authenticate it. What protects the flow is PKCE and
  the device grant — neither involves a secret a user could paste, so Settings has no
  field for one. The earlier design (every user registering their own Google Cloud
  project and pasting its ID) was safer-looking but unusable, and the ID it protected
  was never a credential.
* **Each provider's own browser flow.** Google: authorization code + PKCE (S256), a
  one-time `state`, and a loopback listener on `127.0.0.1` that takes one answer and
  closes — the flow Google documents for desktop apps. GitHub: the device flow,
  because GitHub's OAuth Apps have no PKCE loopback flow; the browser page and the
  short user code suit a desktop app just as well, and GitHub explicitly needs no
  client secret for it.
* **Separate read and write access.** The gate, not the model, decides what needs a
  question, and the answer for writes does not depend on the permission mode.
* **Narrow scopes over convenience.** No delete, label or modify tools for mail, so a
  mistake or an injection cannot lose mail. (GitHub's `repo` scope is coarse — that is
  GitHub's granularity, and the page says what it covers.)

Not done yet: Calendar and Drive, MCP resources/prompts, per-server tool allow-lists.
