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
  (commits from 241e31d to abb98cc, 2025-06-12 to 2025-07-06). NodeToCode registers 64 tools; with the default tool
  profile and Python execution off, `tools/list` returns 63 of them ([#68], [#148], [#202], [#257], [#290], [#293],
  [#294]).
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
  **Editor Preferences > Plugins > Node to Code (User)** ([#243]). The server does not start in commandlets or
  unattended runs unless `-N2CMcp` is passed (01aa7d3, [#26]). <!-- PROVISIONAL 0019 -->
- The server also speaks the stateless 2026-07-28 MCP revision on the same `/mcp` URL: `server/discover`,
  per-request `_meta` with header validation, `resultType`/`ttlMs`/`cacheScope` on results, and on UE 5.8
  `subscriptions/listen` and cancelling a call by closing its stream. Clients that use `initialize` and sessions
  behave as before; current Claude Code connects without a session ([#227]).
- `completion/complete` offers prompt argument values and Blueprint asset paths for `nodetocode://blueprint/{+path}`;
  `serverInfo` has a title, description, website and the plugin icon; resources carry `audience` and `priority`
  annotations; `nodetocode://python/unreal-stub` serves the editor's Python API stub. On UE 5.8 a 2026-07-28
  `subscriptions/listen` can subscribe to Blueprint resources and gets `notifications/resources/updated` after a
  compile, a save or a focus change ([#299]).
- `N2C.Mcp.WriteClientConfig` writes or merges NodeToCode's server into `.mcp.json`, `.cursor/mcp.json`,
  `.vscode/mcp.json`, `.gemini/settings.json` and `.codex/config.toml` with the server's current port, keeping other
  servers, and prints a Claude Desktop snippet; with **Require MCP Bearer Token** on, the files read the token from
  `NODETOCODE_MCP_TOKEN` and never hold it ([#222]). **Write MCP Client Config** in **Project Settings > Node to Code >
  MCP Server** does the same for the clients you pick, or copies the Claude Desktop snippet ([#262]).
- `place-cpp-translation` turns a saved C++ translation into a class in one of the project's C++ modules: a UCLASS
  header with the graph declarations, a `.cpp` with the implementations and the includes the Blueprint's calls need,
  and the modules added to the module's `Build.cs`. It is a dry run by default, writes only inside the chosen
  module's `Source` folder, and backs up every file it replaces ([#257]).
- `get-blueprint-dependencies` returns the dependency index of Blueprints, interfaces, macro libraries, structs and
  enums under asset paths or folders (typed edges, dependencies-first order, cycle groups), with a JSON export under
  `Saved/NodeToCode/Dependencies` ([#290]). `create-migration-plan` writes a reviewable Blueprint-to-C++ migration
  plan as a draft to `Saved/NodeToCode/Migration/<planId>/plan.json` and `plan.md`: steps in dependency order with
  each cycle group as one step, proposed native types and files in a target module, graphs with token estimates
  and risk flags; nothing else changes ([#293]).
- The Variables tools (create-variable, create-local-variable, the four variable node tools,
  get-blueprint-member-variables, get-blueprint-function-local-variables and both default-value tools) and
  tag-blueprint-graph take optional `blueprintPath`, plus `graphName` or `graphGuid` where a graph applies, and work
  with no Blueprint editor open ([#234]).
- get-focused-blueprint, get-blueprint-n2cjson (and Python `get_blueprint_n2c_json`) and find-nodes-in-graph return
  a `warnings` array (`code`, `path`, `message`) of what the capture found, for example a graph left out at Max
  Translation Depth (`DEPTH_LIMIT`) ([#232]).
- `list-blueprint-graphs` reports Animation Blueprint and Control Rig graph kinds (`animgraph`, `state-machine`,
  `state`, `transition`, `conduit`, `rig`) and a `translatable` flag per graph ([#260]).
- `translate-batch` reports cache read, cache write and reasoning token counts in its batch and per-item `usage`
  ([#277]).

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
  and a session-start line that says whether the editor answers ([#143]). The plugin has a listing icon
  (`.claude-plugin/icon.png`) and lists its privacy policy, the Data Flow and Privacy page ([#286], [#287]).
- Three more Agent Skills with scripts that run in the editor through `run-python`: `asset-iterator` (find and walk
  assets), `asset-validator` (naming by asset class, compile errors, Blueprints with no logic) and
  `project-folder-scaffold` (folder templates). The Claude Code plugin installs them with the other three, and UE
  5.8's skill registry lists them ([#298]).
- `Content/Python/mcp_bridge/manifest.json` and `server.json` describe the stdio bridge as an MCP Bundle (one-click
  install in Claude Desktop, optional project and URL settings) and as an MCP Registry entry; build and publishing
  steps are in that folder's README ([#235]).

**Python**

- The `nodetocode` module in `Content/Python/nodetocode`: Blueprint, graph-editing and editor-navigation helpers
  for the editor's Python (c2c76d7, 4fb31b9, a5d19da).
- `run-python` over MCP, listed only when **Allow MCP Python Execution** is on (7ae749d, 01aa7d3, [#294]).
  <!-- PROVISIONAL 0019 -->
- A stdio bridge, `Content/Python/mcp_bridge` (`launch_bridge.bat` / `launch_bridge.sh`), for MCP clients that
  only start stdio servers. It finds UE 5.6 to 5.8's Python and passes UTF-8 and streamed responses through
  (8d94a08, e775d97, [#60]). It runs calls side by side and forwards the client's cancel while a call runs, finds
  the editor through the MCP server's discovery file instead of scanning ports (`--project` or
  `NODETOCODE_PROJECT` picks one of several editors), and can start before the editor ([#85]). After an editor
  restart it opens a new session by itself, and it ends its session when the client closes stdin ([#81]).
- `nodetocode.get_translation_targets()` ([#147]); `nodetocode.list_tags(blueprint_path=...)` lists one
  Blueprint's tags, and `tag_graph` and `remove_tag` results include `blueprint_path` ([#198]).
- `nodetocode.invoke_tool(name, arguments)` and `UN2CPythonBridge::InvokeTool` run any NodeToCode MCP tool from
  editor Python, including tools the MCP server's tool profile does not serve ([#291]).
- The Python bridge's graph-edit, function-pin, tag, compile and save calls, and their `nodetocode` wrappers, take
  optional `blueprint_path` and `graph` (GUID or name) ([#234]). `nodetocode.get_blueprint_dependencies(paths, ...)`
  returns the dependency index ([#290]), and `nodetocode.skill_script(skill)` loads an Agent Skill's script ([#298]).

**N2C JSON and capture**

- N2C JSON can carry a Blueprint's member variables (`variables`) and each function's local variables
  (`local_variables`): name, type in pin spellings, default value, and for member variables the category,
  description and flags (instance editable, read-only, expose on spawn, private, replicated, RepNotify). A
  whole-Blueprint capture (get-blueprint-n2cjson and `get_blueprint_n2c_json` without a graph) includes them;
  translating a graph does not. Based on PR #8 by flashpoint493 ([#228]).
- Optional node fields `timeline` (length, loop, autoplay, track names and kinds), `bound_to` (the component or actor
  of a bound event) and `net`/`reliable` (a replicated custom event's RPC kind) ([#237]).
- `metadata` names the Blueprint's parent class (`parent_class`) and the kind of Blueprint asset (`asset_class`), so
  translations of Widget, Animation and Editor Utility Blueprints extend the right base class ([#278]); it may carry
  `asset_kind` ("Blueprint" or "Material"; absent means Blueprint) ([#284]).
- Materials and material functions (`asset_kind` "Material"), captured headless from the saved asset with
  `N2C::CaptureMaterial` / `CaptureMaterialFunction`: expressions, material outputs, reroutes traced, comments,
  unused expressions, parameters and settings, and material function calls within the capture depth ([#292]).
  Collapsed composites are followed and material-attribute pins are named by attribute; material layers, layer blends
  and function instances are refused one by one with `UNSUPPORTED_ASSET_TYPE` ([#295]). A Custom node's inputs are
  named as its code names them and its HLSL, defines and include paths are kept as data; inputs it cannot name warn
  `UNMAPPED_PIN`, layer and deprecated expressions warn `UNSUPPORTED_EXPRESSION`, asset references are object paths
  and `metadata.material.substrate` records whether the project uses Substrate ([#297]). Material capture gives the
  same JSON in Japanese and German, on a second capture and for a copy, and its unused flags match the engine's own
  reference walk ([#312]).
- All of these are additive; the format stays at version 2.0.0.
- Internal: `FN2CDependencyIndex`, the computed dependency index behind `get-blueprint-dependencies` and
  `create-migration-plan` ([#268]); `IN2CCodeVerifier`, a code verifier seam with a Python syntax verifier and a
  capture-coverage verifier, groundwork for a compile-repair loop that nothing calls yet ([#252]).

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
- A Get Started card in the Node to Code window walks a new user through provider, API key (saved in the system
  keychain), a connection test, target language and tagging a first graph; it hides after the first saved
  translation or when closed. The tag tree and the translation viewer say what to do when they are empty ([#271]).

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
- **Max Request Retries** (**Project Settings > LLM Provider**, default 3): a request answered with 429, 529 or
  another server error is sent again, after the wait the provider asks for (`Retry-After`) or a short capped backoff,
  before it fails. The retry and transport design was informed by the ideas in PR #21 by akashskypatel ([#311]).

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
- User guide: a Data Flow and Privacy page lists what each LLM provider receives, keeps and trains on (sourced, with
  dates), what stays on your machine and how to delete it, and that Node to Code sends nothing to Protospatial
  ([#269]). The window, translation viewer, N2C bar and colour settings pages describe the 2.0 UI ([#273]). The
  tool-mode page names the 33 tools that answer `PIE_ACTIVE`, including the tag tools and C++ placement ([#293]).
- A capture harness for the wiki's screenshots, `Tests/DocsShots/capture.py --group <group>` (automation tests
  `N2CDocs.Shots.*` drawing the plugin's widgets from fixture data at 2x on the 5.8 host), with a shots manifest for
  `tools/docs_check.py --stale` ([#240]).
- Unreal Insights CPU timers for capture, serialization, validation, token estimates and MCP tool calls
  (`N2C_Capture_CollectNodes`, `N2C_Serialize_ToJson`, `N2C_Validate`, `N2C_TokenEstimate`, `N2C_Mcp_Execute` with a
  per-tool child, ...); run with `-trace=cpu`, and see the Profiling section of the development-workflow guide
  ([#276]).
- Non-shipping builds accept `-N2CSecretPrefix=NodeToCodeTest/<guid>/`, which keeps API keys in test Credential
  Manager entries, so a hand check of Save Key or the API Key fields never touches your own `NodeToCode/*` entries
  ([#305]).

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
- MCP tools refuse a number, bool or object where a required string is expected; the variable tools refuse arguments
  their schema does not declare, naming them, and unknown `containerType`, `replicationCondition` and
  search-variable-types `category` values, listing the valid ones in any case; `variableName` follows the editor's
  Blueprint name rules ([#219]).
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
- **Per-user settings:** your provider, LM Studio endpoint, MCP server port, window colours, translation output
  folder and reference files outside the project are your own settings (**Editor Preferences > Plugins > Node to
  Code (User)**), saved outside the project instead of in `Config/DefaultNodeToCode.ini`; the project's Reference
  Source Files are stored relative to the project folder. The first start copies each developer's values from the
  project config to their own settings once (settings version 5) and lists the old keys, which stay in
  `DefaultNodeToCode.ini` for teammates and can be deleted once everyone has upgraded. A saved Text Muted of the old
  107 grey moves to the new 148 default ([#285]).
- **Translation archive:** translations are saved one folder per run,
  `<Blueprint>_<yyyyMMdd-HHmmss-fff>_<run id>/` (a batch: `BatchTranslation_..._<run id>/<graph>/`). Each run has a
  `manifest.json` (kind, provider, model, language, token usage, and every graph, struct and enum with its role and
  files) and one folder per entry of the reply; folders saved by earlier versions still load ([#226]). Translation
  records keep the run, its manifest, the exact files and the token usage ([#307]). A relative Custom Translation
  Output Directory is under the project folder, and get-translation-output-directory reports the default directory
  as an absolute path ([#226]). `translate-focused-blueprint` results include `run_folder` and `files` ([#264]).
- **Graph types:** NodeToCode refuses the graphs it cannot translate, keyed on the graph's schema
  ([decision 0008](.agents/decisions/0008-graph-types-every-k2-graph.md)): AnimGraphs and other animation pose graphs, animation
  states, state machines and Control Rig graphs. The capture and batch tools return `UNSUPPORTED_GRAPH_TYPE` naming
  the type, instead of empty code or `EMPTY_GRAPH`, and the graph overlay shows them as not supported ([#260]).
  Capturing a material or other non-Blueprint graph directly fails the same way instead of "No nodes to capture"
  ([#288]).
- A Blueprint capture fails only on a structural error (a dangling flow, a duplicate ID, a missing name); a
  recoverable problem is a `warnings` entry and no longer fails get-focused-blueprint, find-nodes-in-graph,
  get-blueprint-n2cjson or the Python bridge ([#232]).
- The `depth` argument of get-blueprint-n2cjson and export-n2cjson-batch says that collapsed graphs always come with
  their graph and that depth counts calls to your own functions and macros ([#279]).
- connect-pins and `nodetocode.connect_pins` results give each entry's `index`, the schema's `response` and
  `message`, and the `conversionNode` (GUID and title) when the link was made through one; the bridge's failed
  entries also carry an `errorCode` ([#229]).
- The `nodetocode` graph, function-pin, compile, save, `tag_graph` and `open_blueprint` functions run the matching MCP
  tool, so they behave exactly as the tools do, and their `data` is the tool's result. The 1.x result keys stay for
  one release and log a deprecation warning when read ([#291]).
- The Python scripting only tool profile serves `run-python` alone, **Allow MCP Python Execution** governs only
  `run-python`, and the `python-scripting` prompt takes only `task` ([#294]).
- On UE 5.8 a long-running tool call that accepts `text/event-stream` streams even without a `progressToken`, with a
  keep-alive comment every 2 s, so Epic's MCP Client Toolset (30 s HTTP activity timeout, no token) gets long
  translations through, and closing any streamed call cancels it ([#296]). The stdio bridge sends 2026-07-28 requests
  with their `MCP-Protocol-Version`, `Mcp-Method` and `Mcp-Name` headers and cancels them by closing their stream
  ([#227]).
- The translation prompts target Unreal Engine 5.6 to 5.8: C++ output uses `TObjectPtr` members, Enhanced Input and
  log categories and lists the headers it needs; every language prompt includes a worked example ([#272]).
- The Node to Code graph list's columns resize with their headers, sort by graph name, Blueprint or token estimate,
  select with click, Shift or Ctrl click, arrows, Space or Ctrl+A (the selection is the checked graphs), and offer
  Translate, View Translation, Export JSON, Open in Blueprint and Remove Tag on right-click ([#246]). Batch
  Translate, Export JSON and Remove Selected are disabled, with a tooltip saying why, until a graph is checked
  ([#271]).
- Muted text in the Node to Code window is brighter (4.5:1 contrast) and no text is smaller than 8pt; status,
  warning, close and Export JSON marks are editor icons instead of text glyphs; the toolbar and tab logo is an SVG
  that stays sharp at high DPI; UI clicks no longer write log lines at default verbosity ([#254]).
- Project Settings no longer offers to add or remove Code Editor Theme entries, as only one is drawn ([#230]).
- `Scripts/detect-ue-path.bat` and `.sh` (moved from `Content/Scripts`, so no package contains them) write this
  machine's Unreal Engine source path to the gitignored `AGENTS.local.md` instead of rewriting the tracked
  `AGENTS.md` ([#231]).
- Contributor docs: the root `AGENTS.md` is a short hub for coding agents; the build and test reference moved to
  `Context/development-workflow`, and new `Context` pages cover tags and graph state, UI and design tokens, auth and
  secrets, and the Agent Skills ([#239]). The MCP tools' agent docs link each tool's arguments to the generated tool
  reference, and the docs lint checks those lists ([#259]).
- Development: `build.ps1 -Test` starts the editor with `-RenderOffScreen`, so the Blueprint-editor and Material
  Editor specs run headless on UE 5.6, 5.7 and 5.8 ([#258]); the stdio bridge's unit tests moved from
  `Content/Python/tests` to `Tests/python`, so `build.ps1 -Test` runs them ([#300]).
- C++ API: the `UN2CTagManager` facade is removed; use `UN2CGraphStateManager` (`GetAllTags`, `GetTaggedGraphs`,
  `FindTaggedGraphs`, `FindTaggedGraphsInCategory`, `RemoveTagByName`). `OnGraphTagAdded` passes the added tag and
  its graph (`FN2CTagEntry`, `FN2CGraphRef`), and `FN2CTagInfo` moved to `Core/N2CTagTypes.h` ([#242]).
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
- The script library that earlier 2.0 development builds had: the MCP tools `list-python-scripts`,
  `search-python-scripts`, `get-python-script`, `get-script-functions`, `save-python-script` and
  `delete-python-script`, the `nodetocode` functions `list_scripts`, `search_scripts`, `get_script`,
  `get_script_functions`, `run_script`, `save_script`, `delete_script` and `get_script_stats`, and
  `script_registry.json` ([decision 0010](.agents/decisions/0010-script-library-cut-and-convert.md), [#294]). Scripts
  you saved stay in your project's `Content/Python/scripts`; to keep using one, make it an Agent Skill (a folder with
  a `SKILL.md` and the script under its `scripts/`) and run it through `run-python`.
- The bundled scripts under `Content/Python/scripts`. The Blueprint-authoring ones (blueprint_factory,
  variable_batch_creator, function_scaffolder, component_adder, ue5_compat, struct_creator, datatable_populator) are
  covered by the MCP tools ([#294]); asset_duplicator, asset_editor_opener and utility_widget_launcher are gone, and
  asset_iterator, asset_validator and project_folder_scaffold became Agent Skills, with their folder matching,
  prefix checks, compile check, empty-Blueprint check and counts fixed ([#298]).
- The unused `FN2CPinTypeCompatibility` and `FN2CPinDefinition::IsCompatibleWith`, whose rules disagreed with the K2
  schema ([#225]).

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
  and Anim Blueprints ([#206]), and it and `nodetocode.open_blueprint` fail with `GRAPH_NOT_FOUND`, listing the
  graphs, when `focus_graph` names no graph ([#218]); search-blueprint-classes lists each class once with real counts ([#211]); the struct
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
- **Python:** `nodetocode.read_n2c_json` read the document `get_blueprint_n2c_json` returns with every node
  dropped and reported success; it now keeps them, and fails, naming them, when it has to skip a malformed graph, node
  or pin ([#220]). The script library finds the bundled scripts when the plugin is installed from Fab or the Launcher
  ([#210]); `set_pin_value` reports a refused value as an error, and `delete_nodes` honours `preserve_connections`
  and lists what it skipped ([#191], [#195]).
- **Logs:** the Node to Code log file is UTF-8 through one open file ([#189]); keys and payloads stay out of logs
  ([#137]).
- **Translation archive:** a struct, enum or second graph in a reply no longer overwrites the translated graph's
  files; a batch item is saved once instead of twice; two translations of one Blueprint started in the same second
  no longer share one folder; struct and enum definitions are saved for C#, Swift, JavaScript and Pseudocode, not
  only C++; translations requested over MCP and from Python are saved to the translation output directory too
  ([#226]). `/nodetocode:translate` in the Claude Code plugin no longer writes a second copy of each translation; it
  reports the run folder the editor saved it in ([#264]). View Translation loads a translation whose folder holds
  more than one file of its language ([#307]).
- **N2C JSON:** custom events carry their name (they had `member_name` "None"); Switch nodes keep the engine's title
  ("Switch on E_Weapon") with the switched type in `member_name`; Get on an array names its element type; Timeline
  and async nodes (AI MoveTo, async actions) are `latent`, and async nodes name their factory class and function
  ([#237]). User enums keep every value (values named like "Max" or "MaxHealth" were dropped) and carry each value's
  number; user struct members of object, class, soft or interface types carry their `type_name`, a member of a user
  enum type is `Enum` naming the enum instead of `Byte`, and set elements and map keys are typed; a graph that uses
  one user struct or enum on several pins no longer logs ERROR "Missing name" ([#245]). A user struct with a member
  whose type name could not be read is kept, with a warning, instead of dropped ([#232]). Transition and conduit
  rules translate: their result node counts as the graph's result, so the nodes that compute the rule are no longer
  marked unused ([#260]).
- **Cost and context estimates:** they include the system prompt and reference files every request sends and price
  the expected reply at the model's output price, and the batch panel shows the input and output parts; the context
  bars count the prompt with the graph, so a graph that only fits without it no longer looks safe; the batch panel
  reprices the selection when a price, model or provider changes ([#236]).
- **MCP during long calls and dialogs:** on Windows, a translation started over MCP stops when the client that
  started it goes away (Claude Code's Esc or Ctrl+C), on UE 5.6 and 5.7 and for clients that do not accept a streamed
  response; before, the call ran to the end and its LLM request was billed ([#275]). While the editor shows a modal
  dialog the server keeps answering (connecting, tool lists, the health check, `read-file`, `read-path`) and other
  tool calls answer at once with `MODAL_DIALOG_OPEN` instead of waiting for the dialog to close ([#302]).
- **MCP tools:** `list-blueprint-tags` applies `graphGuid`, `tag` and `category` together, and Tag Blueprint Graph
  (Blueprint and Python `tag_blueprint_graph`) finds macro, delegate-signature and collapsed graphs by name ([#223]);
  search-variable-types no longer needs an open Blueprint editor, and tag-blueprint-graph no longer ignores a
  `blueprintPath` ([#234]); connect-pins and `nodetocode.connect_pins` list a malformed connection entry in `failed`
  with its index and `INVALID_REQUEST` instead of dropping it, and `summary.totalRequested` counts every entry
  ([#229]); the MCP type identifiers take `integer`, `integer64` and `real`, and
  `N2CStructUtilsLibrary.add_struct_variable` takes the same type strings as the MCP tools plus `array<T>`, `set<T>`,
  `map<K, V>` and `class<T>`/`softobject<T>`/`softclass<T>`/`interface<T>` ([#238]); the Blueprint graph, function and
  variable tools' toasts show a success or error icon instead of the missing-brush checkerboard ([#225]).
- **run-python:** code runs exactly as written in its own namespace: traceback line numbers are the code's,
  multi-line strings and `from __future__` work, a `.py` in a comment no longer makes it run nothing, a call no longer
  sees the previous call's `result`, and a modal dialog cannot block it. `result` is strict JSON (a UObject as its
  path, sets as lists), and a value JSON cannot hold fails with `result not JSON-serializable` ([#253]). `timeout`
  works: a script still running at the timeout is stopped and the call fails with `timed_out: true`, so
  `while True: pass` no longer freezes the editor. Each call is one undo step, "MCP: run-python"; a call that changes
  nothing adds none ([#301]).
- **Python:** `nodetocode.compile_blueprint(path)` and `save_blueprint(path)` act on the Blueprint at that path
  instead of the focused one, and their results include `blueprint_path` ([#244]).
- **Window and overlay:** Translate on a graph's N2C bar opens the Node to Code window when it is closed and runs the
  translation; before, the click did nothing ([#304]). A graph shown in two tabs has the overlay in both ([#261]). The
  overlay's context ring keeps its size and place at 150% and 200% DPI and dims when disabled ([#254]). Export JSON
  writes to the folder the window's Output field shows and says how many graphs it wrote, with an Open folder link,
  or why it did not run; Browse... saves that folder as your translation output folder ([#308]). The batch progress
  dialog shows each graph's own status when several graphs share a name, and names each graph's Blueprint ([#306]).
  The Get Started card and the empty tag tree say the N2C bar is at the bottom left of a graph ([#303]).
- **Code editor theme:** renaming the Code Editor Theme's "Unreal Engine" entry, or emptying the map, no longer turns
  off syntax highlighting ([#230]); a theme colour changed in Project Settings shows at once in an open translation
  view, syntax colours included ([#233]).
- **Settings pages:** each cloud provider has a working Remove Saved <Provider> Key button on its Saved Key row,
  greyed when no key is saved; the earlier buttons were never shown ([#310]). Editor Preferences and Project Settings
  no longer show developer text (C++ names, card numbers) in tooltips and section descriptions ([#309]). Docs,
  messages, the Agent Skill and the Claude Code plugin hook name **Editor Preferences** for the per-user MCP
  switches; they said Project Settings, where that section does not exist ([#243]).
- **Security:** the stdio bridge uses a discovery entry only when its URL is plain http to 127.0.0.1, localhost or
  [::1], so a planted `Saved/NodeToCode/mcp-server.json` in a downloaded project cannot send the MCP client's traffic
  to another host; `--url` still names any editor ([#283]).
- **Development:** a freshly created host passes `build.ps1 -Smoke` on its first run ([#258]);
  `Tests/mcp_probe/live_provider_matrix.py` records token usage again after [#181] changed the usage log line
  ([#224]).
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
- Your provider, LM Studio endpoint, translation output folder and the other per-user values are copied from
  `Config/DefaultNodeToCode.ini` to your own settings (**Editor Preferences > Plugins > Node to Code (User)**). The
  old keys stay in the project file for teammates still on 1.2.x and are listed in the log; delete them once everyone
  has upgraded ([#285]).

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
- Settings version 5: at the first start the per-user values (provider, LM Studio endpoint, MCP server port, window
  colours, translation output folder, reference files outside the project) are copied once from the project config
  to your own settings; the project config is not rewritten ([#285]).
- The script library and its tools are removed, and with them the bundled scripts (including `datatable_populator`,
  whose merge-by-name change was [#47]); see Removed ([#294], [#298]).
- The `nodetocode` graph, function-pin, compile, save, `tag_graph` and `open_blueprint` functions return the MCP
  tool's result as `data`; the 1.x keys still read for one release, with a deprecation warning ([#291]).
- Translations are saved in one folder per run with a `manifest.json`; folders saved by earlier builds stay as they
  are and still load ([#226]).
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
  `transactionId` ([#191]); `tools/call` with non-object `arguments` is an `isError` result ([#196]); open-blueprint-asset with an
  unknown `focus_graph` fails instead of opening with nothing focused ([#218]); a misspelled variable-tool argument
  is refused instead of ignored ([#219]).

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
  without credit at the time, and the member and local variable capture from #8, ported in [#228].
- [@akashskypatel](https://github.com/akashskypatel): the ideas in #21 that informed the HTTP transport seam and the
  429/529/5xx retries in [#311].

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
[#218]: https://github.com/protospatial/NodeToCode/pull/218
[#219]: https://github.com/protospatial/NodeToCode/pull/219
[#220]: https://github.com/protospatial/NodeToCode/pull/220
[#222]: https://github.com/protospatial/NodeToCode/pull/222
[#223]: https://github.com/protospatial/NodeToCode/pull/223
[#224]: https://github.com/protospatial/NodeToCode/pull/224
[#225]: https://github.com/protospatial/NodeToCode/pull/225
[#226]: https://github.com/protospatial/NodeToCode/pull/226
[#227]: https://github.com/protospatial/NodeToCode/pull/227
[#228]: https://github.com/protospatial/NodeToCode/pull/228
[#229]: https://github.com/protospatial/NodeToCode/pull/229
[#230]: https://github.com/protospatial/NodeToCode/pull/230
[#231]: https://github.com/protospatial/NodeToCode/pull/231
[#232]: https://github.com/protospatial/NodeToCode/pull/232
[#233]: https://github.com/protospatial/NodeToCode/pull/233
[#234]: https://github.com/protospatial/NodeToCode/pull/234
[#235]: https://github.com/protospatial/NodeToCode/pull/235
[#236]: https://github.com/protospatial/NodeToCode/pull/236
[#237]: https://github.com/protospatial/NodeToCode/pull/237
[#238]: https://github.com/protospatial/NodeToCode/pull/238
[#239]: https://github.com/protospatial/NodeToCode/pull/239
[#240]: https://github.com/protospatial/NodeToCode/pull/240
[#242]: https://github.com/protospatial/NodeToCode/pull/242
[#243]: https://github.com/protospatial/NodeToCode/pull/243
[#244]: https://github.com/protospatial/NodeToCode/pull/244
[#245]: https://github.com/protospatial/NodeToCode/pull/245
[#246]: https://github.com/protospatial/NodeToCode/pull/246
[#252]: https://github.com/protospatial/NodeToCode/pull/252
[#253]: https://github.com/protospatial/NodeToCode/pull/253
[#254]: https://github.com/protospatial/NodeToCode/pull/254
[#257]: https://github.com/protospatial/NodeToCode/pull/257
[#258]: https://github.com/protospatial/NodeToCode/pull/258
[#259]: https://github.com/protospatial/NodeToCode/pull/259
[#260]: https://github.com/protospatial/NodeToCode/pull/260
[#261]: https://github.com/protospatial/NodeToCode/pull/261
[#262]: https://github.com/protospatial/NodeToCode/pull/262
[#264]: https://github.com/protospatial/NodeToCode/pull/264
[#268]: https://github.com/protospatial/NodeToCode/pull/268
[#269]: https://github.com/protospatial/NodeToCode/pull/269
[#271]: https://github.com/protospatial/NodeToCode/pull/271
[#272]: https://github.com/protospatial/NodeToCode/pull/272
[#273]: https://github.com/protospatial/NodeToCode/pull/273
[#275]: https://github.com/protospatial/NodeToCode/pull/275
[#276]: https://github.com/protospatial/NodeToCode/pull/276
[#277]: https://github.com/protospatial/NodeToCode/pull/277
[#278]: https://github.com/protospatial/NodeToCode/pull/278
[#279]: https://github.com/protospatial/NodeToCode/pull/279
[#283]: https://github.com/protospatial/NodeToCode/pull/283
[#284]: https://github.com/protospatial/NodeToCode/pull/284
[#285]: https://github.com/protospatial/NodeToCode/pull/285
[#286]: https://github.com/protospatial/NodeToCode/pull/286
[#287]: https://github.com/protospatial/NodeToCode/pull/287
[#288]: https://github.com/protospatial/NodeToCode/pull/288
[#290]: https://github.com/protospatial/NodeToCode/pull/290
[#291]: https://github.com/protospatial/NodeToCode/pull/291
[#292]: https://github.com/protospatial/NodeToCode/pull/292
[#293]: https://github.com/protospatial/NodeToCode/pull/293
[#294]: https://github.com/protospatial/NodeToCode/pull/294
[#295]: https://github.com/protospatial/NodeToCode/pull/295
[#296]: https://github.com/protospatial/NodeToCode/pull/296
[#297]: https://github.com/protospatial/NodeToCode/pull/297
[#298]: https://github.com/protospatial/NodeToCode/pull/298
[#299]: https://github.com/protospatial/NodeToCode/pull/299
[#300]: https://github.com/protospatial/NodeToCode/pull/300
[#301]: https://github.com/protospatial/NodeToCode/pull/301
[#302]: https://github.com/protospatial/NodeToCode/pull/302
[#303]: https://github.com/protospatial/NodeToCode/pull/303
[#304]: https://github.com/protospatial/NodeToCode/pull/304
[#305]: https://github.com/protospatial/NodeToCode/pull/305
[#306]: https://github.com/protospatial/NodeToCode/pull/306
[#307]: https://github.com/protospatial/NodeToCode/pull/307
[#308]: https://github.com/protospatial/NodeToCode/pull/308
[#309]: https://github.com/protospatial/NodeToCode/pull/309
[#310]: https://github.com/protospatial/NodeToCode/pull/310
[#311]: https://github.com/protospatial/NodeToCode/pull/311
[#312]: https://github.com/protospatial/NodeToCode/pull/312
