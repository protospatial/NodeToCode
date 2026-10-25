# Changelog

Notable changes to Node to Code, newest first. Numbers in brackets are pull requests on
[protospatial/NodeToCode](https://github.com/protospatial/NodeToCode/pulls); short hashes are commits on `dev-2.0`.
Notes for 1.0.0 to 1.2.3 are on the [GitHub releases](https://github.com/protospatial/NodeToCode/releases) page
and in the wiki's [Latest Updates](wiki/Latest-Updates.md).

<!--
Owner, before tagging (card 0083): lines marked PROVISIONAL follow a review recommendation for an owner decision
that is not confirmed yet. Edit them if the decision changes, then delete these comments.
  0052 engines 5.6-5.8 | 0053 licence and DCO | 0073 Win64 only | 0085 5.6/5.7 MCP transport
  0019 run-python off by default | 0038 subscription sign-in removed | 0061 per-user secrets file
Set the release date in the heading below when the tag is made.
-->

## [2.0.0-beta.1] - unreleased

Node to Code 2.0 is a new development line, `dev-2.0`, with its own git history. Besides translation it adds an MCP
server and agent tools that let AI agents read and edit Blueprints in a running editor, a Python bridge, a Slate
editor UI, tagging and batch translation. This is a beta. The stable release is still 1.2.3, on `main` and on Fab.

### Requirements

- Unreal Engine 5.6, 5.7 or 5.8. A project on 5.5 or older stays on 1.2.x
  ([#38], [#39], 8f16836). <!-- PROVISIONAL 0052 -->
- Windows (Win64), editor only. Mac is no longer on the platform allowlist, because it has never been compiled;
  Mac or Linux is added once a build on that platform is verified
  ([#65], [decision 0001](.agents/decisions/0001-platforms-for-2-0-beta.md)). <!-- PROVISIONAL 0073 -->
- The `.uplugin` reports `Version` 3 and `VersionName` "2.0.0-beta.1", marked beta ([#65]).

### Added

**MCP server and agent tools**

- An MCP server inside the editor at `http://127.0.0.1:27000/mcp` (setting **Server Port**), with JSON-RPC
  batches, resources, prompts and progress notifications (f61300a, 6da9fc2, 04fbfb5, 080c6ad, 9a07871).
- MCP protocol 2025-11-25, with 2025-06-18, 2025-03-26 and 2024-11-05 for older clients. Each `initialize` gets
  its own `Mcp-Session-Id`, so several clients can share one editor; `DELETE /mcp` ends a session and cancels its
  running calls. JSON-RPC batches are accepted on 2025-03-26 sessions only, as the later versions dropped them
  ([#80], [#81]).
- Agent tools in these groups: read the focused Blueprint and its functions, variables and components; add,
  connect and delete nodes; create and delete functions, function pins and variables; tag graphs; compile and save;
  search and create Blueprint classes; add, list and remove components; search, move, copy and rename assets and
  folders in the Content Browser; read project files and folders; translate with Node to Code's own LLM settings
  (commits from 241e31d to abb98cc, 2025-06-12 to 2025-07-06). With Python execution off, `tools/list` returns
  59 tools ([#68]).
- `translate-focused-blueprint` answers on the same request when the translation finishes. On UE 5.8 it streams
  progress first when the client asks for `text/event-stream`; on 5.6 and 5.7 it returns one JSON result
  ([#59]). <!-- PROVISIONAL 0085 -->
- `nodetocode/cancelTask` stops a running translation and its HTTP request ([#51], [#52]). It is deprecated: the
  standard `notifications/cancelled` with the tools/call's request id now stops the call the same way and no
  response is sent for it ([#79], [#82]).
- MCP errors and capabilities follow the 2025-11-25 spec: an unknown tool or prompt and a missing required prompt
  argument are -32602, an unknown resource is -32002 with the uri, a handler timeout is -32603;
  `resources/templates/list` lists the resource templates; `logging` is no longer advertised; `initialize` returns
  `instructions` naming the tools to start with ([#82]). A request with `"id": null` or a `method` that is not a
  string is -32600 ([#80]).
- Optional dynamic tool discovery: only `assess-needed-tools` is listed until the client asks for tool groups;
  off by default (66659c1, df65415, 9579c4a).
- **Enable MCP Server** (on by default) and **Allow MCP Python Execution** (off by default), both per user under
  **Project Settings > Plugins > Node to Code (User)**. The server does not start in commandlets or unattended
  runs unless `-N2CMcp` is passed (01aa7d3, [#26]). <!-- PROVISIONAL 0019 -->

**Python**

- The `nodetocode` module in `Content/Python/nodetocode`: Blueprint, graph-editing and editor-navigation helpers
  for the editor's Python (c2c76d7, 4fb31b9, a5d19da).
- A script library that agents can list, search, read, save and rerun, with ten starter scripts, kept in the
  project's `Content/Python/scripts` (7ae749d, e318fc1, 2fa7816).
- `run-python`, `save-python-script` and `delete-python-script` over MCP, listed only when **Allow MCP Python
  Execution** is on (7ae749d, 01aa7d3). <!-- PROVISIONAL 0019 -->
- A stdio bridge, `Content/Python/mcp_bridge` (`launch_bridge.bat` / `launch_bridge.sh`), for MCP clients that
  only start stdio servers. It finds UE 5.6 to 5.8's Python and passes UTF-8 and streamed responses through
  (8d94a08, e775d97, [#60]). It runs calls side by side and forwards the client's cancel while a call runs, finds
  the editor through the MCP server's discovery file instead of scanning ports (`--project` or
  `NODETOCODE_PROJECT` picks one of several editors), and can start before the editor ([#85]). After an editor
  restart it opens a new session by itself, and it ends its session when the client closes stdin ([#81]).

**Editor**

- A Slate editor window with a translation viewer, a tag manager and batch progress (7616abc, 53a7a13, c232e0c).
- A toolbar overlay on the Blueprint graph with Translate, Copy JSON, tag and open-window buttons
  (ae80df9, 2b2fbba, a86d97f, d3113e2).
- Tags on graphs, kept with translation history in `Saved/NodeToCode/BlueprintGraphState.json` (02dd949,
  24adaa4, [#48]).
- Batch translation of tagged graphs and batch JSON export without an LLM call (d7292da, 0112479).
- Token estimation with a context-usage indicator on the overlay, and provider and model selection in the
  window (5db7852, 8e35641, 2830010).
- A configurable colour palette for the window, **Theming > UI Colors** (c827960, 162cb36).

**Providers and models**

- One model registry for every provider's model IDs, prices, context windows and request rules (9a74388, [#30]).
- New models: Claude Sonnet 5.5, Opus 5.5 and Haiku 5.5; GPT-6.1 Sol, GPT-6 Astra and GPT-6 Luna; Gemini 3.8
  Flash, Gemini 4 Argon and Gemini 3.5 Flash-Lite; DeepSeek Flash (with or without thinking) and DeepSeek V4 Pro
  ([#32], [#33]). Deprecated models show their shutdown date in Project Settings ([#32]).
- **Max Output Tokens** per cloud provider and LM Studio, with an Auto mode (the default) that sizes the budget
  from the model and the prompt ([#41]).
- Ollama: a separate enable box per sampler option, Context Window and Max Tokens at 0 (server default) unless
  set, and a **Think** setting ([#34]).
- API keys from environment variables when none is saved: `ANTHROPIC_API_KEY`, `OPENAI_API_KEY`,
  `GEMINI_API_KEY`, `DEEPSEEK_API_KEY` (or `DEEPSEEK_API`). Project Settings shows an **API Key Source** line
  per provider ([#44]).
- A failed translation says why. Each error has a kind (Truncated, Refused, Blocked, RateLimited, Overloaded,
  Auth, InvalidRequest, Billing, Server, Network or Parse), the HTTP status, the provider's own message and
  whether a retry can help. `translate-focused-blueprint` returns it as an `isError` result whose JSON has
  `error`, `error_kind`, `message`, `http_status`, `retryable`, `provider` and `model`; batch items and
  `BatchSummary.json` carry it too, and the batch progress window shows it under the failed item ([#70], [#71]).

**Project**

- `NOTICE`, `CONTRIBUTING.md`, `SECURITY.md`, issue forms and a pull request template. Contributions are under
  Apache-2.0 with a DCO sign-off (`git commit -s`)
  ([#66], [decision 0002](.agents/decisions/0002-licence-and-contribution-model.md)). <!-- PROVISIONAL 0053 -->
- The user guide lives in `wiki/` in this repository ([#27], [#36]).
- `Scripts/package-release.ps1` builds per-engine zips and a SHA256SUMS file from a clean `git archive`, then
  checks, rebuilds and loads each package ([#67]). Packages include `ThirdParty`, `LICENSE`, `NOTICE` and
  `Config/FilterPlugin.ini` ([#37], [#66]).
- `build.ps1` at the repository root builds the plugin for UE 5.6, 5.7 and 5.8. `-Smoke` starts the editor in
  each MCP registration mode and fails on log errors, a changed tool count, a failed MCP probe, the MCP
  conformance suite or Inspector CLI, or a changed project config file; `-Test` runs the automation tests
  ([#72], [#73], [#76], [#77]). New tests: Automation Specs for path policy, pagination, names, the model
  registry and the MCP protocol, CPython pytest for the script library, and live probes for the BeginPlay
  deletion, apostrophe and PIE-edit bugs ([#74], [#75], [#78], [#88]). How to run them is in `CONTRIBUTING.md`.
- A threat model for the MCP server, `Context/mcp-server/threat-model.md`, linked from `SECURITY.md` ([#89]).
- A docs check runs on every pull request into `dev-2.0`: a pull request that changes the plugin updates the
  matching user guide pages or says why not in a `Docs:` line ([#93]). The contributor docs describe the MCP
  tool registration modes and the tool categories as the code has them ([#92]).

### Changed

- N2C JSON is version 2.0.0 ([decision 0003](.agents/decisions/0003-n2c-json-v2-contract.md)): when one output
  feeds several inputs, its `flows.data` value is an array of every input; a graph without fan-out gets the same
  JSON as 1.0.0. 1.0.0 documents (1.2.x saves) still read, also from Python with `nodetocode.read_n2c_json`
  (card 0135). Each `flows.execution` entry is one exec wire, from the output pin to the input pin it runs
  (`"N3.P5->N4.P1"`), instead of a node pair (card 0136). <!-- PROVISIONAL 0117 -->
- Default models: Claude Sonnet 5.5, GPT-6.1 Sol, Gemini 3.8 Flash and DeepSeek Flash with thinking ([#32], [#33]).
- DeepSeek's `deepseek-chat` and `deepseek-reasoner` were discontinued, so the saved R1 and V3 choices both send
  `deepseek-flash`, R1 with thinking on and V3 with it off ([#33]).
- Requests no longer send sampling parameters or schema fields that current models reject. The OpenAI JSON
  schema is sent as strict, and the Gemini key goes in the `x-goog-api-key` header instead of the URL
  ([#30], [#31]).
- API keys are saved per user, outside the project: `%LOCALAPPDATA%\NodeToCode\secrets.json` on Windows, written
  atomically with a `.bak` copy ([#42]). <!-- PROVISIONAL 0061 -->
- The code editor uses one theme for every language, **Theming > Code Editor Theme**, instead of 21 themes per
  language (a4f66d6).
- The Blueprint editor's Node to Code toolbar menu is replaced by the graph overlay. The command names are
  unchanged, so custom key bindings still work (a86d97f).
- Startup no longer writes HTTP timeouts to `Config/DefaultEngine.ini` or turns off **Use Less CPU when in
  Background**. Timeouts are set per request, and CPU throttling is off only while a translation runs (4fea3e5).
- The vendored cpp-httplib is v0.63.0 ([#40]). Since the notification server was removed ([#84]) no source
  file includes it; it stays for a possible UE 5.6/5.7 streaming transport. <!-- PROVISIONAL 0085 -->
- MCP request bodies are parsed exactly once, so JSON-RPC batches work and trailing bytes are refused; responses
  are condensed JSON ([#56]).
- `UN2CCodeEditorWidget::GetSelection` outputs are renamed `OutStartLine` and `OutEndLine`, and a line-and-column
  overload is added ([#43]).
- The LLM layer returns a cancellable `FN2CLLMRequestHandleRef` from `IN2CLLMService::SendRequest`,
  `UN2CHttpHandlerBase::PostLLMRequest`, `UN2CLLMModule::ProcessN2CJson` and `ProcessN2CJsonWithOverrides` ([#51]).
  `FN2CTranslationResponse` carries an `FN2CTranslationError` ([#70]).
- MCP tools that run for a long time implement `IN2CMcpTool::InvokeAsync`, which gets an `FN2CToolCallContext`
  for progress and cancellation ([#79]).

### Removed

- The 1.2.x editor UI, an Editor Utility Widget under `/NodeToCode/UI/` (NodeToCodeUI, the `N2C_EUW_*`
  components, `N2C_Theme_*` data assets, `N2C_T_UI_*` icons, `MPC_` / `M_ComboBoxText`). The UMG wrappers
  `UN2CMainWindowWidget`, `UN2CTagManagerWidget` and `UN2CCodeEditorWidget` stay for this beta, unsupported and
  planned for removal ([#61]).
- The per-language code editor themes and the Blueprint-callable `SetTheme`, `GetAvailableThemes`,
  `GetThemeFromName` and `GetCurrentTheme` on `UN2CCodeEditorWidget` (a4f66d6).
- `UN2CUserSecrets` and `UN2CSettings::UserSecrets`; C++ reads keys through `FN2CSecretStore::Get()` ([#42]).
- Models the providers have retired. They stay in the enums so saved settings load, but are hidden in Project
  Settings ([#32]).
- Claude subscription and Google-account sign-in, which earlier 2.0 development builds had (5e42920, b73299f).
  Node to Code uses API keys only; to use a subscription, drive Node to Code from that vendor's agent over MCP
  ([#35], [#36]). <!-- PROVISIONAL 0038 -->

### Fixed

- **Fan-out:** an output wired to several inputs, directly or through a knot, kept only one of its connections in
  the N2C JSON, so the LLM invented the other inputs' values (issue #13). Every connection is now sent, based on
  PR #7 by Yury Rakhuba (card 0135). The JSON reader also reads exec pins, structs and enums again.
- **Exec wiring:** the N2C JSON recorded exec wires as node pairs, so the LLM could not tell a Branch's True from its
  False, a Switch's cases, a loop's body from its Completed, or which of a Gate's or Timeline's inputs an event
  ran, and two wires into one node were one entry. Each wire now names its output and input pin (card 0136).
- **MCP security:** requests with a non-local `Host` or `Origin` are refused and listeners bind to loopback;
  file tools refuse the secrets store, `.git` and `.env`, and the path jail resolves junctions and symlinks;
  saved scripts and LLM-supplied graph names cannot leave their folders (fa1f145, f9ab31a, 5ec3add, 4f2b6cc, [#42]).
- **Per-user MCP switches:** a project's committed `Config/DefaultEditorPerProjectUserSettings.ini` can no longer
  turn on **Allow MCP Python Execution** (or set **Enable MCP Server**); only your own Saved/Config value counts, and
  an ignored project value is logged as a warning ([#90]).
- **MCP request size:** a request body over 8 MiB to `/mcp` is answered 413 with a JSON-RPC -32600 error before it
  is copied or parsed; before, a body of any size was parsed on the game thread ([#91]).
- **Crashes:** the active LLM service survives garbage collection; the logger is thread-safe; input that crashed
  the editor returns errors; only real Blueprint editors are treated as such; struct utilities return errors
  instead of fatal asserts; theme edits no longer free brushes an open code editor uses, and `SelectText` is
  clamped (e8bdbc4, 57f7a42, 9a091f8, e2b93db, 3516fd4, [#43]).
- **Lost work:** `search-blueprint-nodes` no longer spawns and deletes nodes in your graph; `connect-pins` and the
  Python `ConnectPins` no longer cut an existing link unless asked to; MCP edits mark the Blueprint unsaved
  (04c0b3d, [#29], [#46], [#58]).
- **Files:** the graph-state file and the script registry are written atomically, and a file that does not parse
  is kept as `.corrupt-<time>` instead of being written over ([#48], [#49]).
- **Translation state:** translation-in-progress flags clear on every path; node capture for MCP tools no longer
  reads a finished capture's graph (0fc7301, [#28]).
- **MCP tasks:** translations run on the game thread with real cancel; finished tasks are released, and running
  ones are cancelled when the editor exits; `ExecuteOnGameThread` is safe when its caller times out
  ([#50], [#52], [#53]).
- **Translation errors:** an HTML page or a text error with quotes from a provider or proxy no longer turns into
  invalid JSON, and a provider's 429, 503 or billing error is no longer reported as "Failed to parse LLM
  response". An answer cut off at the output limit is a Truncated error that names the provider's **Max Output
  Tokens** setting; refusals and safety blocks are named; a valid translation whose code has a `{` inside a
  string is no longer rejected; an empty `content` no longer fails before the reason is read; a request that
  fails before it is sent no longer leaves a batch waiting ([#70], [#71]).
- **Translation overrides over MCP:** the provider and language IDs that `get-available-llm-providers` and
  `get-available-translation-targets` return now work in `translate-focused-blueprint`, in any case. An unknown
  ID is an `isError` result naming the valid ones; before, an agent asking for `rust` silently got C++ from the
  default provider. A model ID the registry does not list is still sent, with a `warnings` entry ([#79]).
- **Python tool results:** `run-python` and the script-library tools return `isError` when they fail, instead of
  a success with `success:false` inside ([#86]).
- **Apostrophes in the Python bridge:** a name, value or message with an apostrophe (`Tag 'x' not found`, a
  comment "Player's input") made the bridge answer invalid JSON, so the `nodetocode` module reported a failure
  after the edit was made and agents retried and made duplicates. Bridge results are now real JSON ([#87]).
- **MCP port conflicts:** a second editor no longer says its server started when the port was taken (its agent then
  edited the first editor's project). The server takes the next free port in **Port Fallback Range** (default 10)
  and says so in a notification and in **Server Status**, or shows an error naming the port and **Server Port**;
  changing **Server Port** restarts it on the new port at once. A discovery file
  (`Saved/NodeToCode/mcp-server.json`) names the port, project and process. Stopping the server no longer stops
  every HTTP listener in the editor, so Remote Control and UE 5.8's Unreal MCP keep running ([#83]).
- UE 5.8 compile errors and UE 5.7 deprecation warnings (8f16836); synchronous Blueprint compiles to avoid
  preview-actor crashes (d86db38).
- A missing include in `N2CMcpAddFunctionEntryPinTool.cpp` (deca5c3), the fix first proposed in [#9].

### Breaking changes for 1.2.x projects

Each item needs action from you. The [migration matrix](Context/release/migration-1.2-to-2.0.md) has every
setting, file and API, row by row.

- **Engine:** UE 5.6 or newer ([#38]). <!-- PROVISIONAL 0052 -->
- **Platform:** Win64 only. Mac users stay on 1.2.x for now ([#65]). <!-- PROVISIONAL 0073 -->
- **Editor UI:** assets of yours that reference `/NodeToCode/UI/...` lose those references. An Editor Utility
  Widget that embeds `UN2CMainWindowWidget`, `UN2CTagManagerWidget` or `UN2CCodeEditorWidget` still loads; move off
  them before they are removed ([#61]).
- **Code editor Blueprints:** reconnect the `GetSelection` output pins (`OutStartIndex` / `OutEndIndex` are now
  `OutStartLine` / `OutEndLine`) ([#43]), and remove calls to `SetTheme`, `GetAvailableThemes`,
  `GetThemeFromName` and `GetCurrentTheme` (a4f66d6).
- **Code themes:** per-language theme colours are not converted. Re-create them under **Theming > Code Editor
  Theme**; the old entries stay in `Config/DefaultNodeToCode.ini` so you can copy them ([#63]).
- **API keys:** keys are per user, not per project. A second project whose keys differ keeps the first project's
  keys; its own are saved under `MigrationConflicts` in the per-user file ([#42]). <!-- PROVISIONAL 0061 -->
- **Old translations:** folders that 1.2.x saved are left as they are but are not listed in the 2.0 window; open
  them from disk ([#62]).
- **N2C JSON readers:** a tool that parses N2C JSON must accept an array as a `flows.data` value (fan-out) and
  `"version": "2.0.0"`. C++ or Blueprint code that read `FN2CFlows::Data` as `TMap<FString, FString>` reads
  `FN2CDataFlowTargets::Targets` now (card 0135). They must also accept `flows.execution` entries of the form
  `"N3.P5->N4.P1"` (exec output pin to exec input pin) as well as 1.x node pairs `"N1->N2"` (card 0136).
  <!-- PROVISIONAL 0117 -->
- **C++ providers:** a provider that overrides `IN2CLLMService::SendRequest` must return
  `FN2CLLMRequestHandleRef` ([#51]). Code that used `UN2CUserSecrets` must use `FN2CSecretStore::Get()` ([#42]).

### Upgrading from 1.2.x

Open the project with 2.0 on UE 5.6 or newer. On the first start 2.0 updates what 1.2.x left, once, and lists each
change in the Output Log under **LogNodeToCode**; the second start logs none of it ([#42], [#32], [#63]):

- API keys move from `<Project>/Saved/NodeToCode/User/secrets.json` to the per-user file, and the project copy is
  deleted.
- A model the provider has retired becomes the provider's current default, saved to
  `Config/DefaultNodeToCode.ini`. The v1.2.3 default `Gemini_2_5_ProExp` no longer exists and becomes Gemini 3.8
  Flash.
- Price entries for retired models, or still at 1.2.x's built-in prices, are removed so current prices apply.
  Prices you entered for current models stay.
- Ollama: Context Window and Max Tokens saved at 1.2.x's 8192 go back to the server's default, sampler values you
  changed stay set, and **Use System Prompts** is turned on ([#34]).
- `HttpConnectionTimeout=300` and `HttpActivityTimeout=3600` under `[HTTP]` in `Config/DefaultEngine.ini` are
  removed. Other values there stay.
- **Use Less CPU when in Background** is left as it is, and a warning says so once.

If `Config/DefaultNodeToCode.ini` or `Config/DefaultEngine.ini` is read-only, the update runs again on the next
start; check the files out first. Card 0079 ran this with settings written by 1.2.3 and by `main` on UE 5.6, 5.7
and 5.8 ([#64]).

### Changes for earlier 2.0 development builds

Only for people who ran a `dev-2.0` build before this beta.

- Claude subscription and Google-account sign-in are removed. Saved sign-in settings are ignored with a warning
  and the provider's API key is used; sign-in tokens are deleted from the secrets file ([#35]).
  <!-- PROVISIONAL 0038 -->
- `run-python`, `save-python-script` and `delete-python-script` are listed only when **Allow MCP Python
  Execution** is on, and `bEnablePythonScriptOnlyMode` now defaults to false (01aa7d3). <!-- PROVISIONAL 0019 -->
- `translate-focused-blueprint` no longer answers `{status: "accepted", ..., sseUrl}` and sends the result on
  another port. Clients read the result from the call's own response ([#59]). <!-- PROVISIONAL 0085 -->
- `LMStudioMaxTokens` becomes `LMStudioMaxOutputTokens`; the old default 32768 becomes Auto ([#41]).
- `datatable_populator.populate_from_json` / `populate_from_csv` merge rows by name by default and save only
  with `save=True`. Pass `clear_existing=True, save=True` for the old replace-and-save behaviour ([#47]).
- The stdio bridge reads streamed responses and no longer follows `sseUrl` ([#60]).
- The second server on the MCP port + 1 (`/mcp/events`, `/mcp/notifications`) is gone, and so are
  `initialize`'s `_meta.notificationUrl` and the `nodetocode/taskStarted` notification. Only the MCP port is
  opened. `initialize` advertises `tools.listChanged: false`: the server has no channel for
  `notifications/tools/list_changed` ([#84]).
- Every request after `initialize` must send the `Mcp-Session-Id` that `initialize` returned: without one it is
  400, with an unknown or ended one 404. Before, one session was shared by every client and a wrong id was only
  logged. The `/mcp/sse` route is gone, and a client's JSON-RPC response gets 202 ([#81]).
- The script-library tools return the `nodetocode` function's result as it is, not wrapped in a second
  `{success, data}` ([#86]).
- C++ MCP tools: `IN2CToolAsyncTask`, `FN2CToolAsyncTaskBase` and the tool-name switch in
  `FN2CToolAsyncTaskManager` are removed; a long-running tool overrides `IN2CMcpTool::InvokeAsync` ([#79]).

### Known issues

- With **Enable Dynamic Tool Discovery** on, clients are not told when `assess-needed-tools` changes the tool list;
  they see the new tools when they list tools again, and the editor logs a warning saying so ([#84]).
- On UE 5.6 and 5.7, `translate-focused-blueprint` sends no progress, so a client that gives up on silent calls
  (Claude Code does after five minutes) can abort a long translation ([#59]). <!-- PROVISIONAL 0085 -->
- The secrets file is plain JSON in your user folder, not the OS keychain ([#42]).
- Settings load before the reference source files are checked, so the token estimate can be stale and missing
  reference files stay listed until edited; 1.2.x does the same ([#64]).
- Not verified live: Gemini 4 Argon ([#45]); refusals and safety blocks from a live provider (the tests use
  each provider's documented response bodies) ([#71]).
- The 413 for an oversized `/mcp` body is sent after Unreal's HTTP server has read the whole body into memory;
  the engine offers no earlier hook ([#91]).

### Credits

Thanks to the people whose pull requests are in this release:

- Dylan Martin ([@Subj3kt1](https://github.com/Subj3kt1)): portable platform code from #23, in [#65], and the
  Anthropic model update from #24, redone in [#32]. #24 also found that current Claude models reject
  `temperature`; [#30] (26e40ce) stops sending sampling parameters to every model that rejects them, and that
  commit did not credit #24 at the time.
- [@cryptosebek](https://github.com/cryptosebek): the LM Studio max-token setting from #20, which became Max
  Output Tokens in [#41], and the guarded LM Studio `usage`, `stats` and `model_info` reads and empty-content
  handling from #20, in [#71].
- [@YakovAU](https://github.com/YakovAU): the Copy Blueprint JSON command from #3 (c5177ab).
- [@flashpoint493](https://github.com/flashpoint493): the missing-include fix in #9, which landed in deca5c3
  without credit at the time.

[2.0.0-beta.1]: https://github.com/protospatial/NodeToCode/tree/dev-2.0
[#9]: https://github.com/protospatial/NodeToCode/pull/9
[#26]: https://github.com/protospatial/NodeToCode/pull/26
[#27]: https://github.com/protospatial/NodeToCode/pull/27
[#28]: https://github.com/protospatial/NodeToCode/pull/28
[#29]: https://github.com/protospatial/NodeToCode/pull/29
[#30]: https://github.com/protospatial/NodeToCode/pull/30
[#31]: https://github.com/protospatial/NodeToCode/pull/31
[#32]: https://github.com/protospatial/NodeToCode/pull/32
[#33]: https://github.com/protospatial/NodeToCode/pull/33
[#34]: https://github.com/protospatial/NodeToCode/pull/34
[#35]: https://github.com/protospatial/NodeToCode/pull/35
[#36]: https://github.com/protospatial/NodeToCode/pull/36
[#37]: https://github.com/protospatial/NodeToCode/pull/37
[#38]: https://github.com/protospatial/NodeToCode/pull/38
[#39]: https://github.com/protospatial/NodeToCode/pull/39
[#40]: https://github.com/protospatial/NodeToCode/pull/40
[#41]: https://github.com/protospatial/NodeToCode/pull/41
[#42]: https://github.com/protospatial/NodeToCode/pull/42
[#43]: https://github.com/protospatial/NodeToCode/pull/43
[#44]: https://github.com/protospatial/NodeToCode/pull/44
[#45]: https://github.com/protospatial/NodeToCode/pull/45
[#46]: https://github.com/protospatial/NodeToCode/pull/46
[#47]: https://github.com/protospatial/NodeToCode/pull/47
[#48]: https://github.com/protospatial/NodeToCode/pull/48
[#49]: https://github.com/protospatial/NodeToCode/pull/49
[#50]: https://github.com/protospatial/NodeToCode/pull/50
[#51]: https://github.com/protospatial/NodeToCode/pull/51
[#52]: https://github.com/protospatial/NodeToCode/pull/52
[#53]: https://github.com/protospatial/NodeToCode/pull/53
[#56]: https://github.com/protospatial/NodeToCode/pull/56
[#58]: https://github.com/protospatial/NodeToCode/pull/58
[#59]: https://github.com/protospatial/NodeToCode/pull/59
[#60]: https://github.com/protospatial/NodeToCode/pull/60
[#61]: https://github.com/protospatial/NodeToCode/pull/61
[#62]: https://github.com/protospatial/NodeToCode/pull/62
[#63]: https://github.com/protospatial/NodeToCode/pull/63
[#64]: https://github.com/protospatial/NodeToCode/pull/64
[#65]: https://github.com/protospatial/NodeToCode/pull/65
[#66]: https://github.com/protospatial/NodeToCode/pull/66
[#67]: https://github.com/protospatial/NodeToCode/pull/67
[#68]: https://github.com/protospatial/NodeToCode/pull/68
[#70]: https://github.com/protospatial/NodeToCode/pull/70
[#71]: https://github.com/protospatial/NodeToCode/pull/71
[#72]: https://github.com/protospatial/NodeToCode/pull/72
[#73]: https://github.com/protospatial/NodeToCode/pull/73
[#74]: https://github.com/protospatial/NodeToCode/pull/74
[#75]: https://github.com/protospatial/NodeToCode/pull/75
[#76]: https://github.com/protospatial/NodeToCode/pull/76
[#77]: https://github.com/protospatial/NodeToCode/pull/77
[#78]: https://github.com/protospatial/NodeToCode/pull/78
[#79]: https://github.com/protospatial/NodeToCode/pull/79
[#80]: https://github.com/protospatial/NodeToCode/pull/80
[#81]: https://github.com/protospatial/NodeToCode/pull/81
[#82]: https://github.com/protospatial/NodeToCode/pull/82
[#83]: https://github.com/protospatial/NodeToCode/pull/83
[#84]: https://github.com/protospatial/NodeToCode/pull/84
[#85]: https://github.com/protospatial/NodeToCode/pull/85
[#86]: https://github.com/protospatial/NodeToCode/pull/86
[#87]: https://github.com/protospatial/NodeToCode/pull/87
[#88]: https://github.com/protospatial/NodeToCode/pull/88
[#89]: https://github.com/protospatial/NodeToCode/pull/89
[#90]: https://github.com/protospatial/NodeToCode/pull/90
[#91]: https://github.com/protospatial/NodeToCode/pull/91
[#92]: https://github.com/protospatial/NodeToCode/pull/92
[#93]: https://github.com/protospatial/NodeToCode/pull/93
