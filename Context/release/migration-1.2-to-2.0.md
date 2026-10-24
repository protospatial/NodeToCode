# Migration matrix: NodeToCode 1.2.x to 2.0

What happens to a 1.2.x project's settings, keys, saved files and integrations when it opens in a 2.0 build, one
row per item. Card 0079 uses it to prove a 1.2.3 project loads in 2.0, card 0082 takes its breaking changes into
the changelog, and E04, E09 and E10 use it to see what their upgraders must carry. Every row was read from the code
on dev-2.0 at 860182e and on `origin/main`; rows marked **Unverified** say what was not checked and why.

## Baselines and how to read the rows

- **1.2.x** is `origin/main` (cb1483a, "Merge branch 'v.1.2.3'", 2025-06-05), the 1.2.x maintenance line. The
  `v1.2.3` tag points at an older commit (c7aa4c0, 2025-04-08) without LM Studio, the Claude 4 / o3 / o4-mini /
  GPT-4.1 / Gemini 2.5 Pro and Flash enum values, and with different defaults. Rows say where the tag differs.
  **Unverified:** which of the two the Fab 1.2.3 package was built from (`.uplugin` says 1.2.2 on both).
- **2.0** is dev-2.0. dev-2.0 and main share no merge base, so behaviour was compared by reading both trees
  (`git show origin/main:<path>`).
- **Handling:** *Unchanged* (loads as it was), *Auto* (2.0 changes it on load, no action), *Reset* (the value is
  lost and the 2.0 default applies), *User* (the user must act), *New* (no 1.2.x counterpart; default applies).
- **Provisional** marks behaviour that applies a review recommendation for an owner decision card that is not
  confirmed: 0019 (run-python off by default; the board shows it done, but there is no decision record in
  `.agents/decisions/`), 0038 (subscription sign-in removed), 0061 (per-user secrets file), 0085 (5.6/5.7 MCP
  transport).
- 1.2.x had no MCP server, Python library, tag manager or batch translation. Changes to those since earlier 2.0
  development builds are in the last section; they do not affect a 1.2.x user.
- **Engine:** 2.0 builds for UE 5.6 to 5.8 only, so a 1.2.x project on 5.5 or older must upgrade the engine first.

## Settings: `[/Script/NodeToCode.N2CSettings]`

Both versions declare `UN2CSettings` as `Config = NodeToCode, DefaultConfig`, so the values live in the project's
`Config/DefaultNodeToCode.ini` (a local `Saved/Config/<Platform>Editor/NodeToCode.ini` layers on top in both). The
module, class and section names are unchanged. Values are stored by name; enum values by enumerator name.

General rules for every row:

- A key absent from the ini takes the 2.0 default, which differs from 1.2.x for the model fields. **Unverified:**
  whether a 1.2.x Project Settings save wrote every Config property or only the edited ones, so the 0079 fixture
  should set every key explicitly.
- An enum name 2.0 does not know fails `ImportEnumFromBuffer` (engine, `CoreUObject/Private/UObject/EnumProperty.cpp`, read on
  5.8, not run): the engine logs a warning and the field keeps its 2.0 default. No CoreRedirects `EnumRedirects` exist (the plugin
  has no Config ini besides `FilterPlugin.ini`).
- `MigrateRetiredModelSelections` (PR #32) runs from `PostInitProperties` and `PostReloadConfig`: a selection the
  model registry marks retired, or does not know, becomes the provider's `N2CDefaultModels` value and one
  `LogNodeToCode` warning lists every change ("Saved model selection no longer served by its provider, using the
  current default instead: ..."). The ini is not rewritten; it repeats on every start until the user picks a model.
- `MigrateSavedSettings` runs the layout migrations for `SettingsVersion` < 2 on every load; the first edit in
  Project Settings stamps `SettingsVersion=2` and saves the migrated values. One Info line lists the changes
  ("Settings saved before NodeToCode 2.0 updated so the server and model defaults apply: ...").

How 0079 checks a row: read the value from `unreal.get_default_object(unreal.N2CSettings)` in a headless 5.8
editor started on the fixture, and grep the editor log for the `LogNodeToCode` lines quoted below. Config-only
properties with no editor flags (`SettingsVersion`) are not visible to Python; read those from the ini.

| ID | 1.2.x key, type, default | 2.0 | Handling | Owner | Check |
|---|---|---|---|---|---|
| S1 | `Provider` (`EN2CLLMProvider`), Anthropic. Values OpenAI, Anthropic, Gemini, Ollama, DeepSeek, LMStudio (LMStudio not on the tag) | Same key and values | Unchanged | - | Value equals the fixture's |
| S2 | `AnthropicModel` (`EN2CAnthropicModel`), main Claude4_Sonnet, tag Claude3_7_Sonnet | Same key; every 1.2.x value is retired (see the enum table) | Auto: becomes Claude5_5_Sonnet with the warning above; User: pick a model to persist it | PR #32 done; E09 replaces enums with string IDs | Value is Claude5_5_Sonnet; warning names `AnthropicModel=<old>` |
| S3 | `OpenAI_Model` (`EN2COpenAIModel`), main GPT_o4_mini, tag GPT_o3_mini | Same key. Kept: GPT4o_2024_08_06, GPT4o_Mini_2024_07_18, GPT_4_1, GPT_o3. Retired: GPT_o1, GPT_o3_mini, GPT_o1_Preview, GPT_o1_Mini, GPT_o4_mini | Unchanged for kept values (each now deprecated, see the enum table); Auto to GPT_6_1_Sol with the warning for retired ones | PR #32; E09 | Kept value equals the fixture's; retired value reads GPT_6_1_Sol plus warning |
| S4 | `Gemini_Model` (`EN2CGeminiModel`), main Gemini_2_5_Flash, tag Gemini_2_5_ProExp | Same key. Gemini_2_5_ProExp no longer exists; every other 1.2.x value is retired | Tag's Gemini_2_5_ProExp: **Reset** silently to Gemini_3_8_Flash (engine import warning only, no NodeToCode warning, because the default it falls back to is not retired). Others: Auto to Gemini_3_8_Flash with the warning | 0079 adds an EnumRedirect or records the loss; E09 | Value is Gemini_3_8_Flash; for ProExp the engine's invalid-enum warning, for others the NodeToCode warning |
| S5 | `DeepSeekModel` (`EN2CDeepSeekModel`), DeepSeek_R1 (`deepseek-reasoner`); DeepSeek_V3 (`deepseek-chat`) | Same names; both now send `deepseek-flash` (R1 with thinking at high effort, V3 without thinking), since `deepseek-reasoner` and `deepseek-chat` were discontinued 2026-07-24 | Auto, silent (not retired, so no warning) | PR #33 done | Value equals the fixture's; a request's payload model is `deepseek-flash` |
| S6 | `OllamaConfig` (struct): OllamaEndpoint, bUseSystemPrompts (false), PrependedModelCommand (not on the tag), KeepAlive, Temperature, NumPredict (8192), TopP, TopK, MinP, RepeatPenalty, NumCtx (8192), Mirostat, MirostatEta, MirostatTau, Seed | Same members plus Think, ThinkLevel and one `bOverride_*` per sampler; options are sent only when set | Auto (settings version 1, PR #34): NumCtx 8192 and NumPredict 8192 become 0 (server default); bUseSystemPrompts false becomes true (a deliberate false is lost too); a sampler that differs from the old default gets its override turned on, one at the old default stays off. Endpoint, command and KeepAlive unchanged | PR #34 done | Fixture with NumCtx 8192, Temperature 0.7, bUseSystemPrompts False reads NumCtx 0, bOverride_Temperature True, Temperature 0.7, bUseSystemPrompts True; Info line lists them |
| S7 | `OllamaModel` (string), main `qwen3:32b`, tag `qwen2.5-coder:32b` | Same | Unchanged (an absent key now gives `qwen3:32b`) | - | Value equals the fixture's |
| S8 | `LMStudioModel` (string, main only) | Same | Unchanged | - | Value equals the fixture's |
| S9 | `LMStudioEndpoint` (string, main only) | Same | Unchanged | - | Value equals the fixture's |
| S10 | `LMStudioPrependedModelCommand` (string, main only) | Same | Unchanged | - | Value equals the fixture's |
| S11 | `OpenAIModelPricing` (map enum to input/output price), filled with 1.2.x prices by the constructor | Same key; the constructor no longer fills it, prices come from the model registry and a map entry overrides them | Unchanged, so any 1.2.x prices saved in the ini stay as overrides (for example o3 at 15/60). User: clear the map to use registry prices | E09 (pricing moves to the registry) | Map in the CDO equals the ini; `GetCurrentInputCost` returns the saved price |
| S12 | `AnthropicModelPricing` | As S11 | As S11; entries for retired models are unused | E09 | As S11 |
| S13 | `GeminiModelPricing` | As S11 | As S11; a tag entry keyed Gemini_2_5_ProExp cannot import. **Unverified:** whether that drops one entry or the whole map | E09 | As S11; check the map's size |
| S14 | `DeepSeekModelPricing` | As S11 | As S11 | E09 | As S11 |
| S15 | `TargetLanguage` (`EN2CCodeLanguage`), Cpp | Same key and values | Unchanged | - | Value equals the fixture's |
| S16 | `TranslationDepth` (int, 0) | Same | Unchanged | - | Value equals the fixture's |
| S17 | `MinSeverity` (`EN2CLogSeverity`), Info | Same | Unchanged | - | Value equals the fixture's |
| S18 | `CPPThemes` (`FN2CCodeEditorThemes`: map of theme name to 11 colors) | Not read. 2.0 has one `CodeEditorTheme` and draws the code editor with its "Unreal Engine" entry | Reset: custom themes are ignored. User: re-create colors under Theming > Code Editor Theme | - | Key still in the ini; `CodeEditorTheme` holds the 2.0 defaults |
| S19 | `PythonThemes` | As S18 | As S18 | - | As S18 |
| S20 | `JavaScriptThemes` | As S18 | As S18 | - | As S18 |
| S21 | `CSharpThemes` | As S18 | As S18 | - | As S18 |
| S22 | `SwiftThemes` | As S18 | As S18 | - | As S18 |
| S23 | `PseudocodeThemes` | As S18 | As S18 | - | As S18 |
| S24 | `EstimatedReferenceTokens` (int, recomputed) | Same; recomputed in the constructor from the reference files | Unchanged (the saved number is overwritten in memory) | - | Equals the sum of the files' lengths / 4 |
| S25 | `ReferenceSourceFilePaths` (file paths) | Same; paths whose file is missing are dropped in memory with a warning, as in 1.2.x | Unchanged | - | Existing paths equal the fixture's |
| S26 | `CustomTranslationOutputDirectory` (directory) | Same; used by the single, batch and MCP output paths | Unchanged | - | Value equals the fixture's; a translation lands there |

New 2.0 keys in the same section (absent from a 1.2.x ini, so the default applies):

| ID | 2.0 key, default | Handling | Check |
|---|---|---|---|
| N1 | `AnthropicMaxOutputTokens`, `OpenAIMaxOutputTokens`, `GeminiMaxOutputTokens`, `DeepSeekMaxOutputTokens`, `LMStudioMaxOutputTokens`, all 0 = Auto (PR #41). 1.2.x had no setting and sent a fixed budget | New | All read 0 |
| N2 | `SettingsVersion`, 0 when absent; `CurrentSettingsVersion` is 2 | New: 0 makes S6 and the LMStudioMaxTokens step run on load; the first edit writes 2 | Ini has no key after load; `SettingsVersion=2` after one edit and save |
| N3 | `McpServerPort`, 27000 | New | Reads 27000 |
| N4 | `bEnableDynamicToolDiscovery`, false | New | Reads false |
| N5 | `bEnablePythonScriptOnlyMode`, false (provisional 0019) | New | Reads false |
| N6 | `UIColors` (`FN2CUIColors`, the window's colors) | New | Reads the struct defaults |
| N7 | `CodeEditorTheme` (`FN2CCodeEditorThemes`, built-in themes) | New; replaces S18 to S23 | Contains "Unreal Engine" |

## Model enum values

Values present in 1.2.x (main or tag). No 1.2.x value of `EN2CLLMProvider`, `EN2CCodeLanguage`,
`EN2CLogSeverity` or the Blueprint/pin/node enums changed; only the model enums did. 2.0 keeps retired values
(hidden in the editor) so saved names still import; S2 to S5 say what happens on load.

| Enum | 1.2.x value | Model ID in 1.2.x | 2.0 |
|---|---|---|---|
| EN2CAnthropicModel | Claude3_7_Sonnet | claude-3-7-sonnet-20250219 | Retired 2026-02-19, migrated to Claude5_5_Sonnet |
| EN2CAnthropicModel | Claude3_5_Sonnet | claude-3-5-sonnet-20241022 | Retired 2025-10-28, migrated |
| EN2CAnthropicModel | Claude3_5_Haiku | claude-3-5-haiku-20241022 | Retired 2026-02-19, migrated |
| EN2CAnthropicModel | Claude4_Opus (main only) | claude-4-opus-20250514 | Retired 2026-06-15, migrated |
| EN2CAnthropicModel | Claude4_Sonnet (main only) | claude-4-sonnet-20250514 | Retired 2026-06-15, migrated |
| EN2COpenAIModel | GPT4o_2024_08_06 | gpt-4o-2024-08-06 | Kept, deprecated (shuts down 2027-01-14) |
| EN2COpenAIModel | GPT4o_Mini_2024_07_18 | gpt-4o-mini-2024-07-18 | Kept, deprecated (2027-01-14) |
| EN2COpenAIModel | GPT_o1 | o1 | Retired 2026-10-23, migrated to GPT_6_1_Sol |
| EN2COpenAIModel | GPT_o3_mini | o3-mini | Retired 2026-10-23, migrated |
| EN2COpenAIModel | GPT_o1_Preview | o1-preview-2024-09-12 | Retired 2025-07-28, migrated |
| EN2COpenAIModel | GPT_o1_Mini | o1-mini-2024-09-12 | Retired 2025-10-27, migrated |
| EN2COpenAIModel | GPT_o4_mini (main only) | o4-mini | Retired 2026-10-23, migrated |
| EN2COpenAIModel | GPT_4_1 (main only) | gpt-4.1 | Kept, deprecated (2027-01-14) |
| EN2COpenAIModel | GPT_o3 (main only) | o3 | Kept, deprecated (2026-12-11) |
| EN2CGeminiModel | Gemini_2_5_ProExp (tag only) | gemini-2.5-pro-exp-03-25 | **Removed** (absent on main and dev-2.0): Reset to Gemini_3_8_Flash, see S4 |
| EN2CGeminiModel | Gemini_2_5_Pro (main only) | gemini-2.5-pro-preview-05-06 | Retired 2025-12-02, migrated to Gemini_3_8_Flash |
| EN2CGeminiModel | Gemini_2_5_Flash (main only) | gemini-2.5-flash-preview-05-20 | Retired 2025-11-18, migrated |
| EN2CGeminiModel | Gemini_Flash_2_0 | gemini-2.0-flash | Retired 2026-06-01, migrated |
| EN2CGeminiModel | Gemini_Flash_Lite_2_0 | gemini-2.0-flash-lite-preview-02-05 | Retired 2025-12-09, migrated |
| EN2CGeminiModel | Gemini_1_5_Flash | gemini-1.5-flash | Retired (no date), migrated |
| EN2CGeminiModel | Gemini_1_5_Pro | gemini-1.5-pro | Retired (no date), migrated |
| EN2CGeminiModel | Gemini_2_0_ProExp_02_05 | gemini-2.0-pro-exp-02-05 | Retired (no date), migrated |
| EN2CGeminiModel | Gemini_2_0_FlashThinkingExp | gemini-2.0-flash-thinking-exp-01-21 | Retired (no date), migrated |
| EN2CDeepSeekModel | DeepSeek_R1 | deepseek-reasoner | Kept name, now deepseek-flash with thinking (S5) |
| EN2CDeepSeekModel | DeepSeek_V3 | deepseek-chat | Kept name, now deepseek-flash without thinking (S5) |

## API keys

| ID | 1.2.x | 2.0 | Handling | Owner | Check |
|---|---|---|---|---|---|
| K1 | `<Project>/Saved/NodeToCode/User/secrets.json`, plain JSON with `OpenAI_API_Key`, `Anthropic_API_Key`, `Gemini_API_Key`, `DeepSeek_API_Key`; per project | `FN2CSecretStore` keeps keys per user in `FPlatformProcess::UserSettingsDir()/NodeToCode/secrets.json` (Windows `%LOCALAPPDATA%\NodeToCode\secrets.json`), stamped `"Version": 1`, written via `.tmp` with a `.bak` copy | Auto, once, at module startup (provisional 0061): the project file is merged in (per-user values win, an empty per-user value is filled, a different project key is kept under `MigrationConflicts.<project file path>`), then the project file is deleted and its `User` folder removed if empty. Log: "Moved API keys from <old> to <new>". A project file that does not parse stays in place with an error and nothing moves | PR #42 done; OS keychain card 0161 (E04) | Old file gone; per-user file has the four keys and `Version` 1; Project Settings shows them; the log line names both paths and no key |
| K2 | Second project with its own keys | One per-user file for all projects | User: a project whose keys differ from the ones already moved keeps the first project's keys; its own are under `MigrationConflicts` and must be copied back by hand if wanted | 0061 (provisional) | Second fixture project: warning "already had a different key", conflict entry present |
| K3 | No environment variables | `ANTHROPIC_API_KEY`, `OPENAI_API_KEY`, `GEMINI_API_KEY`, `DEEPSEEK_API_KEY` (then `DEEPSEEK_API`) are used when no key is saved; a saved key wins; Project Settings shows an "API Key Source" line | New | PR #44 done; 0061 | With no saved key and the variable set, API Key Source names the variable |
| K4 | No sign-in tokens (1.2.x had no OAuth) | Earlier 2.0 builds saved `OAuth` and `Claude_OAuth_*` / `Google_OAuth_*` fields; 2.0 deletes them on load and keeps the keys | Auto (provisional 0038); affects pre-release users only | PR #35 done | A file with those fields loses them; warning links the wiki |

## Files under the project and the editor's config

| ID | 1.2.x writes | 2.0 | Handling | Owner | Check |
|---|---|---|---|---|---|
| F1 | `Saved/NodeToCode/Translations/<Blueprint>_<YYYY-MM-DD-HH.MM.SS>/` (or the custom directory): `N2C_BP_<folder>.json`, `N2C_BP_Minified_<folder>.json`, `N2C_Translation_<folder>.json`, and per graph `<Graph>/<Graph>.h`, `<Graph>.<ext>`, `<Graph>_Notes.txt` | Same root and layout for new translations; folder and graph names pass through `GetTranslationFileStem` (differs only for names with path characters or over 128 characters) | Unchanged on disk; never moved, rewritten or deleted. 2.0 does not scan the folder: its window lists translations recorded in `BlueprintGraphState.json` (F3), so 1.2.x translations are not listed there. User: open them from disk | - | Fixture folder byte-identical after a 2.0 session; not listed in the 2.0 window |
| F2 | N2C JSON `"version": "1.0.0"` in the `N2C_BP_*` files | Still `FN2CVersion` "1.0.0" | Unchanged; consumers cannot tell 1.2.x JSON from 2.0 JSON | E10 (N2C JSON v2 bumps the version) | `version` field in a new translation reads 1.0.0 |
| F3 | Nothing (no tags or graph state in 1.2.x) | `Saved/NodeToCode/BlueprintGraphState.json` (tags and translation records), with `.tmp`, `.bak` and `.corrupt-<UTC stamp>` beside it | New | PR #48 done | File appears after the first tag or translation |
| F4 | Nothing | Reads `Saved/NodeToCode/Tags/BlueprintTags.json` (written by 2.0 development builds from 2025-06-21) once when there is no state file and no `.bak`/`.corrupt-*`, then renames it `.migrated` | New for 1.2.x users (nothing to migrate); Auto for pre-release users | PR #48 done | 0079's legacy-tags fixture tests the pre-release path, not a 1.2.3 one |
| F5 | Batch output: none | `Translations/BatchTranslation_<stamp>/` with `BatchSummary.json`, and `Translations/BatchJson_<stamp>/` | New | - | - |
| F6 | `Saved/NodeToCode.log`: code path exists, but nothing calls `EnableFileLogging`, so it is never written | Same | Unchanged (not written by either) | - | No such file after a session |
| F7 | `Config/DefaultEngine.ini`: `ConfigureHttpTimeouts` writes `HttpConnectionTimeout=300` and `HttpActivityTimeout=3600` (section `HTTP`, through a PerObjectConfig helper) when lower | Writes nothing; timeouts are set per LLM request | User: the 1.2.x values stay in the project and still raise the engine-wide HTTP timeouts; remove them if unwanted | - | DefaultEngine.ini unchanged by 2.0 |
| F8 | `EditorPerProjectUserSettings.ini`: sets "Use Less CPU when in Background" (`bThrottleCPUWhenNotForeground`) to false on every start | Leaves the preference alone and disables throttling only while a translation runs | User: the saved false stays; turn the option back on in Editor Preferences if wanted | - | Value unchanged by 2.0 |
| F9 | Nothing | `[/Script/NodeToCode.N2CUserSettings]` in `Saved/Config/<Platform>Editor/EditorPerProjectUserSettings.ini`: `bEnableMcpServer` true, `bAllowMcpPythonExecution` false | New (provisional 0019 for the Python default). The MCP server starts on port 27000 and a second server on 27001 at editor start (not in commandlets or unattended runs without `-N2CMcp`), accepting loopback Host and Origin only | PR #26 done | Log "MCP endpoint available at: http://localhost:27000/mcp" and "SSE server started on port 27001" |
| F10 | Nothing | `<Project>/Content/Python/scripts/script_registry.json` and `scripts/<category>/*.py`, written by the script library tools (`.tmp`, `.bak`, `.corrupt-<stamp>`) | New for 1.2.x users | PR #49 done | - |

## Editor UI and Blueprint-facing API

| ID | 1.2.x | 2.0 | Handling | Owner | Check |
|---|---|---|---|---|---|
| U1 | Editor Utility Widget UI under `/NodeToCode/UI/` (NodeToCodeUI, the N2C_EUW_* components, N2C_Theme_* data assets, N2C_T_UI_* icons, MPC/M_ComboBoxText) | Removed; the 2.0 window is Slate, opened from the Blueprint editor toolbar | User: project assets that reference `/NodeToCode/UI/...` lose those references. An open-tab entry in `[/Script/Blutility.EditorUtilitySubsystem] LoadedUIs` gives one "Could not load" warning on the first start, then the engine prunes it | PR #61 done | First start logs one `/NodeToCode/UI` warning, the second none |
| U2 | `UN2CMainWindowWidget`, `UN2CTagManagerWidget`, `UN2CCodeEditorWidget` UMG classes | Kept for 2.0-beta, unsupported, planned for removal; the first two show a placeholder in the UMG designer | Unchanged for now; User: move off them before removal | PR #61 | User EUW embedding them still loads |
| U3 | `UN2CCodeEditorWidget` BlueprintCallable `SetTheme`, `GetAvailableThemes`, `GetThemeFromName`, `GetCurrentTheme` | Removed (commit a4f66d6) | User: Blueprints calling them fail to compile until the nodes are removed | - | Compile a Blueprint with those nodes: errors name the functions |
| U4 | `UN2CCodeEditorWidget::GetSelection` outputs `OutStartIndex`, `OutEndIndex` | Renamed `OutStartLine`, `OutEndLine` (they were line indices already) | User: reconnect the output pins | Card 0063 | Old pins show as orphaned |
| U5 | Toolbar commands `NodeToCode_OpenWindow`, `NodeToCode_CollectNodes`, `NodeToCode_CopyJson` | Same names | Unchanged; custom key bindings keep working | - | Binding in EditorKeyBindings.ini still fires |

## C++ API

| ID | 1.2.x | 2.0 | Handling | Owner |
|---|---|---|---|---|
| A1 | `IN2CLLMService::SendRequest`, `UN2CHttpHandlerBase::PostLLMRequest`, `UN2CLLMModule::ProcessN2CJson` return `void` (`ProcessN2CJsonWithOverrides` is new in 2.0) | Return `FN2CLLMRequestHandleRef`, a cancellable handle | User: third-party providers that override `SendRequest` must change the signature; callers that ignore the result compile unchanged | PR #51 done |
| A2 | `UN2CUserSecrets` (UObject holding the four keys, `LoadSecrets`/`SaveSecrets`) and `UN2CSettings::UserSecrets` | Removed; keys go through `FN2CSecretStore::Get()` (`GetApiKey`, `GetStoredApiKey`, `GetApiKeySource`) | User: C++ that read keys from `UN2CUserSecrets` must switch | PR #42 done |

## Changes since earlier 2.0 development builds (not 1.2.x)

These affect people who ran a dev-2.0 build before the beta. 0079 does not need them for a 1.2.3 project.

| ID | Earlier 2.0 build | 2.0-beta | Handling | Owner |
|---|---|---|---|---|
| D1 | `AnthropicAuthMethod` / `GeminiAuthMethod` = OAuth in the settings section | Keys removed; ignored on load with one warning per session; the provider uses its API key | Auto; User: set an API key (provisional 0038) | PR #35 |
| D2 | `LMStudioMaxTokens` (PR #20 build, default 32768) | A non-default value moves to `LMStudioMaxOutputTokens`; 32768 becomes Auto | Auto (settings version 2) | PR #41 |
| D3 | run-python, save-python-script and delete-python-script always exposed over MCP; script-only mode defaulted on (commit 6db827b) | Off unless the per-user **Allow MCP Python Execution** is on; `bEnablePythonScriptOnlyMode` defaults to false (a saved true stays) | User: opt in per user (provisional 0019) | PR #26 |
| D4 | `translate-focused-blueprint` answered `{status: "accepted", taskId, progressToken, sseUrl}` and sent the result on `/mcp/events` on the port+1 server | Answered on the same POST: UE 5.8 streams `notifications/progress` then the result as `text/event-stream` when the client accepts it and sets a progress token; 5.6/5.7 hold the POST and return one JSON result (provisional 0085). No `sseUrl`. The port+1 server still starts and `initialize` still returns `_meta.notificationUrl` (`http://localhost:<port+1>/mcp/notifications`) | User: MCP clients that waited on `sseUrl` must read the POST response | PR #59 |
| D5 | Python `datatable_populator.populate_from_json` / `populate_from_csv` emptied the table and always saved | Merge rows by name by default (`clear_existing=True` replaces); save only with `save=True`; failures restore the rows and return an error | User: scripts that relied on replace-and-save pass `clear_existing=True, save=True` | PR #47 |
| D6 | `script_registry.json` rewritten in place; a parse failure gave an empty registry that the next save wrote over | Atomic writes; an unparseable registry is copied to `.corrupt-<stamp>`, left in place, and the tools return an error | Auto | PR #49 |
| D7 | `BlueprintGraphState.json` written in place; legacy tags re-migrated after a parse failure | Atomic writes, `.corrupt-<stamp>` quarantine, legacy file renamed `.migrated` | Auto | PR #48 |

## Not verified

- Which commit the Fab 1.2.3 package was built from (see Baselines).
- Whether 1.2.x saved every Config property on each Project Settings save, or only edited ones (S rows, S11 to S14).
- How a pricing map entry with an unknown enum key imports (S13).
- That 2.0 drops the stale 1.2.x keys (S18 to S23) from the ini on the next save; PR #35 says so for the sign-in keys,
  but it was not re-run here.
- The secrets location on macOS and Linux (`UserSettingsDir()` there was not checked).
- None of the rows were run in an editor for this card; card 0079 runs them on 5.8 with a 1.2.3 fixture.

## Other release notes found while writing this

- dev-2.0's `NodeToCode.uplugin` says `"VersionName": "1.3"`; the release card should set the 2.0.0-beta version.
