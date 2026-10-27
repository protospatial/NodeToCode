# Changelog

Notable changes to Node to Code, newest first. Numbers in brackets are pull requests on
[protospatial/NodeToCode](https://github.com/protospatial/NodeToCode/pulls); short hashes are commits on `dev-2.0`.
Notes for 1.0.0 to 1.2.3 are on the [GitHub releases](https://github.com/protospatial/NodeToCode/releases) page
and in the wiki's [Latest Updates](wiki/Latest-Updates.md).

<!--
Owner, before tagging (card 0083): lines marked PROVISIONAL follow a review recommendation for an owner decision
that is not confirmed yet. Edit them if the decision changes, then delete these comments.
  0052 engines 5.6-5.8 | 0053 licence and DCO | 0073 Win64 only | 0085 5.6/5.7 MCP transport
  0019 run-python off by default | 0038 subscription sign-in removed | 0061 key storage (Windows Credential Manager,
  option (a)) | 0115 model catalog and string model IDs | 0117 N2C JSON v2 | 0140 CRUD tool families frozen
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
  (commits from 241e31d to abb98cc, 2025-06-12 to 2025-07-06). With the default tool profile and Python execution
  off, `tools/list` returns 67 tools ([#68], [#148], [#202]).
- `get-blueprint-n2cjson` and `list-blueprint-graphs` read any Blueprint by asset path with no Blueprint editor
  open; a document over about 20,000 tokens is written to `Saved/NodeToCode/Exports` and the call returns its path
  ([#130]). `export-n2cjson-batch` exports many graphs (by asset path, whole Blueprints or tag) as N2C JSON files,
  and `translate-batch` translates many graphs by path or tag with the call's provider, model and language, with
  progress and cancel; non-Blueprint assets are refused per target with `UNSUPPORTED_ASSET_TYPE` ([#146]).
- `get-editor-context`: the focused Blueprint and graph, the selected nodes' GUIDs, every open Blueprint editor with
  the graph it shows, Play In Editor state and the engine version, in one read-only call ([#202]).
- The Graph and Functions tools take optional `blueprintPath`, `graphName` and `graphGuid`, so an agent can name the
  graph it edits instead of relying on editor focus. They resolve every graph kind, refuse read-only graphs, answer
  typed errors (`GRAPH_NOT_FOUND`, `AMBIGUOUS_GRAPH_NAME`, `READ_ONLY_GRAPH`, ...) and echo the `target`; function
  IDs are stable across renames and moves ([#126]).
- Tools are grouped in twelve declared toolsets. **Project Settings > MCP Server** has **Tool Profile** (Default
  toolsets, Python scripting only, or Custom toolsets) and **Tool Exposure** (list every tool, or tool search with
  `list_toolsets`, `describe_toolset` and `call_tool`, named like UE 5.8's tool search, for clients with a tool
  limit). The tool list a client sees never changes while it is connected
  ([#148], [decision 0005](.agents/decisions/0005-mcp-tool-exposure-toolsets-and-tool-search.md)).
- Every tool has a title and all four behaviour hints (`readOnlyHint`, `destructiveHint`, `idempotentHint`,
  `openWorldHint`), and its category in `_meta["nodetocode/category"]`; `tools/list` is sorted by name and built once
  at registration ([#138], [#171]). `get-focused-blueprint`, `read-file` and `translate-focused-blueprint` tell Claude
  Code it may keep results up to 200,000 characters ([#171]).
- The read tools (`get-focused-blueprint`, `list-blueprint-graphs`, `get-open-blueprint-editors`,
  `translate-focused-blueprint`, `get-available-translation-targets`, `get-available-llm-providers`,
  `get-translation-output-directory`, `read-path`) declare an `outputSchema` and return `structuredContent`. Every
  failed call carries a stable error code in `_meta["nodetocode/error"]` (`INVALID_ARGUMENT` with the argument's
  name, `PIE_ACTIVE`, `TOOL_FAILED`, `TRANSLATION_<KIND>`, ...) ([#196]). `tools/call` checks required arguments
  against the input schema before a tool runs ([#169]).
- An optional bearer token on `/mcp`, **Require MCP Bearer Token** (per user, off by default). The token is in
  `%LOCALAPPDATA%\NodeToCode\mcp-token`; `N2C.Mcp.CopyToken` copies it and `N2C.Mcp.RegenerateToken` replaces it. The
  stdio bridge sends it with no setup (the token file, `NODETOCODE_MCP_TOKEN` or `--token-file`) ([#209]).
- One Info line per MCP request in the Output Log: method, tool, status, `isError`, duration and the client's W3C
  `traceparent` ([#189]).
- An MCP tool reference generated from the registry, `Context/mcp-server/tools.generated.md` and `.json`, by the
  `NodeToCode.N2CToolReference` commandlet (`Scripts/generate_tool_reference.py`); `build.ps1 -Test` fails while it is
  stale ([#154]).
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
- **Enable MCP Server** (on by default) and **Allow MCP Python Execution** (off by default), both per user under
  **Project Settings > Plugins > Node to Code (User)**. The server does not start in commandlets or unattended
  runs unless `-N2CMcp` is passed (01aa7d3, [#26]). <!-- PROVISIONAL 0019 -->

**UE 5.8 integration**

- With the Toolset Registry plugin on, NodeToCode registers a `NodeToCodeTools` Python toolset, so Unreal MCP and
  other registry clients can read any Blueprint as N2C JSON, export tagged graphs, manage tags, list languages and
  providers, and translate: `translate_graph` (async), `translate_graphs_by_tag`, `get_batch_status` and
  `cancel_batch` ([#147], [#152]). UE 5.6 and 5.7 are unaffected.
- When the Unreal MCP and EditorToolset plugins are on, NodeToCode leaves the 34 tools Epic's EditorToolset also has
  to Unreal MCP by default; **Overlapping Tools** (Automatic, Leave them to Unreal MCP, Always serve them) changes
  that, and a client that calls a left-out tool is told which Epic tool to use ([#151]).
- NodeToCode's Agent Skills are registered in Epic's skill registry, so Unreal MCP clients find them with no copying
  ([#155]).
- Mounting NodeToCode in Epic's registry through `MCPClientToolset` is tested and documented ([#118]).

**Agent Skills and Claude Code plugin**

- Three Agent Skills for coding agents, `blueprint-comprehension`, `blueprint-to-cpp` and `blueprint-migration`, in
  `Agents/claude-code/nodetocode/skills/` ([#139]). They name Epic's tools for the operations UE 5.8 leaves to
  Unreal MCP, and translate by asset path ([#155]).
- A Claude Code plugin, `Agents/claude-code`: `claude plugin marketplace add <Plugins/NodeToCode>/Agents/claude-code`
  and `claude plugin install nodetocode@nodetocode` give the MCP server config, the skills, `/nodetocode:translate`
  and a session-start line that says whether the editor answers ([#143]).

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
- `nodetocode.get_translation_targets()` ([#147]); `nodetocode.list_tags(blueprint_path=...)` lists one
  Blueprint's tags, and `tag_graph` and `remove_tag` results include `blueprint_path` ([#198]).

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
- **Prune Missing** in the Node to Code window removes the tags of deleted Blueprints ([#185]). Remove Selected
  asks for confirmation with the number of tags and graphs, and its notification has an Undo button ([#208]).
- The translation window's JSON tab shows the Blueprint's N2C JSON after a translate and when a saved translation is
  reopened, highlighted as JSON ([#193], [#203]).

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
- A bundled model catalog keyed by model ID ([#108]); see Changed for how Project Settings uses it ([#109]).
- **HTTP Activity Timeout (s)** (default 300): a provider that accepts a request and then stalls fails with a
  retryable `Timeout` error instead of holding a translation or batch for up to an hour ([#122]).
- Gemini also reads its key from `GOOGLE_API_KEY` when `GEMINI_API_KEY` is not set, and `get-available-llm-providers`
  reports where each key comes from (`keySource`: `saved`, `environment` with `keyVariable`, or `none`), never the
  key ([#141]).
- Saved translations and `BatchSummary.json` record cache read and write tokens and reasoning tokens ([#181]).
- `n2c.LogLLMPayloads` (default 0, never saved): payloads, prompts, N2C JSON, model answers and HTTP error bodies are
  logged only when it is 1; otherwise their size is ([#137]).

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
- User guide pages: Connecting an MCP Client (Claude Code, Cursor, VS Code, Codex, Claude Desktop through the stdio
  bridge, Epic's MCP Client Toolset), Choosing a Tool Mode, Security Model and Upgrading from 1.x ([#160],
  [#161]). The MCP, Python, tool and code-editor docs were checked against the code and corrected ([#110], [#112],
  [#113], [#115], [#117], [#194]). The user docs live in `wiki/`
  ([decision 0006](.agents/decisions/0006-user-docs-location-wiki-in-repo.md), [#156]); the Blueprint and Content Browser CRUD tool families are frozen, bug fixes only
  ([decision 0007](.agents/decisions/0007-crud-tool-families-fix-then-freeze.md), [#157]).
  <!-- PROVISIONAL 0140 -->
- A docs-lint CI job checks the agent docs against the generated tool reference, and the reference against the tool
  sources, on every pull request (`Scripts/docs_lint.py`) ([#159]).
- Tests: golden-file specs for every provider's request payload and response parser and for the captured N2C JSON of
  the core graph shapes, user types, recursion, copy-paste, nested graphs and input events ([#95], [#96], [#97]);
  an MCP tool spec harness with Graph, Functions, type-table, Structure, Content and run-python specs ([#133],
  [#134], [#153], [#204], [#215]); specs for the key store, the 1.2.x settings upgrade, StructUtils, config layers,
  graph state and windowed editor integration ([#149], [#162], [#188]); 100, 1k and 5k node Blueprint fixtures in a
  test-only content plugin, `Tests/NodeToCodeTestContent` ([#187]); `build.ps1 -Test` on UE 5.8 runs the
  NodeToCodeTools Python toolset tests ([#163]), and `Tests/python/editor/run_editor_tests.py` runs the in-editor
  Python tests ([#210]). The live provider matrix translates issue #13, a Gate and the viewer load path per language
  ([#119]); the live probes use a wired probe Blueprint and headless editors run with `-RenderOffScreen` ([#111]).
- `build.ps1` skips an engine only when an editor is open on its own host project, gives UnrealBuildTool a log per
  run and retries a build blocked by another build's shared files, so several checkouts can build at once ([#144]).
  The smoke and test editors use the file-system DDC and skip UE 5.8's startup UBT call ([#199]).
- Evidence for later decisions: a graph-type coverage matrix and K2 node inventory for UE 5.6 to 5.8 ([#174]), and a
  material expression and asset class inventory ([#175]).
- Shell scripts (`*.sh`) keep LF line ends in checkouts and release packages ([#143]).

### Changed

- N2C JSON is version 2.0.0 ([decision 0003](.agents/decisions/0003-n2c-json-v2-contract.md)): when one output
  feeds several inputs, its `flows.data` value is an array of every input; a graph without fan-out gets the same
  JSON as 1.0.0. 1.0.0 documents (1.2.x saves) still read, also from Python with `nodetocode.read_n2c_json`
  ([#102]). Each `flows.execution` entry is one exec wire, from the output pin to the input pin it runs
  (`"N3.P5->N4.P1"`), instead of a node pair ([#103]). Names are identifiers that do not change with the editor
  language or settings: each node has a `class`, a pin's `name` is its internal name (`execute`, `then`,
  `ReturnValue`), and the display text is an English `title` (nodes, replacing `name`) or `label` (pins) hint, given
  only when it says more than the identifier ([#105]). <!-- PROVISIONAL 0117 -->
- More N2C JSON 2.0.0: the editor's greyed-out default events (ghost nodes) are left out, and nodes are flagged
  `unused`, `unreachable` or `disabled` when they are ([#104]). Graphs carry their comment boxes (`comments`), and
  graph types come from the graph's schema ([#170]). Node types follow the nearest mapped base class; Break Struct,
  Enhanced Input action events, local variable Get/Set, For Each (Map/Set) and To Text get their own types, and an
  unmapped node is `Unknown` with its `class` instead of `CallFunction` ([#167]). Map pins carry their value type,
  and enum, Target and bitmask pins are typed `Enum`, `Self` and `Bitmask` ([#179]). The reader keeps unknown
  blueprint, graph, node and pin kinds and unknown fields and writes them back, so a later 2.x document round-trips
  ([#132]). <!-- PROVISIONAL 0117 -->
- The six CodeGen prompts and the batch spec are built from one shared N2C JSON spec,
  `Content/Prompting/Shared/N2CJsonSpec.md`, that matches what the serializer writes ([#106]).
- Blueprint text in MCP output (comments, string literals, names) is marked as untrusted data, and the MCP prompts
  use the CodeGen prompts ([#107]).
- Default models: Claude Sonnet 5.5, GPT-6.1 Sol, Gemini 3.8 Flash and DeepSeek Flash with thinking ([#32], [#33]).
- DeepSeek's `deepseek-chat` and `deepseek-reasoner` were discontinued, so the saved R1 and V3 choices both send
  `deepseek-flash`, R1 with thinking on and V3 with it off ([#33]).
- Project Settings selects each cloud provider's **Model** by model ID from the bundled model catalog, so a new
  model is a catalog edit, not a plugin update; **Custom Model ID** sends an ID the catalog does not list, and
  DeepSeek's **Thinking** sets its thinking mode. Price overrides are one **Model Price Overrides** map keyed by
  model ID, and the cost getters fall back to the catalog price ([#108], [#109]). <!-- PROVISIONAL 0115 -->
- Requests no longer send sampling parameters or schema fields that current models reject. The OpenAI JSON
  schema is sent as strict, and the Gemini key goes in the `x-goog-api-key` header instead of the URL
  ([#30], [#31]).
- API keys are saved per user, outside the project. On Windows they are in Windows Credential Manager (entries
  `NodeToCode/<Provider>`), and keys from `%LOCALAPPDATA%\NodeToCode\secrets.json` or a project's
  `Saved/NodeToCode/User/secrets.json` are moved there at the first start, read back, and only then deleted. Mac and
  Linux keep the per-user file ([#42], [#145]). Every save and removal is read back and repeated until it holds
  ([#190]). The Project Settings key fields are masked and write-only, **API Key Source** is now **API Key Status**,
  and each provider has **Remove Saved Key**; `N2CSettings.SetApiKey` and `HasApiKey` are for Blueprints and Python,
  and nothing returns a key ([#145]). <!-- PROVISIONAL 0061 -->
- Each request reads its API key when it is sent, so a key entered, changed or cleared applies to the next request,
  also within a running batch ([#141]). Each provider sends its key in one header and never in a URL; Anthropic
  requests no longer carry a second `Authorization` header ([#137]).
- MCP tools that create, save, copy, move or rename assets and folders write only to `/Game` and the project's own
  plugins' content unless the per-user **Allow MCP Writes Outside Project** is on ([#165]). While Play In Editor or
  Simulate runs, tools that edit or compile Blueprints or change assets answer `PIE_ACTIVE` ([#150]).
- Each MCP tool call compiles each Blueprint it changed once, after its transactions close, and returns the compiler
  diagnostics as `compile`; comments never compile ([#124]).
- `read-file` returns at most 500 KB of whole lines per call (`offset`, `limit`, `truncated`, `nextOffset`) and reads
  text files only; `read-path` is paginated (`pageSize`, `cursor`, `filter`); both return project-relative paths
  ([#166]).
- A `translate-focused-blueprint` result over 200,000 characters is saved under
  `Saved/NodeToCode/Exports/Translations` and returned as a summary with a `resource_link` ([#196]).
- Target languages (ids, extensions, prompt keys) are one table, `Core/N2CTargetLanguage.h` ([#178]).
- Token estimates capture exactly what Translate sends, follow graph edits without a compile, and re-estimate only
  the compiled Blueprint's graphs ([#216]). Debug lines during capture are built only at Debug severity ([#189]).
- On graphs Node to Code cannot translate (such as state machines) the overlay's buttons are disabled with a tooltip
  that says why ([#182]). The tag tree's graph list names what it shows, lists a graph with several tags once, and
  search also matches tag and category names ([#197]).
- Python docstrings are coloured as strings, and lines over 2,000 characters are shown without highlighting ([#193]).
- Internal: capture runs through a per-call capture module instead of the `FN2CNodeTranslator` singleton ([#100]);
  MCP tools are described and called through one seam, `FN2CToolCatalog` ([#169]); UI colours live in
  `Core/Style/N2CUIColors.h` ([#168]); overlay injection is `FN2CGraphOverlayInjector` ([#205]).
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
- `assess-needed-tools` and the **Enable Dynamic Tool Discovery** and **Enable Python Script-Only Mode** settings,
  replaced by **Tool Profile** and **Tool Exposure** ([#148]).
- `FN2CLLMConfig::ApiKey`, `UN2CSettings::GetActiveApiKey` / `GetActiveApiKeyForProvider` and the `OutAuthToken`
  parameter of `IN2CLLMService::GetConfiguration` ([#141]); Blueprint and Python access to the `*_API_Key_UI`
  properties ([#145]).
- `FN2CEditorIntegration`'s unused graph-editor wrapper, toolbar command and dead translate, copy and theme entry
  points ([#205]).

### Fixed

- **Fan-out:** an output wired to several inputs, directly or through a knot, kept only one of its connections in
  the N2C JSON, so the LLM invented the other inputs' values (issue #13). Every connection is now sent, based on
  PR #7 by Yury Rakhuba ([#102]). The JSON reader also reads exec pins, structs and enums again.
- **Exec wiring:** the N2C JSON recorded exec wires as node pairs, so the LLM could not tell a Branch's True from its
  False, a Switch's cases, a loop's body from its Completed, or which of a Gate's or Timeline's inputs an event
  ran, and two wires into one node were one entry. Each wire now names its output and input pin ([#103]).
- **Editor language:** node and pin names in the N2C JSON were display text, so a Japanese, German or French editor,
  or the friendly-names preference, changed what the LLM and MCP clients saw. A variable's `member_parent` was its
  type (`real/double`) instead of the class that declares it, and names ending in `_C` (a variable `Temperature_C`,
  a function `Convert_C`) lost the suffix. Names are now stable identifiers ([#105]).
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
- **Issue #13, ghost and unused nodes:** an Actor Blueprint's greyed-out default BeginPlay, ActorBeginOverlap and
  Tick were sent as live events, disabled nodes as active code, and an unwired node could come back as a function's
  return value ([#104]). `nodetocode.get_focused_blueprint()` and Translate on a graph with only ghost events give a
  clear result instead of "Failed to translate collected nodes" ([#114]).
- **Capture:** Max Translation Depth counts per graph, so limits past 1 apply and a graph is taken at its shortest
  path ([#98]). Node and pin IDs are per object, so a pasted node and a function that calls itself get unique IDs,
  and nested graphs are deduplicated by identity, not name ([#101]). Collapsed graphs are translated at every depth,
  plugin and Game Feature content is followed, and a graph with a missing node no longer crashes translation
  ([#170]).
- **Saved translations and the viewer:** a translation is saved with the Blueprint captured when its request was
  sent, not one captured while it ran ([#99]). Saved translations open in every language, with their own tab,
  extension and highlighting ([#120], [#121]); `BatchSummary.json` records the language id, and a non-C++
  translation no longer shows an old `.h` ([#178]). Two graphs with the same name in one batch are saved in their own
  folders ([#146]).
- **LLM requests:** batch cancel stops the in-flight request; a batch records only its own results; a provider or
  model picked during a batch applies to the next item ([#122]). A reply in any code fence, or with text around it,
  parses; Gemini output counts include thinking tokens and Anthropic input counts include cached input ([#181]).
- **Focused Blueprint:** the focused Blueprint editor follows what you click, so focus-based MCP tools and
  `open-blueprint-asset` no longer act on a Blueprint you are not looking at ([#123]). Overlays go only into the
  graph editor that shows their graph, one each, and opening graphs no longer writes warnings ([#200]); tagged
  collapsed graphs and interface functions translate from the window ([#182]).
- **Tags and graph state:** a duplicated Blueprint no longer shares the original's tags and history, and renames and
  moves keep tags ([#185]); the tag-added event reports the added tag, and an empty category stores the tag under
  `Default` ([#164]); a wrong-shape `BlueprintGraphState.json` is set aside as `.corrupt-<time>` instead of being
  overwritten ([#192]); the tag tree keeps its selection across refreshes ([#197]); the Remove Tag node and the
  Python `remove_tag` take an optional Blueprint path for a duplicated graph ([#198], [#205]).
- **Settings:** a provider or model picked in the window no longer overrides later Project Settings changes, and
  dragging a theme colour no longer saves the file on every mouse move ([#173]). Missing reference source files are
  dropped at start with a warning and their token estimate is recomputed ([#173]); this closes a known issue of
  [#64].
- **Code editor:** strings end at their own closing quote, raw and triple-quoted strings highlight, and multi-character
  operators are one token ([#193]); word wrap wraps, tab size reaches the highlighting, and the UMG widget fires its
  text, cursor and selection events ([#212]).
- **MCP Blueprint tools:** type names such as `HitResult`, enums, plugin and `/Engine` types resolve to the right pin
  types (a bare struct name crashed UE 5.8) ([#116], [#217]); node search IDs are the spawner signature, so any
  found node can be added, and `contextSensitive: false` searches the target graph ([#125], [#131]); create-variable
  and create-local-variable set flags that survive a compile, in one undo step ([#127]); variable and comment nodes
  get GUIDs, one pin set and undo ([#128]); function pin tools regenerate the skeleton before updating call sites
  ([#129]); create- and delete-blueprint-function check names and arguments before changing anything and touch only
  this Blueprint's calls ([#201]); `set-input-pin-value` and member and local default-value tools refuse a value the
  engine refuses and keep the old one ([#191], [#207]); delete-blueprint-node's `preserveConnections` reconnects
  pass-through links ([#195]); create-blueprint-class makes real Widget Blueprints through `IAssetTools` ([#140]);
  component add and delete attach to the named parent and can be undone ([#142], [#213]); list-blueprint-functions
  lists events, list-overridable-functions follows the editor's rules ([#183]); open-blueprint-asset reports Widget
  and Anim Blueprints ([#206]); search-blueprint-classes lists each class once with real counts ([#211]); the struct
  utilities refuse clashing names and remove only the default member ([#214]).
- **MCP content tools:** move-, rename- and copy-asset check new names and report real results ([#172]); move-folder
  and rename-folder move empty folders and refuse moving a folder into itself ([#177]); search-content-browser
  searches plugin content and subclasses with real totals, and read-content-browser-path no longer moves your
  Content Browser ([#184]); save-blueprint never opens a dialog and saves under `-unattended` ([#186]); project
  plugin content is accepted ([#165]).
- **MCP resources and transport:** `nodetocode://blueprint/...` resolves any Blueprint by asset path and returns real
  N2C JSON, and template matching is exact ([#176]). GET, PUT and PATCH on `/mcp` get a 405 with no body, so a client
  closing its stream no longer logs `socket_send_failure` ([#180]). With the old dynamic discovery, 15 tools could
  never be enabled and enabling tools changed every client's list ([#148]).
- **Python:** the script library finds the bundled scripts when the plugin is installed from Fab or the Launcher
  ([#210]); `set_pin_value` reports a refused value as an error, and `delete_nodes` honours `preserve_connections`
  and lists what it skipped ([#191], [#195]).
- **Logs:** the Node to Code log file is UTF-8 through one open file ([#189]); keys and payloads stay out of logs
  ([#137]).
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
- **API keys:** keys are per user, not per project, and on Windows they are in Windows Credential Manager. A second
  project whose keys differ keeps the first project's keys; its own are saved under `MigrationConflicts` in the
  per-user file ([#42], [#145]). Blueprints and Python set keys with `N2CSettings.SetApiKey` and can no longer read
  them ([#145]). <!-- PROVISIONAL 0061 -->
- **Old translations:** folders that 1.2.x saved are left as they are but are not listed in the 2.0 window; open
  them from disk ([#62]).
- **N2C JSON readers:** a tool that parses N2C JSON must accept an array as a `flows.data` value (fan-out) and
  `"version": "2.0.0"`. C++ or Blueprint code that read `FN2CFlows::Data` as `TMap<FString, FString>` reads
  `FN2CDataFlowTargets::Targets` now ([#102]). They must also accept `flows.execution` entries of the form
  `"N3.P5->N4.P1"` (exec output pin to exec input pin) as well as 1.x node pairs `"N1->N2"` ([#103]). A node's
  display title is `title` (optional) instead of `name`, nodes have a `class`, pin names are `PinName`s with an
  optional `label`, and a variable's `member_parent` is its owning class ([#105]). C++ code that read
  `FN2CNodeDefinition::Name` reads `Title` (a hint) or `NodeClass`/`MemberName`. <!-- PROVISIONAL 0117 -->
- **N2C JSON node and pin types:** ghost nodes are left out, an unmapped node is `Unknown` instead of
  `CallFunction`, and enum pins are `Enum` instead of `Byte` ([#104], [#167], [#179]). <!-- PROVISIONAL 0117 -->
- **Model settings in Blueprints, Python or C++:** `AnthropicModel`, `OpenAI_Model`, `Gemini_Model`,
  `DeepSeekModel` and the four `*ModelPricing` maps are gone from `UN2CSettings`. Read `AnthropicModelId` (and
  the others, plus `DeepSeekReasoningEffort`) and `ModelPricing`, or call `GetCurrentInputCost` /
  `GetCurrentOutputCost`. The model enums and per-provider pricing structs stay as deprecated types ([#108], [#109]).
  <!-- PROVISIONAL 0115 -->
- **C++ providers:** a provider that overrides `IN2CLLMService::SendRequest` must return
  `FN2CLLMRequestHandleRef` ([#51]). Code that used `UN2CUserSecrets` must use `FN2CSecretStore::Get()` ([#42]).
  Code that read `FN2CLLMConfig::ApiKey` or called `UN2CSettings::GetActiveApiKey` must drop it: keys are read
  per request through the credential source ([#141]).

### Upgrading from 1.2.x

Open the project with 2.0 on UE 5.6 or newer. On the first start 2.0 updates what 1.2.x left, once, and lists each
change in the Output Log under **LogNodeToCode**; the second start logs none of it ([#42], [#32], [#63]):

- API keys move from `<Project>/Saved/NodeToCode/User/secrets.json` to Windows Credential Manager (the per-user
  file on Mac and Linux), are read back, and only then is the project copy deleted ([#145]).
- Model selections are saved again as model IDs (`AnthropicModelId=...`), and the old entries are removed from
  `Config/DefaultNodeToCode.ini` ([#108], [#109]). A model the provider has retired becomes the provider's current
  default. The v1.2.3 default `Gemini_2_5_ProExp` no longer exists and becomes Gemini 3.8 Flash.
- Price entries for retired models, or still at 1.2.x's built-in prices, are removed so current prices apply.
  Prices you entered for current models move to **Model Price Overrides** under the model's ID.
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
- Model selections and price overrides saved by enum name are converted to model IDs once, at the first start
  (settings version 4), and the old keys are removed; a saved model the catalog does not list is kept as the
  provider's **Custom Model ID** ([#108], [#109]).
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
- Keys in `%LOCALAPPDATA%\NodeToCode\secrets.json` move to Windows Credential Manager at the first start ([#145]).
- **Enable Dynamic Tool Discovery** or **Enable Python Script-Only Mode** left on opens with Tool Exposure = tool
  search or Tool Profile = Python scripting only ([#148]).
- `BlueprintGraphState.json` is version 3, keyed by Blueprint and graph; version 2 is migrated at the first start
  ([#185]).
- Tool result and argument changes: a tool's category is in `_meta["nodetocode/category"]` ([#138]);
  create-variable's `isInstanceEditable` defaults to false ([#127]); the variable and comment node tools return the
  node GUID as `nodeId` ([#128]); `spawnMetadata.actionIdentifier` is the spawner signature ([#125]);
  create-blueprint-class has `ifExists` and `addPrefix` and no `generateConstructionScript` ([#140]);
  list-components-in-actor lists native and inherited components ([#142]); `read-file` and `read-path` drop
  `absolutePath` ([#166]); copy-asset drops `overwriteExisting` ([#172]); list-blueprint-functions drops
  `includeInherited` and `includeOverridden` ([#183]); read-content-browser-path results are camelCase and
  `sync_browser` defaults to false ([#184]); search-blueprint-classes' `includeAbstract` defaults to true ([#211]);
  get-open-blueprint-editors drops `isDirty`, `hasCompilerResults`, `isInEditingMode` and `isCompileEnabled` for
  `packageDirty` and `compileStatus` ([#202]); delete-blueprint-node and delete-blueprint-function drop
  `transactionId` ([#191]); `tools/call` with non-object `arguments` is an `isError` result ([#196]).

### Known issues

- On UE 5.6 and 5.7, `translate-focused-blueprint` sends no progress, so a client that gives up on silent calls
  (Claude Code does after five minutes) can abort a long translation ([#59]). <!-- PROVISIONAL 0085 -->
- On Mac and Linux the keys file is plain JSON in your user folder, not the OS keychain ([#42], [#145]).
- create-blueprint-class is not an undo step: undoing it would leave the `.uasset` on disk ([#140]).
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
[#95]: https://github.com/protospatial/NodeToCode/pull/95
[#96]: https://github.com/protospatial/NodeToCode/pull/96
[#97]: https://github.com/protospatial/NodeToCode/pull/97
[#98]: https://github.com/protospatial/NodeToCode/pull/98
[#99]: https://github.com/protospatial/NodeToCode/pull/99
[#100]: https://github.com/protospatial/NodeToCode/pull/100
[#101]: https://github.com/protospatial/NodeToCode/pull/101
[#102]: https://github.com/protospatial/NodeToCode/pull/102
[#103]: https://github.com/protospatial/NodeToCode/pull/103
[#104]: https://github.com/protospatial/NodeToCode/pull/104
[#105]: https://github.com/protospatial/NodeToCode/pull/105
[#106]: https://github.com/protospatial/NodeToCode/pull/106
[#107]: https://github.com/protospatial/NodeToCode/pull/107
[#108]: https://github.com/protospatial/NodeToCode/pull/108
[#109]: https://github.com/protospatial/NodeToCode/pull/109
[#110]: https://github.com/protospatial/NodeToCode/pull/110
[#111]: https://github.com/protospatial/NodeToCode/pull/111
[#112]: https://github.com/protospatial/NodeToCode/pull/112
[#113]: https://github.com/protospatial/NodeToCode/pull/113
[#114]: https://github.com/protospatial/NodeToCode/pull/114
[#115]: https://github.com/protospatial/NodeToCode/pull/115
[#116]: https://github.com/protospatial/NodeToCode/pull/116
[#117]: https://github.com/protospatial/NodeToCode/pull/117
[#118]: https://github.com/protospatial/NodeToCode/pull/118
[#119]: https://github.com/protospatial/NodeToCode/pull/119
[#120]: https://github.com/protospatial/NodeToCode/pull/120
[#121]: https://github.com/protospatial/NodeToCode/pull/121
[#122]: https://github.com/protospatial/NodeToCode/pull/122
[#123]: https://github.com/protospatial/NodeToCode/pull/123
[#124]: https://github.com/protospatial/NodeToCode/pull/124
[#125]: https://github.com/protospatial/NodeToCode/pull/125
[#126]: https://github.com/protospatial/NodeToCode/pull/126
[#127]: https://github.com/protospatial/NodeToCode/pull/127
[#128]: https://github.com/protospatial/NodeToCode/pull/128
[#129]: https://github.com/protospatial/NodeToCode/pull/129
[#130]: https://github.com/protospatial/NodeToCode/pull/130
[#131]: https://github.com/protospatial/NodeToCode/pull/131
[#132]: https://github.com/protospatial/NodeToCode/pull/132
[#133]: https://github.com/protospatial/NodeToCode/pull/133
[#134]: https://github.com/protospatial/NodeToCode/pull/134
[#137]: https://github.com/protospatial/NodeToCode/pull/137
[#138]: https://github.com/protospatial/NodeToCode/pull/138
[#139]: https://github.com/protospatial/NodeToCode/pull/139
[#140]: https://github.com/protospatial/NodeToCode/pull/140
[#141]: https://github.com/protospatial/NodeToCode/pull/141
[#142]: https://github.com/protospatial/NodeToCode/pull/142
[#143]: https://github.com/protospatial/NodeToCode/pull/143
[#144]: https://github.com/protospatial/NodeToCode/pull/144
[#145]: https://github.com/protospatial/NodeToCode/pull/145
[#146]: https://github.com/protospatial/NodeToCode/pull/146
[#147]: https://github.com/protospatial/NodeToCode/pull/147
[#148]: https://github.com/protospatial/NodeToCode/pull/148
[#149]: https://github.com/protospatial/NodeToCode/pull/149
[#150]: https://github.com/protospatial/NodeToCode/pull/150
[#151]: https://github.com/protospatial/NodeToCode/pull/151
[#152]: https://github.com/protospatial/NodeToCode/pull/152
[#153]: https://github.com/protospatial/NodeToCode/pull/153
[#154]: https://github.com/protospatial/NodeToCode/pull/154
[#155]: https://github.com/protospatial/NodeToCode/pull/155
[#156]: https://github.com/protospatial/NodeToCode/pull/156
[#157]: https://github.com/protospatial/NodeToCode/pull/157
[#159]: https://github.com/protospatial/NodeToCode/pull/159
[#160]: https://github.com/protospatial/NodeToCode/pull/160
[#161]: https://github.com/protospatial/NodeToCode/pull/161
[#162]: https://github.com/protospatial/NodeToCode/pull/162
[#163]: https://github.com/protospatial/NodeToCode/pull/163
[#164]: https://github.com/protospatial/NodeToCode/pull/164
[#165]: https://github.com/protospatial/NodeToCode/pull/165
[#166]: https://github.com/protospatial/NodeToCode/pull/166
[#167]: https://github.com/protospatial/NodeToCode/pull/167
[#168]: https://github.com/protospatial/NodeToCode/pull/168
[#169]: https://github.com/protospatial/NodeToCode/pull/169
[#170]: https://github.com/protospatial/NodeToCode/pull/170
[#171]: https://github.com/protospatial/NodeToCode/pull/171
[#172]: https://github.com/protospatial/NodeToCode/pull/172
[#173]: https://github.com/protospatial/NodeToCode/pull/173
[#174]: https://github.com/protospatial/NodeToCode/pull/174
[#175]: https://github.com/protospatial/NodeToCode/pull/175
[#176]: https://github.com/protospatial/NodeToCode/pull/176
[#177]: https://github.com/protospatial/NodeToCode/pull/177
[#178]: https://github.com/protospatial/NodeToCode/pull/178
[#179]: https://github.com/protospatial/NodeToCode/pull/179
[#180]: https://github.com/protospatial/NodeToCode/pull/180
[#181]: https://github.com/protospatial/NodeToCode/pull/181
[#182]: https://github.com/protospatial/NodeToCode/pull/182
[#183]: https://github.com/protospatial/NodeToCode/pull/183
[#184]: https://github.com/protospatial/NodeToCode/pull/184
[#185]: https://github.com/protospatial/NodeToCode/pull/185
[#186]: https://github.com/protospatial/NodeToCode/pull/186
[#187]: https://github.com/protospatial/NodeToCode/pull/187
[#188]: https://github.com/protospatial/NodeToCode/pull/188
[#189]: https://github.com/protospatial/NodeToCode/pull/189
[#190]: https://github.com/protospatial/NodeToCode/pull/190
[#191]: https://github.com/protospatial/NodeToCode/pull/191
[#192]: https://github.com/protospatial/NodeToCode/pull/192
[#193]: https://github.com/protospatial/NodeToCode/pull/193
[#194]: https://github.com/protospatial/NodeToCode/pull/194
[#195]: https://github.com/protospatial/NodeToCode/pull/195
[#196]: https://github.com/protospatial/NodeToCode/pull/196
[#197]: https://github.com/protospatial/NodeToCode/pull/197
[#198]: https://github.com/protospatial/NodeToCode/pull/198
[#199]: https://github.com/protospatial/NodeToCode/pull/199
[#200]: https://github.com/protospatial/NodeToCode/pull/200
[#201]: https://github.com/protospatial/NodeToCode/pull/201
[#202]: https://github.com/protospatial/NodeToCode/pull/202
[#203]: https://github.com/protospatial/NodeToCode/pull/203
[#204]: https://github.com/protospatial/NodeToCode/pull/204
[#205]: https://github.com/protospatial/NodeToCode/pull/205
[#206]: https://github.com/protospatial/NodeToCode/pull/206
[#207]: https://github.com/protospatial/NodeToCode/pull/207
[#208]: https://github.com/protospatial/NodeToCode/pull/208
[#209]: https://github.com/protospatial/NodeToCode/pull/209
[#210]: https://github.com/protospatial/NodeToCode/pull/210
[#211]: https://github.com/protospatial/NodeToCode/pull/211
[#212]: https://github.com/protospatial/NodeToCode/pull/212
[#213]: https://github.com/protospatial/NodeToCode/pull/213
[#214]: https://github.com/protospatial/NodeToCode/pull/214
[#215]: https://github.com/protospatial/NodeToCode/pull/215
[#216]: https://github.com/protospatial/NodeToCode/pull/216
[#217]: https://github.com/protospatial/NodeToCode/pull/217
