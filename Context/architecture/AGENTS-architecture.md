# NodeToCode Architecture

Core plugin structure, editor integration, utilities, settings, and content organization.

## Directory Structure

The source code follows a Public/Private split for proper header visibility:

```
Source/
├── Public/                      # Public headers (API surface)
│   ├── Core/                    # Editor integration, settings, widgets
│   │   ├── Style/               # UI color, spacing and font tokens, Slate style set
│   │   └── Widgets/             # Slate widget headers
│   ├── Models/                  # Data models (N2CLogging, N2CBlueprint)
│   └── Utils/                   # Utility classes and validators
│       └── Validators/          # Blueprint, Node, Pin validators
├── Private/                     # Private implementations
│   ├── Core/                    # Implementation files
│   │   ├── Style/               # Style set and palette accessor
│   │   └── Widgets/             # Widget implementations
│   ├── Models/                  # Model implementations
│   └── Utils/                   # Utility implementations
│       └── Validators/          # Validator implementations
├── NodeToCode.Build.cs          # Build configuration
├── Public/NodeToCode.h          # Module header
└── Private/NodeToCode.cpp       # Module implementation
```

## Plugin Module & Core Setup

*   **`NodeToCode.uplugin`**: Descriptor file (version, author, module definition: "NodeToCode" Editor module, PostEngineInit loading, Win64/Mac platforms). Declares plugin dependencies: `PythonScriptPlugin`, `EditorScriptingUtilities`.
*   **`Source/NodeToCode.Build.cs`**: Build script defining dependencies:
    *   **Public**: Core, CoreUObject, Engine, InputCore, Json, UnrealEd, BlueprintGraph, Slate, SlateCore, Kismet, GraphEditor, HTTP, UMG, ToolMenus, ApplicationCore, Projects, HTTPServer
    *   **Private**: DeveloperSettings, Blutility, UMGEditor, KismetWidgets, EditorSubsystem, EditorWidgets, AssetRegistry, ContentBrowserData, DesktopPlatform, PythonScriptPlugin, EditorScriptingUtilities
    *   **Third-Party**: cpp-httplib (header-only HTTP library in `ThirdParty/cpp-httplib/`; vendored but included by no source file since card 0113, see its README)
*   **`Source/Public/NodeToCode.h` & `Source/Private/NodeToCode.cpp`**:
    *   `FNodeToCodeModule`: Main plugin class.
        *   `StartupModule`: Initializes:
            *   Logging system (`FN2CLogger`)
            *   No editor preference is written at startup: HTTP timeouts are set per request (`UN2CHttpHandlerBase`), and CPU throttling is suspended only while a translation runs (a `ShouldDisableCPUThrottlingDelegates` entry). The MCP servers are skipped in commandlets and unattended runs unless `-N2CMcp` is passed
            *   API keys (`FN2CSecretStore::Get()`; on Windows it first moves the per-user and a project's old `secrets.json` into Windows Credential Manager)
            *   Settings carried over from NodeToCode 1.2.x (`UN2CSettings::MigrateLegacy12Settings`, not in commandlets): writes back model selections and other values that did not load as saved, and, once (`SettingsVersion` < 3), drops stale prices, reports the per-language theme keys, removes the 1.2.x HTTP timeouts from `Config/DefaultEngine.ini` and reports the CPU-throttle preference. The only startup write to project config; nothing is written when there is nothing to migrate. See `Context/release/migration-1.2-to-2.0.md`
            *   Styles (`N2CStyle`, `FN2CCodeEditorStyle`)
            *   Editor integration (`FN2CEditorIntegration`)
            *   Widget factories (`FN2CCodeEditorWidgetFactory`)
            *   Syntax definitions (`FN2CSyntaxDefinitionFactory`)
            *   MCP HTTP server (`FN2CMcpHttpServerManager`), the only listener the plugin opens
            *   `ShutdownModule`: Cleans up all resources in reverse order.

## Editor Integration & UI

Connects plugin logic to the Unreal Editor UI.

### Core Integration

*   **`Source/Public/Core/N2CEditorIntegration.h` / `Source/Private/Core/N2CEditorIntegration.cpp` (`FN2CEditorIntegration`)**: the editor module's wiring and shared entry points (card 0344).
    *   `Initialize` / `Shutdown`: the N2C tab spawner, the focused-editor tracker and the graph overlay injector below
    *   Its `GetActiveBlueprintEditor` and `GetFocusedGraphFromActiveEditor` forward to the tracker below
    *   The focused-graph capture facade (`GetFocusedBlueprintAsJson`, `GetGraphAsJson`, `CollectNodesFromGraph`, `TranslateNodesToN2CBlueprint*`) and translation dispatch (`TranslateFocusedBlueprintAsync()`, `TranslateJsonAsync`, `ResolveTranslationOverrides`) for the overlay, the main window and the MCP tools; they move to the capture module and `UN2CTranslationSubsystem` with cards 0010 and 0254
    *   Manages global translation state across overlays
    *   Delegates: `OnTranslationStateChanged`, `OnOverlayTranslationRequested`

*   **`Source/Public/Core/N2CGraphOverlayInjector.h` / `Source/Private/Core/N2CGraphOverlayInjector.cpp` (`FN2CGraphOverlayInjector`)**: the one place that walks the engine's graph editor widgets (card 0344). Injects the graph overlays (card 0293): into the exact `SGraphEditor` that shows the graph (`FindGraphEditorFor`: the graph's own document tab, else `SGraphEditor::FindGraphEditorForGraph` limited to the editor's tabs; never another graph's tab), into the `SOverlay` around its graph panel (`FindGraphEditorOverlay`), one per graph editor (`InjectedGraphOverlays` keyed by `TWeakPtr<SGraphEditor>`). Triggers: a Blueprint editor opening (`OnAssetOpenedInEditor`, retried until it shows a graph), a graph tab coming to the front, and a 2 s poll (`n2c.OverlayPoll`) that backs off per graph and gives up after `MaxPollInjectionAttempts`; the tab event and the poll share one pass, `InjectMissingOverlays`. Editors come from the tracker below, and injection never changes the focus. Shutdown takes every overlay out again (`RemoveInjectedOverlays`, `SOverlay::RemoveSlot`). Injection logs at Debug only

*   **`Source/Public/Core/N2CBlueprintEditorTracker.h` / `Source/Private/Core/N2CBlueprintEditorTracker.cpp` (`FN2CBlueprintEditorTracker`)**: the single definition of the focused Blueprint editor (card 0150): the Blueprint editor last activated through `OnTabForegrounded`, `OnActiveTabChanged` or `OnAssetOpenedInEditor`, with a fallback to the most recently activated editor still open. `GetEditorFor(UBlueprint*)` gives a Blueprint's own editor (checked with `IsBlueprintEditor()`). See "Which Blueprint is focused" in `Source/Private/MCP/Tools/Implementations/Blueprint/AGENTS.md`

*   **`Source/Public/Core/N2CEditorWindow.h` / `Source/Private/Core/N2CEditorWindow.cpp` (`SN2CEditorWindow`)**: Slate widget container for the main plugin UI. Registers a nomad tab spawner, embedding `SN2CMainWindow`.

### Slate Widgets (`Source/Public/Core/Widgets/`)

Pure Slate widgets for the plugin UI:

*   **`SN2CMainWindow`**: Main plugin window (pure Slate implementation)
*   **`N2CMainWindowWidget`**: UMG wrapper for embedding in Editor Utility Widgets (kept for compatibility, unsupported, planned for removal; shows a placeholder in the UMG designer)
*   **`SN2CGraphOverlay`**: Overlay widget for Blueprint graph tabs (put there by `FN2CGraphOverlayInjector`); its Copy JSON and Translate buttons are the editor's entry points for them
*   **`SN2CTagManager`**, **`SN2CTagCategoryTree`**, **`N2CTagManagerWidget`**: Tag management UI (`N2CTagManagerWidget` is the UMG wrapper: kept, unsupported, planned for removal)
*   **`SN2CTaggedGraphsList`**, **`SN2CGraphListRow`**: Tagged graphs list view
*   **`SN2CBatchProgressModal`**: Progress modal for batch translations
*   **`SN2CTranslationViewer`**: Translation result viewer

### Legacy UI Assets

The Editor Utility Widget UI (`Content/UI`) was removed in 2.0; the plugin UI is the Slate widgets above.

### Styling

All presentation tokens live in `Source/Public/Core/Style/` (implementations in `Source/Private/Core/Style/`), so widget code does not include `Core/N2CSettings.h`:

*   **`N2CUIColors.h` / `N2CUIColors.cpp` (`FN2CUIColors`, namespace `N2C::UI`)**: The color tokens. The live palette is the `UIColors` property on `UN2CSettings`; widgets read it with `N2C::UI::Colors()` (defined in the .cpp, the only style file that includes the settings header), convert with `N2C::UI::Linear` / `LinearAlpha`, and bind with `N2C::UI::Bind` / `BindAlpha`, which return a `TAttribute<FSlateColor>` re-read on every paint so a Project Settings edit shows in open widgets at once.
*   **`N2CDesignTokens.h`**: Spacing (`FN2CSpacing`), font (`FN2CFonts`) and sizing (`FN2CSizing`) tokens.
*   **`N2CStyle.h` / `N2CStyle.cpp` (`N2CStyle`)**: The plugin's Slate style set (toolbar icon, button, border, combo box and widget styles with stable addresses). `UN2CSettings::PostEditChangeProperty` calls its `Update*` functions when `UIColors` changes. Provides the `N2C_PLUGIN_BRUSH` macro for content paths.
*   **`Source/Public/Core/N2CWidgetContainer.h` / `Source/Private/Core/N2CWidgetContainer.cpp` (`UN2CWidgetContainer`)**: Persistent UObject outer for widgets to maintain state.
*   **`Source/Public/Core/N2CTargetLanguage.h` / `Source/Private/Core/N2CTargetLanguage.cpp` (`FN2CTargetLanguage`)**: the target-language table (card 0250), one row per `EN2CCodeLanguage` value: `Id` (what translation records store: `Cpp`, `Python`, ...), `DisplayName`, `ImplementationExtension` (Pseudocode is `.md`), `DeclarationExtension` and `bHasDeclarationFile` (C++ only, `.h`), `LegacyImplementationExtension` (Pseudocode's old `.txt`, still read), `PromptKey` and the MCP description, category and features. `N2C::GetLanguage(Enum)`, `N2C::FindLanguage(Name)` (id in any case, `EN2CCodeLanguage::X`, display name or prompt key) and `N2C::GetPromptKey(Enum, Family)` (`CodeGen_CPP`; `N2C::BlueprintPromptFamily` is `CodeGen`, room for another source kind's family). The save paths, `LoadTranslation`, `FN2CPathUtils`' language helpers, the prompt manager, the translation viewer's tab labels, the translate overrides, `get-available-translation-targets` and the MCP prompts read it. The code editor keeps its own language-to-syntax and style maps (the table must not depend on Slate); `NodeToCode.Languages` checks both sides cover every enum value. Adding a language: an enum value, a row, `Content/Prompting/CodeGen_<PromptKey>.md`, and the code editor's syntax definition and style.

## Plugin Settings

*   **`Source/Public/Core/N2CSettings.h` / `Source/Private/Core/N2CSettings.cpp` (`UN2CSettings`)**: `UDeveloperSettings` class for all plugin configurations:

    **LLM Provider Settings:**
    *   `Provider` - Selected LLM provider (`EN2CLLMProvider`)
    *   `AnthropicModelId`, `OpenAIModelId`, `GeminiModelId`, `DeepSeekModelId` - model selection by catalog model ID, each with a `<Provider>CustomModelId` sent instead when set; `DeepSeekReasoningEffort` - DeepSeek thinking mode (settings version 4; configs saved with the old enum keys `AnthropicModel`, `OpenAI_Model`, `Gemini_Model`, `DeepSeekModel` are converted once, see `Context/llm-providers/AGENTS-llm-providers.md`)
    *   `OllamaConfig`, `OllamaModel` - Ollama settings
    *   `LMStudioModel`, `LMStudioEndpoint`, `LMStudioPrependedModelCommand` - LM Studio settings
    *   `ModelPricing` - price overrides keyed by model ID (replaces the four per-provider maps)

    **Code Generation Settings:**
    *   `TargetLanguage` - Target programming language (`EN2CCodeLanguage`)
    *   `TranslationDepth` - Max depth for nested graph translation (0-5)
    *   `ReferenceSourceFilePaths` - Source files for LLM context
    *   `CustomTranslationOutputDirectory` - Custom output path for translations

    **MCP Server Settings:**
    *   `McpServerPort` - HTTP server port (default: 27000); `McpServerPortRange` - fallback ports tried when it is taken (default: 10)
    *   `McpToolProfile`, `McpCustomToolsets` - which toolsets the MCP server serves (default: all; Python scripting only)
    *   `McpToolExposure` - list every tool, or tool search (`list_toolsets`, `describe_toolset`, `call_tool`)

    **Theming:**
    *   `UIColors` (`FN2CUIColors`, `Core/Style/N2CUIColors.h`) - the window's colors, read through `N2C::UI::Colors()`; spacing, font and sizing tokens are constants in `Core/Style/N2CDesignTokens.h` (see `Context/code-editor/AGENTS-code-editor.md`, Colors)
    *   `CodeEditorTheme` (`FN2CCodeEditorThemes`) - one theme for every language; the code editor draws its "Unreal Engine" entry. 1.2.x's per-language theme maps are no longer read (migration S18)
    *   `FN2CCodeEditorColors` struct with: NormalText, Operators, Keywords, Strings, Numbers, Comments, Preprocessor, Parentheses, CurlyBraces, SquareBrackets, Background

    **Logging:**
    *   `MinSeverity` - Minimum log severity level

*   **`Source/Public/Core/N2CSecretStore.h` / `Source/Private/Core/N2CSecretStore.cpp`**: the provider API keys.
    *   `FN2CSecretStore` (not a UObject) is the one owner of the keys in the process: `FN2CSecretStore::Get()`. Everything reads and writes keys through it (`GetApiKey`, `HasApiKey`, `SetApiKey`), keyed by provider id(`GetProviderId`: `OpenAI`, `Anthropic`, `Gemini`, `DeepSeek`). Nothing else may hold a copy of the keys that it later writes back. `UN2CSettings` has no secrets member. Its `*_API_Key_UI` fields are write-only (card 0161): `EditAnywhere`, `Transient`, `PasswordField`, no Blueprint access; `PostEditChangeProperty` saves an entered key through the store and empties the field. The `*_API_Key_Source` rows (shown as API Key Status) come from `DescribeApiKeySource`: the store's name and the key's last four characters, the environment variable's name, or "Not set". Blueprints and Python get only `UN2CSettings::SetApiKey(Provider, Key)` (empty removes) and `HasApiKey(Provider)`; the `Remove Saved <Provider> Key` buttons are `CallInEditor` functions. `NodeToCode.LLM.CredentialSource.Reflection` fails on any Blueprint-visible key property or Blueprint function returning a key string.
    *   The store sits on an `IN2CSecretBackend` (`LoadAll`, `StoreKey`, `Describe`, `GetDisplayName`); tests build a store over a backend in a temporary folder or under a test target prefix.
    *   `FN2CCredentialManagerBackend` is the Windows backend (card 0161, decision card 0061 option (a), applied provisionally): one `CRED_TYPE_GENERIC` credential per provider, target `NodeToCode/<ProviderId>`, user name `NodeToCode`, the key as a UTF-8 blob (a UTF-16 blob from `cmdkey /generic:` reads too), `CRED_PERSIST_LOCAL_MACHINE` (this user, this machine, no roaming). `LoadAll` enumerates `NodeToCode/*`; an empty key deletes the entry. Every call holds the session-wide mutex `Local\NodeToCode.CredentialManager` (all threads of all NodeToCode processes), and every write and delete is read back with `CredReadW` and repeated up to `MaxWriteAttempts` times until it holds; the written key is put in the returned map directly. Card 0496 found that Windows can report a `CredWriteW` or `CredDeleteW` as done and not keep it while another process changes credentials (two processes writing one entry at a time lost about 1 write in 100 and brought deleted entries back; concurrent writers in one process plus another process lost writes even after they read back). `FN2CSecretStore::SetApiKey` and `Reload` keep a key they knew that the new listing leaves out while `IN2CSecretBackend::ReadKey` still finds it. Links `Advapi32.lib` on Win64 only. Tests use the prefix `NodeToCodeTest/<guid>/` (`NodeToCode.Secrets.CredentialManager`). On Mac and Linux (not allowlisted, decision 0001) `IsSupported()` is false and `Get()` uses the file backend below, so no source change is needed to add them.
    *   `FN2CSecretStore::MigrateFileToBackend` runs once in `Get()` on Windows: the file backend loads the per-user file (merging a project file first, as below), every non-empty key is written to Credential Manager (a different key already there wins; the file's key goes to `<ProviderId>.Conflict.<n>`, as do keys under `MigrationConflicts`), everything written is read back, and only then are `secrets.json`, `.bak` and `.tmp` deleted. Any failure leaves the files and logs why; the next start retries. A `secrets.json.corrupt-*` copy is named in a warning, never moved or deleted.
    *   `FN2CSecretFileBackend` is the Phase 0 backend, now the store on Mac and Linux and the migration source on Windows: `FPlatformProcess::UserSettingsDir()/NodeToCode/secrets.json` (Windows `%LOCALAPPDATA%`), per user, not per project. Fields are `<ProviderId>_API_Key` plus `Version`. Every write re-reads the file under the store's lock and changes one field (unknown fields kept), writes `secrets.json.tmp`, copies the old file to `secrets.json.bak` and moves the temporary file over it. A file that does not parse is renamed to `secrets.json.corrupt-<UTC timestamp>` and never written over. Loading deletes the sign-in token entries older versions wrote (`RemoveRetiredSignInEntries`) and logs that once.
    *   Migration: on load, `<Project>/Saved/NodeToCode/User/secrets.json` is merged in (per-user values win, an empty per-user value is filled, a differing project key is kept under `MigrationConflicts.<project file path>`), then deleted. Logs name the provider, never the key. A project file that does not parse is left in place with an error.
    *   `FN2CPathUtils::IsDeniedForAgentAccess` refuses both locations and any `secrets.json*` file to the MCP file tools. Two editors on one machine share the file (or the Credential Manager entries); the read-modify-write keeps their edits to different keys, but there is no cross-process lock for the file (on Windows, Credential Manager calls take a session-wide mutex and each write is read back, see above).

**Authentication:** every cloud provider (OpenAI, Anthropic, Gemini, DeepSeek) authenticates with its API key only; Ollama and LM Studio need none. NodeToCode 2.0 removed Claude Pro/Max and Google account sign-in. A config that still selects it loads normally and is reported once per session (`UN2CSettings::ReportRemovedSignInSelections`), and a cloud request without a key fails before it is sent (`IN2CCredentialSource::MakeMissingKeyMessage`). Requests never read the store directly: `UN2CBaseLLMService::SendRequest` resolves each request's key through `IN2CCredentialSource` (`LLM/N2CCredentialSource.h`, card 0160) when it is sent, and no config or reflected property holds a key.

## Logging System

*   **`Source/Public/Models/N2CLogging.h`**: Defines logging types:
    *   `LogNodeToCode` - Log category declaration
    *   `EN2CLogSeverity` - Debug, Info, Warning, Error, Fatal
    *   `EN2CErrorCategory` - Syntax, Reference, Type, Connection, Validation, Internal
    *   `FN2CError` - Structured error information

*   **`Source/Public/Utils/N2CLogger.h` / `Source/Private/Utils/N2CLogger.cpp` (`FN2CLogger`)**: Singleton for centralized logging.
    *   `Log()`, `LogError()`, `LogWarning()` - Logging methods
    *   `GetErrors()`, `GetErrorsBySeverity()` - The most recent 1,000 records (a ring buffer, oldest first). Thread-safe: `Log()` locks the buffer and file append and calls UE_LOG outside the lock
    *   `SetMinSeverity()` - Filter log level
    *   `EnableFileLogging()`, `SetLogFilePath()` - File logging support
    *   `SeverityToVerbosity()` - Maps N2C severity to UE verbosity

## Utility Classes

*   **`Source/Public/Utils/N2CNodeTypeRegistry.h` / `Source/Private/Utils/N2CNodeTypeRegistry.cpp` (`FN2CNodeTypeRegistry`)**: Singleton mapping `UK2Node` classes to `EN2CNodeType` by the nearest mapped ancestor in the class hierarchy (`Unknown` when none is mapped); its public header includes no K2Node headers.

*   **`Source/Public/Utils/N2CGraphUtils.h` / `Source/Private/Utils/N2CGraphUtils.cpp` (`FN2CGraphUtils`)**: The editor's one resolver for tagged graphs (card 0251). `FindGraphByGuid` searches every graph of the Blueprint (`UBlueprint::GetAllGraphs` through `FN2CGraphTarget::FindGraphsByGuid`, so interface implementations and collapsed graphs too); `ResolveTaggedGraph` loads a tag's Blueprint and finds its graph, with a message naming the graph and Blueprint when it cannot. `CanTranslateGraph` is the rule for which graphs translation can read: a Blueprint (K2) schema graph its Blueprint lists, which leaves out AnimBP state machines and other non-K2 graphs. The main window (translate, double-click, JSON export), the batch orchestrator and token estimation all use it; the graph overlay disables Copy JSON, Translate, adding a tag and the context estimate on a graph it refuses, with the reason as the tooltip, and a caller that cannot resolve a graph shows a notification (`NotifyFailure`) instead of returning silently.

### Validation Utilities (`Source/*/Utils/Validators/`)

*   **`N2CBlueprintValidator`**: Validates Blueprint data integrity.
*   **`N2CNodeValidator`**: Validates node data.
*   **`N2CPinValidator`**: Validates pin data and connections.

## Content Files

```
Content/
├── Data/                        # Data assets
├── Prompting/                   # LLM system prompts
│   ├── CodeGen_CPP.md
│   ├── CodeGen_Python.md
│   ├── CodeGen_JavaScript.md
│   ├── CodeGen_CSharp.md
│   ├── CodeGen_Swift.md
│   ├── CodeGen_Pseudocode.md
│   └── Shared/                  # N2CJsonSpec.md, N2CResponseFormat.md: included by every CodeGen prompt
├── Python/                      # Python scripting support
│   ├── mcp_bridge/              # MCP bridge for external tools
│   ├── nodetocode/              # NodeToCode Python module
│   └── scripts/                 # User Python scripts
└── Scripts/                     # Build/setup scripts
    ├── detect-ue-path.sh        # UE path detection (Mac/Linux)
    ├── detect-ue-path.bat       # UE path detection (Windows)
    └── update_agents_ue_path.py # AGENTS.md path updater
```

**Root Files:**
*   **`.gitignore`**: Standard Git ignore.
*   **`LICENSE`**, **`README.md`**: License and documentation.
*   **`Resources/`**: Plugin/toolbar icons (`Icon128.png`, `button_icon.png`).
*   **`build.ps1`**: Build, smoke and automation-test runner for UE 5.6-5.8 (see `AGENTS.md`).
*   **`assets/`**: Documentation/promotional images.
