# Version 1.2.3 (2025-05-24)

## New Features

### LM Studio Support
- **LM Studio is now supported as a local LLM provider!**
- Visit this [LM Studio Quick Start](LM-Studio-Quick-Start) page in the Wiki to get up and running

### Prepended Model Commands for Local Providers
- **Control reasoning models with command prefixes** for both Ollama and LM Studio
- **Ollama**: Configure `PrependedModelCommand` in Ollama settings
- **LM Studio**: Configure `LMStudioPrependedModelCommand` in plugin settings
- **Common use cases:**
  - `/no_think` - Disable thinking for faster responses with Qwen3 models
  - Custom reasoning control commands for other current, or future models
- Commands are automatically prepended to translation requests with proper formatting

## Improvements

### Newly Supported Models
#### Anthropic
- **Claude 4 Sonnet** (`claude-4-sonnet-20250514`) - Now the default Anthropic model
- **Claude 4 Opus** (`claude-4-opus-20250514`) - SOTA model for the most complex blueprint translations
#### OpenAI
  - **o4-mini** (`o4-mini`) - New default OpenAI model
  - **o3** (`o3`) - OpenAI's latest flagship reasoning model
  - **GPT-4.1** (`gpt-4.1`) - Latest GPT-4 iteration
#### Gemini
- **Gemini 2.5 Flash Preview** (`gemini-2.5-flash-preview-05-20`) - Free tier model with excellent performance
- **Updated Gemini 2.5 Pro** to latest version (`gemini-2.5-pro-preview-05-06`)
	- Note: Gemini 2.5 Pro through API is no longer free, so prices have been updated to match

## Maintenance
- Added proper pricing initialization for new OpenAI Anthropic models
- Enhanced payload builder with LM Studio-specific configuration

---

# Version 1.2.2 (2025-04-08)

## Maintenance

- Fixed the HttpTimeout config notification not closing automatically after 10 seconds
- Fixed the Http DefaultEngine.ini config properties not persisting between UE 5.5 editor sessions, causing the HttpTimeout notification to reappear

# Version 1.2.1 (2025-03-29)

## New Features

### Copy N2C JSON to Clipboard 
- Quickly copy any blueprint to your clipboard in same format that's used for LLM translations
	- `Copy Blueprint JSON` is now the third option in the Node to Code dropdown where you'll already find options for opening the NodeToCode Editor or starting a translation 
	- Thanks to @YakovAU for your contribution!
- Perfect for external use with other LLM chat interfaces
### Choose a custom path for your saved translations
- Now instead of every translation only being saved to your project's Saved/NodeToCode folder, you can now pick any path on any drive to save your translations to!
- This can be great for tracking blueprint changes via version control in a more compact format, use with external documentation, lightweight backups, and more

## Improvements

### Added Support for Gemini 2.5
- Now Google's latest state of the art (and currently free) thinking model is now supported via the plugin settings!

## Maintenance
- Fixed the N2CPinValidator from outright preventing a translation from proceeding due to edge cases with some blueprint pins. Now potentially flawed pins will be flagged as warnings in the output log by the validator, but the translation request will proceed.
- The `Use Less CPU when in Background` editor setting is now force enabled when opening the editor with the NodeToCode plugin installed in order to prevent edge cases of translations failing or hanging if the editor is not currently focused.
- Fixed HTTP activity timeouts not being able to exceed 30s
	- This would prevent some Ollama requests to always fail due to responses taking longer than 30s
- Fixed API keys not always persisting between editor sessions in UE 5.4+. Now API keys are stored in %project_root%/Saved/NodeToCode/User/secrets.json
	- This is to reduce the chances of accidentally pushing your API keys to a repo as long as your default .gitignore or .p4ignore is setup correctly
- Fixed custom Ollama endpoints not being properly initialized with N2COllamaService causing a fallback to the default Ollama endpoint
- Fixed saved translations path not opening on Mac

# Version 1.2.0 (2025-03-07)

## New Features

### Automatic Blueprint Struct & Enum Collection!
- Automatically collects and processes Blueprint-defined structs and enums in your Blueprint graphs
- Includes struct members with full type information and metadata
- Supports nested struct and enum types
- Adds struct and enum definitions to generated code output

### Enhanced System Prompts
- Updated prompts for all supported languages to handle struct and enum definitions

## Improvements

### LLM Provider Architecture
- Implemented new base LLM service class for better shared functionality across providers
- Added centralized provider registry for easier provider maintenance/expansion
- Created unified payload builder system for more consistent request formatting
- Standardized error handling and response parsing across providers

### Node Processing System
- Introduced processor factory pattern for node type handling
- Added specialized processors for different node categories
- Improved node type detection and classification
- Enhanced pin type compatibility checking
- Added comprehensive validation system for nodes, pins, and graphs

## Maintenance
- Fixed UE 5.5 issues with config file saving
- Refactored node type helper into registry pattern
- Improved error logging and debug information
- Separated N2CBlueprint validation logic into dedicated validator classes
- Improved JSON response handling and error recovery

# Version 1.1.1 (2025-03-03)
## Maintenance 
- Better error handling for cases of malformed/incorrectly formatted LLM responses
- Fixed Logging MinSeverity setting not persisting between editor sessions
- Fixed debug console window appearing in UE 5.5

# Version 1.1.0 (2025-03-02)

## New Features

### Automatic Translation Saving
- **Translations are now automatically saved to disk!** You can easily revisit previous translations or share translation files with other people and applications.
- Navigate to your saved translations by clicking the new Folder button in the Translations window.
- All translations are saved to `[ProjectDir]/Saved/NodeToCode/Translations/[BlueprintName]_[Timestamp]/`

### Comprehensive File Saving
- **Blueprint JSON files are saved in both formats:**
    - `N2C_BP_[BlueprintName].json` - Prettified format for human readability
    - `N2C_BP_Minified_[BlueprintName].json` - Minified format for sharing with other LLMs or applications

- **LLM Translation output is saved as well:**
    - `N2C_Translation_[BlueprintName].json` - The complete translation response from the LLM

- **Individual graph translations:**
    - Each translated graph/function is saved to its own folder within the parent translation folder
    - File extensions match the target language (`.cpp`, `.py`, `.js`, `.cs`, `.swift`, or `.md` for Pseudocode)
    - Implementation notes are saved as separate text files

### New Pseudocode Translation Target
- **Translate blueprints into language-agnostic pseudocode** formatted in Markdown
- Perfect for:
    - Documentation
    - Communicating logic with peers, students, or non-programmers
    - Troubleshooting with other LLMs
    - Understanding complex blueprint logic without language-specific syntax

## Improvements

### LLM Provider Updates
- **Added support for Anthropic's Claude 3.7 Sonnet**
    - Now the default model when using Anthropic as the provider
    - Provides the highest quality code generation/blueprint translations available
    - Improved handling of complex blueprint logic

### Code Editor Enhancements
- Added syntax highlighting support for Pseudocode
- Updated theming system to support the new language target

### Prompt Refinements
- Improved all code translation prompts with clearer output instructions & better handling of multiple graphs
- Added new Pseudocode translation prompt following CalPoly Pseudocode Standard
- Enhanced prompt clarity for handling source files

## Maintenance
- Fixed pricing information for a couple LLM models
- Improved error handling in LLM response parsing
- Fixed UE console window appearing when editor is opened