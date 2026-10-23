# NodeToCode Architecture

Core plugin structure, editor integration, utilities, settings, and content organization.

## Directory Structure

The source code follows a Public/Private split for proper header visibility:

```
Source/
├── Public/                      # Public headers (API surface)
│   ├── Core/                    # Editor integration, settings, widgets
│   │   └── Widgets/             # Slate widget headers
│   ├── Auth/                    # OAuth authentication types and managers
│   ├── Models/                  # Data models (N2CStyle, N2CLogging)
│   └── Utils/                   # Utility classes and validators
│       └── Validators/          # Blueprint, Node, Pin validators
├── Private/                     # Private implementations
│   ├── Core/                    # Implementation files
│   │   └── Widgets/             # Widget implementations
│   ├── Auth/                    # OAuth implementation and customization
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
            *   HTTP timeouts (`UHttpTimeoutConfig` for `DefaultEngine.ini`)
            *   Editor performance settings (disables "Use Less CPU when in Background")
            *   User secrets (`UN2CUserSecrets`)
            *   Styles (`N2CStyle`, `FN2CCodeEditorStyle`)
            *   Editor integration (`FN2CEditorIntegration`)
            *   Widget factories (`FN2CCodeEditorWidgetFactory`)
            *   Syntax definitions (`FN2CSyntaxDefinitionFactory`)
            *   MCP HTTP server (`FN2CMcpHttpServerManager`)
            *   SSE server (`NodeToCodeSseServer`) for long-running operations
            *   OAuth settings customization (`FN2COAuthSettingsCustomization`)
            *   Console commands for OAuth (Claude and Gemini)
        *   `ShutdownModule`: Cleans up all resources in reverse order.
    *   `UHttpTimeoutConfig`: Helper to adjust global HTTP timeouts for LLM requests.

### Console Commands

The plugin registers console commands for OAuth authentication.

> **Note:** These OAuth commands are experimental and may violate service TOS. See [Authentication System](#authentication-system-sourceauth) for important warnings.

**Claude/Anthropic OAuth:**
*   `N2C.OAuth.Claude.Login` - Opens browser for Claude Pro/Max OAuth login
*   `N2C.OAuth.Claude.Submit <code#state>` - Submit authorization code
*   `N2C.OAuth.Claude.Logout` - Log out from Claude OAuth
*   `N2C.OAuth.Claude.Status` - Show current authentication status

**Gemini/Google OAuth:**
*   `N2C.OAuth.Gemini.Login` - Opens browser for Gemini OAuth login
*   `N2C.OAuth.Gemini.Submit <code>` - Submit authorization code
*   `N2C.OAuth.Gemini.Logout` - Log out from Gemini OAuth
*   `N2C.OAuth.Gemini.Status` - Show current authentication status

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

## Authentication System (`Source/*/Auth/`)

OAuth authentication support for LLM providers.

> **⚠️ EXPERIMENTAL - USE AT YOUR OWN RISK ⚠️**
>
> The OAuth authentication systems for Claude (Anthropic) and Gemini (Google) are **highly experimental** and intended for **research purposes only**. These features:
> - **Will NOT be actively maintained**
> - **May violate the Terms of Service** of Claude Code and Gemini CLI
> - **Could result in account suspension or banning** by Anthropic or Google
>
> Users should be fully aware of these risks before enabling OAuth authentication. The recommended approach is to use standard API keys instead.

*   **`Source/Public/Auth/N2COAuthTypes.h`**: OAuth type definitions including:
    *   `EN2COAuthProvider` - Provider enum (Anthropic, Google)
    *   `EN2CAnthropicAuthMethod` - APIKey or OAuth
    *   `EN2CGeminiAuthMethod` - APIKey or OAuth
    *   `FN2COAuthTokens` - Token storage struct

*   **`Source/Public/Auth/N2COAuthTokenManagerBase.h`**: Base class for OAuth token managers with refresh token logic.

*   **`Source/Public/Auth/N2CAnthropicOAuthTokenManager.h`**: Anthropic/Claude OAuth manager (PKCE flow).

*   **`Source/Public/Auth/N2CGoogleOAuthTokenManager.h`**: Google/Gemini OAuth manager.

*   **`Source/Private/Auth/N2COAuthSettingsCustomization.h`**: Property customization for OAuth settings in Project Settings.

## Plugin Settings

*   **`Source/Public/Core/N2CSettings.h` / `Source/Private/Core/N2CSettings.cpp` (`UN2CSettings`)**: `UDeveloperSettings` class for all plugin configurations:

    **LLM Provider Settings:**
    *   `Provider` - Selected LLM provider (`EN2CLLMProvider`)
    *   `AnthropicModel`, `AnthropicAuthMethod` - Anthropic settings
    *   `OpenAI_Model` - OpenAI model selection
    *   `Gemini_Model`, `GeminiAuthMethod` - Gemini settings
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

*   **`Source/Public/Core/N2CUserSecrets.h` / `Source/Private/Core/N2CUserSecrets.cpp` (`UN2CUserSecrets`)**: UObject for secure local storage of API keys and OAuth tokens in `secrets.json`.
    *   API keys: `OpenAI_API_Key`, `Anthropic_API_Key`, `Gemini_API_Key`, `DeepSeek_API_Key`
    *   Unified OAuth storage API: `SetOAuthTokensForProvider()`, `GetOAuthTokensForProvider()`, `ClearOAuthTokensForProvider()`, `HasOAuthTokensForProvider()`
    *   Legacy OAuth methods for backward compatibility

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
