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
  transport), 0073 (2.0-beta is Win64 only; `.agents/decisions/0001-platforms-for-2-0-beta.md`).
- 1.2.x had no MCP server, Python library, tag manager or batch translation. Changes to those since earlier 2.0
  development builds are in the last section; they do not affect a 1.2.x user.
- **Engine:** 2.0 builds for UE 5.6 to 5.8 only, so a 1.2.x project on 5.5 or older must upgrade the engine first.
- **Platform (Provisional, 0073):** 1.2.x allowlisted Win64 and Mac; 2.0.0-beta.1 allowlists Win64 only, because Mac
  has never been compiled. A Mac user stays on 1.2.x until a verified Mac build re-adds it (card 0074).
- **Version:** 1.2.x reports `Version` 2, `VersionName` "1.2.2"; 2.0 reports `Version` 3, `VersionName`
  "2.0.0-beta.1" (also the MCP `serverInfo.version`).

## Settings: `[/Script/NodeToCode.N2CSettings]`

Both versions declare `UN2CSettings` as `Config = NodeToCode, DefaultConfig`, so the values live in the project's
`Config/DefaultNodeToCode.ini` (a local `Saved/Config/<Platform>Editor/NodeToCode.ini` layers on top in both). The
module, class and section names are unchanged. Values are stored by name; enum values by enumerator name.

General rules for every row:

- A key absent from the ini takes the 2.0 default, which differs from 1.2.x for the model fields. A 1.2.x
  Project Settings edit of a single value wrote only that key (`UpdateSinglePropertyInConfigFile`, from both the
  engine's settings editor and 1.2.x's own `PostEditChangeProperty`); an edit of any map or array (a pricing or
  theme map) saved the whole section, every pricing and theme map included (`SSettingsEditor::
  HandleSettingsPropertyChanged` then `TryUpdateDefaultConfigFile`, read on 5.8 with `ini.UseNewPropertySaving` 0,
  the default). The 0079 fixture should still set every key explicitly.
- An enum name 2.0 does not know fails to import (run on 5.8): the engine logs `FEnumProperty: ... invalid value of
  '<name>'` and `LogObj: Error: LoadConfig (...): import failed for <key>`, and the field keeps its 2.0 default. A map
  keyed by such a name fails as a whole and loads empty. No CoreRedirects `EnumRedirects` exist (the plugin has no
  Config ini besides `FilterPlugin.ini`). Since settings version 4 (card 0129) no setting holds a model enum, so the
  model keys can no longer cause these errors.
- **Model keys (settings version 4, card 0129).** 2.0 stores model selections as catalog model IDs
  (`AnthropicModelId`, `OpenAIModelId`, `GeminiModelId`, `DeepSeekModelId` plus `DeepSeekReasoningEffort`, each
  provider with an optional `<Provider>CustomModelId`) and price overrides in one `ModelPricing` map keyed by model ID.
  `MigrateModelSelectionsToIds` runs from `PostInitProperties` and `PostReloadConfig` while `SettingsVersion` < 4 and
  converts the 1.2.x keys in memory: a name whose catalog row is live becomes that row's ID; a retired row or a name
  2.0's enum lacks becomes the provider default, with the warning "Saved model selection no longer served by its
  provider, using the current default instead: ..." (retired) or the unknown-value warning below (removed); a name
  the enum has but the catalog does not list keeps its model ID as the Custom Model ID with a warning (cannot happen
  with the shipped catalog, where every enumerator has a row). Prices are re-keyed by model ID after the 1.2.x
  cleanup (S11). An Info line lists the conversions ("Model settings saved by model enum name now use model IDs:
  ..."). If the catalog file did not load, the conversion stays in memory and nothing is written.
- `MigrateRetiredModelSelections` (PR #32) runs from `PostInitProperties` and `PostReloadConfig`: a selected model ID
  the catalog marks retired becomes the provider default, with the same warning. An ID the catalog does not list is
  left alone.
- `MigrateSavedSettings` runs the layout migrations for `SettingsVersion` < 2 on every load. One Info line lists
  the changes ("Settings saved before NodeToCode 2.0 updated so the server and model defaults apply: ...").
- `MigrateLegacy12Settings` (card 0418) runs once per editor start from `FNodeToCodeModule::StartupModule` (not in
  commandlets) and writes to `Config/DefaultNodeToCode.ini` with the engine's per-key update, leaving other lines
  alone. On every start it writes back any enum value that did not load as saved and any model ID moved off a
  retired model, and any value the engine could not import, so those warnings and engine errors appear on the first
  start only; names 2.0 lacks get a warning ("Saved settings hold values NodeToCode 2.0 does not have, so these load
  as the 2.0 value instead: ..."). While `SettingsVersion` < 4 it writes the model ID conversion above and removes the
  eight enum keys (`AnthropicModel`, `OpenAI_Model`, `Gemini_Model`, `DeepSeekModel`, `OpenAIModelPricing`,
  `AnthropicModelPricing`, `GeminiModelPricing`, `DeepSeekModelPricing`). While `SettingsVersion` < 3 it also does
  the one-time steps in S6, S11 to S23, F7 and F8. Then it writes `SettingsVersion=4`. A project with nothing to migrate is not written. One
  Info line lists what it saved, dropped and removed ("Updated settings left by an earlier NodeToCode version:
  ..."). The second start logs none of these lines.

How 0079 checks a row: read the value from `unreal.get_default_object(unreal.N2CSettings)` in a headless 5.8
editor started on the fixture, and grep the editor log for the `LogNodeToCode` lines quoted below. Config-only
properties with no editor flags (`SettingsVersion`) are not visible to Python; read those from the ini.

| ID | 1.2.x key, type, default | 2.0 | Handling | Owner | Check |
|---|---|---|---|---|---|
| S1 | `Provider` (`EN2CLLMProvider`), Anthropic. Values OpenAI, Anthropic, Gemini, Ollama, DeepSeek, LMStudio (LMStudio not on the tag) | Same key and values | Unchanged | - | Value equals the fixture's |
| S2 | `AnthropicModel` (`EN2CAnthropicModel`), main Claude4_Sonnet, tag Claude3_7_Sonnet | `AnthropicModelId` (string model ID); every 1.2.x value is retired (see the enum table) | Auto: becomes `claude-sonnet-5-5` with the retired-model warning, written as `AnthropicModelId` on the first start and `AnthropicModel` removed, so the second start is silent | PR #32, cards 0418 and 0129 done | `anthropic_model_id` is `claude-sonnet-5-5`; first start's warning names `AnthropicModel=<old>`; ini reads `AnthropicModelId=claude-sonnet-5-5` and has no `AnthropicModel`; second start has no warning |
| S3 | `OpenAI_Model` (`EN2COpenAIModel`), main GPT_o4_mini, tag GPT_o3_mini | `OpenAIModelId`. Kept: GPT4o_2024_08_06, GPT4o_Mini_2024_07_18, GPT_4_1, GPT_o3 (now `gpt-4o-2024-08-06`, `gpt-4o-mini-2024-07-18`, `gpt-4.1`, `o3`). Retired: GPT_o1, GPT_o3_mini, GPT_o1_Preview, GPT_o1_Mini, GPT_o4_mini | Auto: kept values become their model ID, silently (each now deprecated, see the enum table); retired ones become `gpt-6.1-sol` with the warning. Written once as `OpenAIModelId`, `OpenAI_Model` removed (as S2) | PR #32, cards 0418 and 0129 | Kept value reads its model ID with no warning; retired value reads `gpt-6.1-sol` plus warning on the first start only |
| S4 | `Gemini_Model` (`EN2CGeminiModel`), main Gemini_2_5_Flash, tag Gemini_2_5_ProExp | `GeminiModelId`. Gemini_2_5_ProExp no longer exists; every other 1.2.x value is retired | Tag's Gemini_2_5_ProExp: **Reset** to `gemini-3.8-flash`; on the first start a NodeToCode warning names `Gemini_Model=Gemini_2_5_ProExp -> gemini-3.8-flash` (no engine error: no setting holds the enum any more) and the new value is written. Others: Auto to `gemini-3.8-flash` with the retired-model warning. Written once as `GeminiModelId`, `Gemini_Model` removed | Cards 0418 and 0129 done | `gemini_model_id` is `gemini-3.8-flash`; ini reads `GeminiModelId=gemini-3.8-flash` and has no `Gemini_Model`; no engine import error; second start has no NodeToCode message |
| S5 | `DeepSeekModel` (`EN2CDeepSeekModel`), DeepSeek_R1 (`deepseek-reasoner`); DeepSeek_V3 (`deepseek-chat`) | `DeepSeekModelId` plus `DeepSeekReasoningEffort`: both become `deepseek-flash`, R1 with thinking `high`, V3 with `none`, since `deepseek-reasoner` and `deepseek-chat` were discontinued 2026-07-24 | Auto, silent (not retired, so no warning); written once, `DeepSeekModel` removed | PR #33, card 0129 done | `deep_seek_model_id` is `deepseek-flash` and `deep_seek_reasoning_effort` the fixture's mode (V3: `none`); a request's payload model is `deepseek-flash` |
| S6 | `OllamaConfig` (struct): OllamaEndpoint, bUseSystemPrompts (false), PrependedModelCommand (not on the tag), KeepAlive, Temperature, NumPredict (8192), TopP, TopK, MinP, RepeatPenalty, NumCtx (8192), Mirostat, MirostatEta, MirostatTau, Seed | Same members plus Think, ThinkLevel and one `bOverride_*` per sampler; options are sent only when set | Auto (settings version 1, PR #34): NumCtx 8192 and NumPredict 8192 become 0 (server default); bUseSystemPrompts false becomes true (a deliberate false is lost too); a sampler that differs from the old default gets its override turned on, one at the old default stays off. Endpoint, command and KeepAlive unchanged. The migrated struct is written once with `SettingsVersion=3` (card 0418) | PR #34, card 0418 done | Fixture with NumCtx 8192, Temperature 0.7, bUseSystemPrompts False reads NumCtx 0, bOverride_Temperature True, Temperature 0.7, bUseSystemPrompts True; Info line lists them |
| S7 | `OllamaModel` (string), main `qwen3:32b`, tag `qwen2.5-coder:32b` | Same | Unchanged (an absent key now gives `qwen3:32b`) | - | Value equals the fixture's |
| S8 | `LMStudioModel` (string, main only) | Same | Unchanged | - | Value equals the fixture's |
| S9 | `LMStudioEndpoint` (string, main only) | Same | Unchanged | - | Value equals the fixture's |
| S10 | `LMStudioPrependedModelCommand` (string, main only) | Same | Unchanged | - | Value equals the fixture's |
| S11 | `OpenAIModelPricing` (map enum to input/output price), filled with 1.2.x prices by the constructor | `ModelPricing` (one map, model ID to input/output price, for every provider); prices come from the model catalog and an entry overrides them | Auto, once (cards 0418 and 0129): entries for retired or unknown models and entries still at 1.2.x's built-in price (for example o3 at 15/60) are dropped, the rest are written to `ModelPricing` under their model ID and the old key is removed, so catalog prices apply; any other price is the user's and stays as an override (a user who typed exactly the 1.2.x price loses it; the two cannot be told apart) | Cards 0418 and 0129 done | Fixture with GPT_4_1 at 1.5/6 and the 1.2.x prices: `ModelPricing` holds only `gpt-4.1` of the OpenAI prices; `GetCurrentInputCost` for o3 is the catalog price; Info line lists each dropped entry; no `OpenAIModelPricing` key left |
| S12 | `AnthropicModelPricing` | As S11 | As S11; every 1.2.x entry is for a retired model, so none reaches `ModelPricing` | Cards 0418 and 0129 | No `claude-*` entry in `ModelPricing` |
| S13 | `GeminiModelPricing` | As S11 | As S11. A tag entry keyed Gemini_2_5_ProExp makes the whole map unreadable; the upgrade reads it with the engine's import, names it in the unknown-value warning and removes the key (no engine error any more). Every 1.2.x Gemini model is retired or removed, so nothing a user could keep is lost | Cards 0418 and 0129 | No `gemini-*` entry in `ModelPricing`; no `GeminiModelPricing` key left; no engine `import failed` on either start |
| S14 | `DeepSeekModelPricing` | As S11 | As S11 (R1 0.55/2.19 and V3 0.14/0.28 were the 1.2.x prices). R1 and V3 both become `deepseek-flash`; if both kept a user price, the first (R1) wins and the other is listed as dropped | Cards 0418 and 0129 | Fixture with V3 at 0.2/0.5: `ModelPricing` `deepseek-flash` is 0.2/0.5 |
| S15 | `TargetLanguage` (`EN2CCodeLanguage`), Cpp | Same key and values | Unchanged | - | Value equals the fixture's |
| S16 | `TranslationDepth` (int, 0) | Same | Unchanged | - | Value equals the fixture's |
| S17 | `MinSeverity` (`EN2CLogSeverity`), Info | Same | Unchanged | - | Value equals the fixture's |
| S18 | `CPPThemes` (`FN2CCodeEditorThemes`: map of theme name to 11 colors) | Not read. 2.0 has one `CodeEditorTheme` and draws the code editor with its "Unreal Engine" entry | Reset, not converted: 2.0 draws every language with one theme, and 1.2.x kept the selected theme name only in its UI asset (`SN2CCodeEditor::CurrentTheme`, default "Midnight Code"; removed with U1), never in the config, so there is no way to know which of the 21 themes per language was in use; a whole-section save also wrote every map at its defaults, so a key does not prove customisation. Card 0418 logs one warning on the first start naming the keys and pointing to Theming > Code Editor Theme, and leaves the keys in the ini so colours can be copied. User: re-create colours there | Card 0418 done | Key still in the ini; first start's warning names it; `CodeEditorTheme` holds the 2.0 defaults |
| S19 | `PythonThemes` | As S18 | As S18 | - | As S18 |
| S20 | `JavaScriptThemes` | As S18 | As S18 | - | As S18 |
| S21 | `CSharpThemes` | As S18 | As S18 | - | As S18 |
| S22 | `SwiftThemes` | As S18 | As S18 | - | As S18 |
| S23 | `PseudocodeThemes` | As S18 | As S18 | - | As S18 |
| S24 | `EstimatedReferenceTokens` (int, recomputed) | Same; recomputed from the files once the config has loaded (card 0186; 1.2.x computed it in the constructor, before LoadConfig, so the saved number loaded) | Recomputed at the first start; the saved number is ignored | Card 0186 done | Equals the sum of the files' lengths / 4 |
| S25 | `ReferenceSourceFilePaths` (file paths) | Same; the missing-file check runs once the config has loaded (card 0186; in 1.2.x it ran in the constructor and saw an empty list) | Existing paths load; a missing one is left out with one "Reference source file not found" warning per start, and stays in the ini so a teammate's checkout keeps it | Card 0186 done | Existing paths equal the fixture's; the missing path is dropped with the warning |
| S26 | `CustomTranslationOutputDirectory` (directory) | Same; used by the single (toolbar) and batch output paths. MCP `translate-focused-blueprint` returns the code and saves no folder | Unchanged | - | Value equals the fixture's; a translation lands there |

New 2.0 keys in the same section (absent from a 1.2.x ini, so the default applies):

| ID | 2.0 key, default | Handling | Check |
|---|---|---|---|
| N1 | `AnthropicMaxOutputTokens`, `OpenAIMaxOutputTokens`, `GeminiMaxOutputTokens`, `DeepSeekMaxOutputTokens`, `LMStudioMaxOutputTokens`, all 0 = Auto (PR #41). 1.2.x had no setting and sent a fixed budget | New | All read 0 |
| N2 | `SettingsVersion`, 0 when absent; `CurrentSettingsVersion` is 4 | New: 0 makes S6, the LMStudioMaxTokens step, the 1.2.x clean-up (card 0418) and the model ID conversion (card 0129) run on load. The clean-up writes 4 when it found anything 1.2.x left or converted a model key; any Project Settings edit also writes it next to the edited key (3 while the model catalog file is missing, so the conversion still runs once it loads) | `SettingsVersion=4` and none of the eight model enum keys after the first start of a 1.2.x fixture; a 2.0 project with nothing to migrate gets no key until an edit |
| N3 | `McpServerPort`, 27000 | New | Reads 27000 |
| N4 | `McpToolExposure`, `ListAllTools` (card 0165; replaced `bEnableDynamicToolDiscovery`) | New | Reads ListAllTools |
| N5 | `McpToolProfile`, `DefaultToolsets` (card 0165; replaced `bEnablePythonScriptOnlyMode`, provisional 0019) | New | Reads DefaultToolsets |
| N6 | `UIColors` (`FN2CUIColors`, the window's colors) | New | Reads the struct defaults |
| N7 | `CodeEditorTheme` (`FN2CCodeEditorThemes`, built-in themes) | New; replaces S18 to S23 | Contains "Unreal Engine" |

## Model enum values

Values present in 1.2.x (main or tag). No 1.2.x value of `EN2CLLMProvider`, `EN2CCodeLanguage`,
`EN2CLogSeverity` or the Blueprint/pin/node enums changed; only the model enums did. 2.0 keeps retired values
(hidden in the editor) so saved names still map to a model ID; S2 to S5 say what happens on load. The "2.0" column
names enumerators; since card 0129 the setting holds the model ID of the row the enumerator names (the default's
for "migrated").

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
| K1 | `<Project>/Saved/NodeToCode/User/secrets.json`, plain JSON with `OpenAI_API_Key`, `Anthropic_API_Key`, `Gemini_API_Key`, `DeepSeek_API_Key`; per project | `FN2CSecretStore` keeps keys per user in `FPlatformProcess::UserSettingsDir()/NodeToCode/secrets.json` (Windows `%LOCALAPPDATA%\NodeToCode\secrets.json`), stamped `"Version": 1`, written via `.tmp` with a `.bak` copy | Auto, once, at module startup (provisional 0061): the project file is merged in (per-user values win, an empty per-user value is filled, a different project key is kept under `MigrationConflicts.<project file path>`), then the project file is deleted and its `User` folder removed if empty. Log: "Moved API keys from <old> to <new>". A project file that does not parse stays in place with an error and nothing moves | PR #42 done; card 0161 (E04) then moves the per-user file into Windows Credential Manager (`NodeToCode/<Provider>`), reads every key back and deletes the file; log "Moved API keys (...) from <file> to Windows Credential Manager (NodeToCode/*) and deleted the file" | Old files gone; `cmdkey /list:NodeToCode*` lists the four entries; API Key Status says "Stored in Windows Credential Manager"; no key in any log |
| K2 | Second project with its own keys | One per-user file for all projects | User: a project whose keys differ from the ones already moved keeps the first project's keys; its own are kept as Credential Manager entries `NodeToCode/<Provider>.Conflict.<n>` (card 0161) and must be copied back by hand if wanted | 0061 (provisional) | Second fixture project: warning "keys that differed", conflict entries present |
| K3 | No environment variables | `ANTHROPIC_API_KEY`, `OPENAI_API_KEY`, `GEMINI_API_KEY`, `DEEPSEEK_API_KEY` (then `DEEPSEEK_API`) are used when no key is saved; a saved key wins; Project Settings shows an "API Key Status" line | New | PR #44 done; 0061 | With no saved key and the variable set, API Key Status names the variable |
| K4 | No sign-in tokens (1.2.x had no OAuth) | Earlier 2.0 builds saved `OAuth` and `Claude_OAuth_*` / `Google_OAuth_*` fields; 2.0 deletes them on load and keeps the keys | Auto (provisional 0038); affects pre-release users only | PR #35 done | A file with those fields loses them; warning links the wiki |

## Files under the project and the editor's config

| ID | 1.2.x writes | 2.0 | Handling | Owner | Check |
|---|---|---|---|---|---|
| F1 | `Saved/NodeToCode/Translations/<Blueprint>_<YYYY-MM-DD-HH.MM.SS>/` (or the custom directory): `N2C_BP_<folder>.json`, `N2C_BP_Minified_<folder>.json`, `N2C_Translation_<folder>.json`, and per graph `<Graph>/<Graph>.h`, `<Graph>.<ext>`, `<Graph>_Notes.txt` | Same root; new translations use the translation archive's run folders (`<Blueprint>_<yyyyMMdd-HHmmss-fff>_<run id>/` with `manifest.json`, one folder per response entry; card 0252), and folder and graph names pass through `GetTranslationFileStem` (differs only for names with path characters or over 128 characters) | Unchanged on disk; never moved, rewritten or deleted. 2.0 does not scan the folder: its window lists translations recorded in `BlueprintGraphState.json` (F3), so 1.2.x translations are not listed there. User: open them from disk | - | Fixture folder byte-identical after a 2.0 session; not listed in the 2.0 window |
| F2 | N2C JSON `"version": "1.0.0"` in the `N2C_BP_*` files | `FN2CVersion` "2.0.0" (decision 0003, provisional 0117): a `flows.data` value is an array when one output feeds several inputs, exec flows name their pins (0136), and names are stable identifiers with a node `class`, node `title` instead of `name` and pin `name` = `PinName` (0138) | Auto: 1.0.0 files stay as they are and still read: `FN2CSerializer::FromJson`, `UN2CPythonBridge::ReadN2CJson` (Python `nodetocode.read_n2c_json`) and the validator read 1.x and 2.x into the 2.0.0 model, through the 2.x line | Card 0135 done | The 1.2.3 fixture's `N2C_BP_*.json` files read with `ReadN2CJson` (source 1.0.0, model 2.0.0, valid); `--live`: a new translation's `version` reads 2.0.0 |
| F3 | Nothing (no tags or graph state in 1.2.x) | `Saved/NodeToCode/BlueprintGraphState.json` (tags and translation records), with `.tmp`, `.bak` and `.corrupt-<UTC stamp>` beside it | New | PR #48 done | File appears after the first tag or translation |
| F4 | Nothing | Reads `Saved/NodeToCode/Tags/BlueprintTags.json` (written by 2.0 development builds from 2025-06-21) once when there is no state file and no `.bak`/`.corrupt-*`, then renames it `.migrated` | New for 1.2.x users (nothing to migrate); Auto for pre-release users | PR #48 done | 0079's legacy-tags fixture tests the pre-release path, not a 1.2.3 one |
| F5 | Batch output: none | `Translations/BatchTranslation_<stamp>_<run id>/` with `manifest.json` and `BatchSummary.json`, and `Translations/BatchJson_<stamp>_<id>/` | New | - | - |
| F6 | `Saved/NodeToCode.log`: code path exists, but nothing calls `EnableFileLogging`, so it is never written | Same | Unchanged (not written by either) | - | No such file after a session |
| F7 | `Config/DefaultEngine.ini`: `ConfigureHttpTimeouts` writes `HttpConnectionTimeout=300` and `HttpActivityTimeout=3600` (section `HTTP`, through a PerObjectConfig helper) when lower | Writes nothing itself; timeouts are set per LLM request | Auto, once (card 0418): a key in `[HTTP]` holding exactly 300 / 3600 (the 1.2.x values) is removed; a key with any other value is the user's and stays; other keys and sections are untouched (an empty `[HTTP]` header can remain). The engine defaults apply from the next start. A read-only file gets a warning instead | Card 0418 done | After the first start the two keys are gone, `HttpTimeout` and other sections unchanged; a fixture with `HttpConnectionTimeout=120` keeps it |
| F8 | Engine-wide `EditorSettings.ini` (`UEditorPerformanceSettings`, `config=EditorSettings`): sets "Use Less CPU when in Background" (`bThrottleCPUWhenNotForeground`) to false on every start | Leaves the preference alone and disables throttling only while a translation runs | User, told once: a saved false is the same whether 1.2.x or the user set it, so 2.0 leaves it. When the project shows any 1.2.x trace (a key in S18 to S23, an F7 key, a dropped S11 to S14 price or a 1.2.x model name) and the option is off, card 0418 logs one warning on the first start saying so and where to turn it back on | Card 0418 done | Value unchanged; first start's warning mentions "Use Less CPU when in Background", second start does not |
| F9 | Nothing | `[/Script/NodeToCode.N2CUserSettings]` in `Saved/Config/<Platform>Editor/EditorPerProjectUserSettings.ini`: `bEnableMcpServer` true, `bAllowMcpPythonExecution` false; only the user's Saved/Config value (or a `-ini:` override) counts, a project `Config/Default*.ini` value is ignored with a warning (card 0423) | New (provisional 0019 for the Python default). The MCP server starts on port 27000 at editor start (not in commandlets or unattended runs without `-N2CMcp`), accepting loopback Host and Origin only; the second server on 27001 was removed in card 0113 | PR #26 done | Log "MCP endpoint available at: http://localhost:27000/mcp" |
| F10 | Nothing | `<Project>/Content/Python/scripts/script_registry.json` and `scripts/<category>/*.py`, written by the script library tools (`.tmp`, `.bak`, `.corrupt-<stamp>`) | New for 1.2.x users | PR #49 done | - |

## Editor UI and Blueprint-facing API

| ID | 1.2.x | 2.0 | Handling | Owner | Check |
|---|---|---|---|---|---|
| U1 | Editor Utility Widget UI under `/NodeToCode/UI/` (NodeToCodeUI, the N2C_EUW_* components, N2C_Theme_* data assets, N2C_T_UI_* icons, MPC/M_ComboBoxText) | Removed; the 2.0 window is Slate, opened from the Blueprint editor toolbar | User: project assets that reference `/NodeToCode/UI/...` lose those references. An open-tab entry in `[/Script/Blutility.EditorUtilitySubsystem] LoadedUIs` gives one "Could not load" warning on the first start, then the engine prunes it. Seen in 0079: a windowed v1.2.3 start (UE 5.6) wrote no `LoadedUIs` entry; 1.2.3 embeds the widget in its own tab spawner, so such an entry comes only from a user who ran the widget by hand | PR #61 done | First start logs one `/NodeToCode/UI` warning, the second none |
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
| D8 | A second server (cpp-httplib) on `McpServerPort` + 1 with `/mcp/events` and `/mcp/notifications`; `initialize` returned `_meta.notificationUrl` and `tools.listChanged: true`, and `notifications/tools/list_changed` went only to that server. C++: `FN2CMcpSSEResponseManager`, `FN2CMcpProgressTracker`, `IN2CMcpNotificationChannel`, `FJsonRpcUtils::ParseRequest/ParseResponse/ParseNotification`, `FN2CMcpToolManager::GetToolDefinition` (always null), `FN2CMcpHttpServerManager::BroadcastNotification` and the unused list and generic validators of `FN2CMcpRequestValidator` | One server on the MCP port; no `_meta` in `initialize`; `tools.listChanged: false` and no list-changed notification (Dynamic Tool Discovery logs a warning: clients see the tools `assess-needed-tools` enables when they list tools again). The C++ symbols are removed. Server Port may now be 65535 | Auto; User: a client that followed `notificationUrl` stops; C++ that used the removed symbols must drop them | Card 0113 |
| D9 | `BlueprintGraphState.json` translation records: the batch stored the language as `EN2CCodeLanguage::<X>`, the main window stored `CPP` for every language and an absolute or project-relative path, and the viewer expected Pseudocode as `.txt`, so only C++ records opened in the translation viewer | One language id (`Cpp`, `Python`, `JavaScript`, `CSharp`, `Swift`, `Pseudocode`) and extension map for every writer and the loader; the path is project-relative inside the project and absolute outside it. The loader still reads the old ids, `.txt` Pseudocode and a file named after the LLM's graph name | Auto: existing records open as they are; nothing is rewritten | Card 0438 |
| D10 | `bEnableDynamicToolDiscovery` (only `assess-needed-tools` listed, which rewrote one global tool list) and `bEnablePythonScriptOnlyMode` (the seven Python tools) | Tool Profile (`McpToolProfile`), Custom Toolsets and Tool Exposure (`McpToolExposure`); `assess-needed-tools` removed, Tool search lists `list_toolsets`, `describe_toolset` and `call_tool` and `tools/list` never changes while the server runs | Auto, in memory on every load until Project Settings saves the section: script-only True becomes Python scripting only, dynamic True becomes Tool search (not with script-only also True); a saved new key wins. User: a client that called `assess-needed-tools` uses tool search instead (provisional decision 0005) | Card 0165 |

## Not verified

- Which commit the Fab 1.2.3 package was built from (see Baselines).
- That a whole-section Project Settings save in 2.0 drops the stale 1.2.x keys (S18 to S23) from the ini; PR #35
  says so for the sign-in keys, but it was not re-run here. A single-key save (any non-container edit) keeps them,
  and card 0418 leaves them on purpose.
- The secrets location on macOS and Linux (`UserSettingsDir()` there was not checked).
- Card 0078 ran no row in an editor; card 0418 ran S2 to S4, S6, S11 to S14, S18, N2, F7 and F8 on 5.8 with a
  1.2.x-style config; card 0079 ran every row a project's files can show on 5.6, 5.7 and 5.8 (see Verification).
- U1 with a `LoadedUIs` entry: a windowed v1.2.3 start wrote none (see U1), so the 2.0 warning path was not run.
- U2 to U5, A1 and A2 (Blueprint-facing and C++ API): card 0079 checked a project's settings and saved files only;
  these need 1.2.x Blueprint assets or third-party C++ and were not run.
- S5's request payload (`deepseek-flash`) is covered by PR #33's tests, not by the 0079 check.

## Verification (card 0079)

`Tests/Migration/migration_12x_check.py` (README next to it) runs every row below against real 1.2.x data and
prints PASS / FAIL / KNOWN / SKIP per row. Its fixtures were written by 1.2.x itself: v1.2.3 (tag c7aa4c0) and
origin/main (cb1483a) built for UE 5.6 into scratch hosts, every setting set through 1.2.x's own
`PostEditChangeProperty` save, the four API keys saved through `UN2CUserSecrets` (dummy values), and the `[HTTP]`
keys written by 1.2.x's startup. Added by hand, following 1.2.3's code: the reference-file lines (1.2.x saved
the array only on a whole-section save), the 1.2.3 translation folder (1.2.3 sends retired model IDs, so it could
not run a translation), the pre-release `BlueprintTags.json`, and in the main fixture a user `[HTTP]` value.
Each run makes a scratch copy of the built host, starts a headless 2.0 editor twice per fixture (1.2.3, then the
main fixture as a second project sharing the per-user secrets file), then once more with only an environment key.
Run on 2026-10-23 at dev-2.0 8c9a3c1: UE 5.6, 5.7 and 5.8 each 79 PASS, 0 FAIL, 4 KNOWN, 10 SKIP; 5.8 ran a
second time with `--live` (81 PASS: S26 and F2 with a claude-haiku-5-5 batch translation). Results were the same
on every engine and in both 5.8 runs. Card 0135 (N2C JSON 2.0.0, 2026-10-24, UE 5.8): F2 now also reads the 1.2.3
fixture's two saved `N2C_BP_*.json` files with `ReadN2CJson`; 80 PASS, 4 KNOWN, 9 SKIP, and 82 PASS with `--live`.
Card 0186 (settings load order, 2026-10-26, UE 5.8): S24 and S25 pass; 84 PASS, 0 FAIL, 0 KNOWN, 9 SKIP. A missing
reference file is reported at every start while it stays missing; the idempotence row does not count that warning.

| Row | 1.2.3 fixture | 1.2.x-main fixture | Result |
|---|---|---|---|
| S1 | Gemini kept | LMStudio kept | Pass |
| S2 | Claude3_5_Haiku -> Claude5_5_Sonnet, warned once, written | Claude4_Opus -> Claude5_5_Sonnet, warned once, written | Pass |
| S3 | GPT_o1 (retired) -> GPT_6_1_Sol, warned once, written | GPT_4_1 kept, line untouched | Pass |
| S4 | Gemini_2_5_ProExp: engine import error and NodeToCode warning on start 1, Gemini_3_8_Flash written, start 2 silent | Gemini_2_5_Pro -> Gemini_3_8_Flash, warned once | Pass |
| S5 | DeepSeek_V3 kept, no warning | Same | Pass (payload not checked) |
| S6 | NumCtx/NumPredict 0, bUseSystemPrompts True, overrides on for changed samplers only, written with the Info line | Same, PrependedModelCommand kept | Pass |
| S7 | Kept | Kept | Pass |
| S8 to S10 | Not in the tag | Kept | Pass (main) |
| S11 | Only the user's GPT4o_2024_08_06 1.5/6 stays; GPT_o3 costs the registry's 10/40 | Only the user's GPT_4_1 1.5/6 stays | Pass |
| S12 | Map empty (the user's price was for a retired model) | Same | Pass |
| S13 | Whole map failed to import on start 1, `GeminiModelPricing=()` written, start 2 silent | Map empty | Pass |
| S14 | User's DeepSeek_V3 0.2/0.5 kept, R1's 1.2.x price dropped | Same | Pass |
| S15 to S17 | Kept | Kept | Pass |
| S18 to S23 | Keys left in the ini, one warning names all six, 2.0 theme in use | Same | Pass |
| S24 | Recomputed: 57 (the saved 999 is ignored) | Same | Pass (card 0186) |
| S25 | Missing `Removed.h` dropped with a warning at each start; kept in the ini | Same | Pass (card 0186) |
| S26 | Value kept | Value kept; `--live` batch translation landed there | Pass |
| N1 to N7 | Defaults; `SettingsVersion=3` written on start 1 | Same | Pass |
| K1 | Project file and `User` folder removed, per-user file `Version` 1 with the four keys, Project Settings shows them, log names both paths, no key in logs | Moved too | Pass |
| K2 | - | First project's keys kept, this one's under `MigrationConflicts.<its file>`, warning logged | Pass |
| K3 | Environment key used and named, field empty, nothing saved | - | Pass |
| K4 | Not 1.2.x data | - | Skip |
| F1 | Translation folder byte-identical after two starts, not listed in graph state | No folder | Pass |
| F2 | Both saved `N2C_BP_*.json` (1.0.0) read into the 2.0.0 model, valid, 1 graph, 2 nodes; `--live`: new N2C JSON says 2.0.0 | No saved translation | Pass |
| F3, F4 | Pre-release tags migrated into `BlueprintGraphState.json`, file renamed `.migrated`, not migrated again | No state file written | Pass |
| F6 | No `Saved/NodeToCode.log` | Same | Pass |
| F7 | Both 1.2.x keys removed, rest of DefaultEngine.ini unchanged | User's `HttpConnectionTimeout=120` and `HttpTimeout=180` kept, 3600 removed | Pass |
| F8 | One warning on start 1, none on start 2, preference left off | Same | Pass |
| F9 | `bEnableMcpServer` true, Python off, server on 27000 and 27001 | Same (lines not logged at Min Severity Warning) | Pass |
| Idempotence | Start 2: both ini files, loaded values and graph state unchanged, no NodeToCode warning, no import error | Same | Pass |

No `EnumRedirects` were added: no 1.2.x enum value was renamed; the one removed name (Gemini_2_5_ProExp) has no
2.0 equivalent, and card 0418's write-back gives the default with a warning, checked on 5.6 to 5.8.

## Other release notes found while writing this

- dev-2.0's `NodeToCode.uplugin` said `"VersionName": "1.3"`; card 0074 set `Version` 3 and `VersionName`
  "2.0.0-beta.1", with `IsBetaVersion` true, `Installed` false and the Fab `MarketplaceURL`.
