### Getting Starting → Key Features
***

## [One-Click Translation](Accessing-and-Triggering-Translation)

Node to Code integrates seamlessly into any Blueprint Editor toolbar, providing immediate access to code translation with a single click. The plugin's core translation feature is powered by K2Node/K2Pin collection and serialization systems that capture:

- Complete execution flows
- Data connections and pin types
- Node metadata and comments
- Variable references and their contexts
- Custom function and macro implementations
- And much more

    <p align="left">
        <img src="https://github.com/protospatial/NodeToCode/blob/main/assets/Image_NodeToCode_ButtonWithTip.png" alt="Node to Code Blueprint on left, UI with code translation on right" width="512">
    </p>

## [Multi-Provider LLM Support](Choosing-an-LLM-Provider)

The plugin offers flexible API integration with multiple LLM providers:

### Cloud-Based Providers

- **Anthropic (Claude)**
    - Recommended: Claude 3.5 Sonnet for superior Unreal Engine C++ output
    - Advanced understanding of UE-specific patterns and conventions
    - Excellent handling of complex Blueprint structures
- **OpenAI**
    - Support for latest GPT models
    - Sends sampling settings only to models that accept them
    - Strong general code translation capabilities
- **DeepSeek**
    - Cost-effective alternative
    - Specialized code generation models
    - Growing support for UE-specific patterns

### Local Processing

- **Ollama Integration**
    - Run translations entirely locally
    - Support for various open-source models
    - Customizable inference parameters
    - No API costs or internet dependency

    <p align="center">
        <img src="https://github.com/protospatial/NodeToCode/blob/main/assets/Image_NodeToCode_ServiceProviders.jpg" alt="Node to Code supported LLM AI Providers" width="1250">
    </p>


## [Nested Graph Translation](Translation-Depth)

The plugin can automatically traverse and translate nested K2Node & K2Pin Blueprint graphs:

- **Configurable Depth**: Set how deep the translator should go (0-5 levels)
- **Processes User-Created Graphs**:
    - Functions
    - Composite (collapsed) graphs
    - Macros
    - Math Expressions

    <p align="center">
        <img src="https://github.com/protospatial/NodeToCode/blob/main/assets/Image_NodeToCode_NestedTranslation.jpg" alt="Node to Code visualizing nested blueprint translation" width="1250">
    </p>

## [Node to Code Editor](Opening-the-Editor)

Built-in code preview and editing capabilities via the Node to Code Editor UI:

- **Syntax Highlighting**
    - Basic syntax highlighting for all built-in target languages
- **Editor Features**
    - View code for headers/implementation
    - Implementation notes panel
    - Quick copy functionality
    - Variety of built-in themes
    - Add or customize themes via plugin settings
    - Font size adjustment

    <p align="center">
        <img src="https://github.com/protospatial/NodeToCode/blob/main/assets/Image_NodeToCode_Theming.jpg" alt="Node to Code theme options" width="1250">
    </p>

## [Reference Source Files](Reference-Source-Files)

Improve translation accuracy by providing context from your existing codebase through the plugin settings:

- Add .h and .cpp files as references
- Guide the LLM to match your coding style
- Maintain project-specific patterns

    <p align="center">
        <img src="https://github.com/protospatial/NodeToCode/blob/main/assets/Image_NodeToCode_SourceFiles.jpg" alt="Node to Code source file reference" width="1250">
    </p>