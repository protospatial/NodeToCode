# NodeToCode Architecture

Core plugin structure, editor integration, utilities, settings, and content organization.

## Directory Structure

The source code follows a Public/Private split for proper header visibility:

```
Source/
├── Public/                      # Public headers (API surface)
│   ├── Core/                    # Editor integration, settings, widgets
│   │   └── Widgets/             # Slate widget headers
│   ├── Models/                  # Data models (N2CStyle, N2CLogging)
│   └── Utils/                   # Utility classes and validators
│       └── Validators/          # Blueprint, Node, Pin validators
├── Private/                     # Private implementations
│   ├── Core/                    # Implementation files
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
    *   **Third-Party**: cpp-httplib (header-only HTTP library in `ThirdParty/cpp-httplib/`)
*   **`Source/Public/NodeToCode.h` & `Source/Private/NodeToCode.cpp`**:
    *   `FNodeToCodeModule`: Main plugin class.
        *   `StartupModule`: Initializes:
            *   Logging system (`FN2CLogger`)
            *   No project or editor config is written at startup: HTTP timeouts are set per request (`UN2CHttpHandlerBase`), and CPU throttling is suspended only while a translation runs (a `ShouldDisableCPUThrottlingDelegates` entry). The MCP servers are skipped in commandlets and unattended runs unless `-N2CMcp` is passed
            *   Editor performance settings (disables "Use Less CPU when in Background")
            *   API keys (`FN2CSecretStore::Get()`, which also moves a project's old secrets file to the per-user one)
            *   Styles (`N2CStyle`, `FN2CCodeEditorStyle`)
            *   Editor integration (`FN2CEditorIntegration`)
            *   Widget factories (`FN2CCodeEditorWidgetFactory`)
            *   Syntax definitions (`FN2CSyntaxDefinitionFactory`)
            *   MCP HTTP server (`FN2CMcpHttpServerManager`)
            *   SSE server (`NodeToCodeSseServer`) for long-running operations
            *   `ShutdownModule`: Cleans up all resources in reverse order.

## Editor Integration & UI

Connects plugin logic to the Unreal Editor UI.

### Core Integration

*   **`Source/Public/Core/N2CEditorIntegration.h` / `Source/Private/Core/N2CEditorIntegration.cpp` (`FN2CEditorIntegration`)**: Singleton for Blueprint Editor integration.
    *   Registers toolbar extensions
    *   Maps UI commands (`FN2CToolbarCommand`) to handlers
    *   Tracks `ActiveBlueprintEditor` and manages graph tab wrapping
    *   Provides `TranslateFocusedBlueprintAsync()` for MCP tools
    *   Manages global translation state across overlays
    *   Delegates: `OnTranslationStateChanged`, `OnOverlayTranslationRequested`

*   **`Source/Public/Core/N2CToolbarCommand.h` / `Source/Private/Core/N2CToolbarCommand.cpp` (`FN2CToolbarCommand`)**: `TCommands` subclass defining toolbar UI actions:
    *   `OpenWindowCommand` - Open plugin window
    *   `CollectNodesCommand` - Collect nodes for translation
    *   `CopyJsonCommand` - Copy Blueprint JSON to clipboard

*   **`Source/Public/Core/N2CEditorWindow.h` / `Source/Private/Core/N2CEditorWindow.cpp` (`SN2CEditorWindow`)**: Slate widget container for the main plugin UI. Registers a nomad tab spawner, embedding `SN2CMainWindow`.

### Slate Widgets (`Source/Public/Core/Widgets/`)

Pure Slate widgets for the plugin UI:

*   **`SN2CMainWindow`**: Main plugin window (pure Slate implementation)
*   **`N2CMainWindowWidget`**: UMG wrapper for embedding in Editor Utility Widgets
*   **`SN2CGraphOverlay`**: Overlay widget for Blueprint graph tabs
*   **`SN2CGraphEditorWrapper`**: Wrapper for graph editor with overlay injection
*   **`SN2CTagManager`**, **`SN2CTagCategoryTree`**, **`N2CTagManagerWidget`**: Tag management UI
*   **`SN2CTaggedGraphsList`**, **`SN2CGraphListRow`**: Tagged graphs list view
*   **`SN2CBatchProgressModal`**: Progress modal for batch translations
*   **`SN2CTranslationViewer`**: Translation result viewer

### Legacy UI Assets

*   **`Content/UI/NodeToCodeUI.uasset`**: Main Editor Utility Widget Blueprint (legacy, for Blueprint-based UI customization).
*   **`Content/UI/Components/`**: `.uasset` EUW components for building `NodeToCodeUI.uasset`.
*   **`Content/UI/Textures/Icons/`**: UI icon textures.
*   **`Content/UI/Theming/`**: General UI theming assets.

### Styling

*   **`Source/Public/Models/N2CStyle.h` / `Source/Public/Models/N2CStyle.cpp` (`N2CStyle`)**: Manages plugin's global Slate style (toolbar icon). Provides `N2C_PLUGIN_BRUSH` macro for content paths.
*   **`Source/Public/Core/N2CWidgetContainer.h` / `Source/Private/Core/N2CWidgetContainer.cpp` (`UN2CWidgetContainer`)**: Persistent UObject outer for widgets to maintain state.

## Plugin Settings

*   **`Source/Public/Core/N2CSettings.h` / `Source/Private/Core/N2CSettings.cpp` (`UN2CSettings`)**: `UDeveloperSettings` class for all plugin configurations:

    **LLM Provider Settings:**
    *   `Provider` - Selected LLM provider (`EN2CLLMProvider`)
    *   `AnthropicModel` - Anthropic model selection
    *   `OpenAI_Model` - OpenAI model selection
    *   `Gemini_Model` - Gemini model selection
    *   `DeepSeekModel` - DeepSeek model selection
    *   `OllamaConfig`, `OllamaModel` - Ollama settings
    *   `LMStudioModel`, `LMStudioEndpoint`, `LMStudioPrependedModelCommand` - LM Studio settings
    *   Model pricing maps per provider

    **Code Generation Settings:**
    *   `TargetLanguage` - Target programming language (`EN2CCodeLanguage`)
    *   `TranslationDepth` - Max depth for nested graph translation (0-5)
    *   `ReferenceSourceFilePaths` - Source files for LLM context
    *   `CustomTranslationOutputDirectory` - Custom output path for translations

    **MCP Server Settings:**
    *   `McpServerPort` - HTTP server port (default: 27000)
    *   `bEnableDynamicToolDiscovery` - Dynamic tool registration mode
    *   `bEnablePythonScriptOnlyMode` - Python-only MCP mode

    **Theming:**
    *   `CPPThemes`, `PythonThemes`, `JavaScriptThemes`, `CSharpThemes`, `SwiftThemes`, `PseudocodeThemes` - `FN2CCodeEditorThemes` per language
    *   `FN2CCodeEditorColors` struct with: NormalText, Operators, Keywords, Strings, Numbers, Comments, Preprocessor, Parentheses, CurlyBraces, SquareBrackets, Background

    **Logging:**
    *   `MinSeverity` - Minimum log severity level

*   **`Source/Public/Core/N2CSecretStore.h` / `Source/Private/Core/N2CSecretStore.cpp`**: the provider API keys.
    *   `FN2CSecretStore` (not a UObject) is the one owner of the keys in the process: `FN2CSecretStore::Get()`. Everything reads and writes keys through it (`GetApiKey`, `HasApiKey`, `SetApiKey`), keyed by provider id (`GetProviderId`: `OpenAI`, `Anthropic`, `Gemini`, `DeepSeek`). Nothing else may hold a copy of the keys that it later writes back. `UN2CSettings` has no secrets member; its `*_API_Key_UI` fields only mirror the store for the details panel.
    *   The store sits on an `IN2CSecretBackend` (`LoadAll`, `StoreKey`). The OS keychain backend (board card 0161) implements that interface; tests build a store over a backend in a temporary folder.
    *   `FN2CSecretFileBackend` is the Phase 0 backend: `FPlatformProcess::UserSettingsDir()/NodeToCode/secrets.json` (Windows `%LOCALAPPDATA%`), per user, not per project. Fields are `<ProviderId>_API_Key` plus `Version`. Every write re-reads the file under the store's lock and changes one field (unknown fields kept), writes `secrets.json.tmp`, copies the old file to `secrets.json.bak` and moves the temporary file over it. A file that does not parse is renamed to `secrets.json.corrupt-<UTC timestamp>` and never written over. Loading deletes the sign-in token entries older versions wrote (`RemoveRetiredSignInEntries`) and logs that once.
    *   Migration: on load, `<Project>/Saved/NodeToCode/User/secrets.json` is merged in (per-user values win, an empty per-user value is filled, a differing project key is kept under `MigrationConflicts.<project file path>`), then deleted. Logs name the provider, never the key. A project file that does not parse is left in place with an error.
    *   `FN2CPathUtils::IsDeniedForAgentAccess` refuses both locations and any `secrets.json*` file to the MCP file tools. Two editors on one machine share the file; the read-modify-write keeps their edits to different keys, but there is no cross-process lock.

**Authentication:** every cloud provider (OpenAI, Anthropic, Gemini, DeepSeek) authenticates with its API key only; Ollama and LM Studio need none. NodeToCode 2.0 removed Claude Pro/Max and Google account sign-in. A config that still selects it loads normally and is reported once per session (`UN2CSettings::ReportRemovedSignInSelections`), and a cloud request without a key fails before it is sent (`UN2CBaseLLMService::GetMissingApiKeyError`).

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

*   **`Source/Public/Utils/N2CNodeTypeRegistry.h` / `Source/Private/Utils/N2CNodeTypeRegistry.cpp` (`FN2CNodeTypeRegistry`)**: Singleton mapping `UK2Node` classes to `EN2CNodeType`.

*   **`Source/Public/Utils/N2CPinTypeCompatibility.h` / `Source/Private/Utils/N2CPinTypeCompatibility.cpp` (`FN2CPinTypeCompatibility`)**: Static utility for checking pin connection compatibility.

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
│   └── CodeGen_Pseudocode.md
├── Python/                      # Python scripting support
│   ├── mcp_bridge/              # MCP bridge for external tools
│   ├── nodetocode/              # NodeToCode Python module
│   └── scripts/                 # User Python scripts
├── Scripts/                     # Build/setup scripts
│   ├── detect-ue-path.sh        # UE path detection (Mac/Linux)
│   ├── detect-ue-path.bat       # UE path detection (Windows)
│   └── update_agents_ue_path.py # AGENTS.md path updater
└── UI/
    ├── Components/              # EUW components for UI building
    ├── NodeToCodeUI.uasset      # Main plugin EUW (legacy)
    ├── Textures/Icons/          # UI icon textures
    └── Theming/                 # UI theming assets
```

**Root Files:**
*   **`.gitignore`**: Standard Git ignore.
*   **`LICENSE`**, **`README.md`**: License and documentation.
*   **`Resources/`**: Plugin/toolbar icons (`Icon128.png`, `button_icon.png`).
*   **`build.sh`**, **`build.ps1`**: Build scripts for Mac and Windows.
*   **`assets/`**: Documentation/promotional images.
