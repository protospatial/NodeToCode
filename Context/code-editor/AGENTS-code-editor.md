# NodeToCode Code Editor

In-plugin code viewing component with syntax highlighting and theme support.

## Directory Structure

```
Source/Public/Code Editor/
├── Models/
│   ├── N2CCodeLanguage.h           - Language enum (EN2CCodeLanguage)
│   └── N2CCodeEditorStyle.h/.cpp   - Global style management
├── Syntax/
│   ├── N2CSyntaxDefinition.h       - Abstract base for syntax rules
│   ├── N2CSyntaxDefinitionFactory.h - Factory singleton
│   ├── N2CRichTextSyntaxHighlighter.h - Syntax highlighting engine
│   ├── N2CWhiteSpaceRun.h          - Tab rendering handling
│   └── N2C*SyntaxDefinition.h      - Language-specific implementations
└── Widgets/
    ├── SN2CCodeEditor.h            - Core Slate widget
    ├── N2CCodeEditorWidget.h       - UMG wrapper widget
    └── N2CCodeEditorWidgetFactory.h - Widget factory registration

Source/Private/Code Editor/
├── Syntax/
│   ├── N2CSyntaxDefinitionFactory.cpp
│   ├── N2CRichTextSyntaxHighlighter.cpp
│   ├── N2CWhiteSpaceRun.cpp
│   └── N2C*SyntaxDefinition.cpp    - Language implementations
└── Widgets/
    ├── SN2CCodeEditor.cpp
    ├── N2CCodeEditorWidget.cpp
    └── N2CCodeEditorWidgetFactory.cpp
```

## Core Components

### Widgets

*   **`SN2CCodeEditor` (`Source/Public/Code Editor/Widgets/SN2CCodeEditor.h`)**: Core Slate widget for code display/editing, using `SMultiLineEditableText` and `FN2CRichTextSyntaxHighlighter`. Supports language switching, theme switching, font size, word wrap, tab size, cursor positioning, and text selection.

*   **`UN2CCodeEditorWidget` (`Source/Public/Code Editor/Widgets/N2CCodeEditorWidget.h`)**: UMG wrapper for `SN2CCodeEditor`, exposing Blueprint-callable functions and events (`OnTextChanged`, `OnCursorMoved`, `OnSelectionChanged`). Kept so user Editor Utility Widgets that embed it keep loading; unsupported and planned for removal (the plugin's own EUW UI was removed in 2.0).

*   **`FN2CCodeEditorWidgetFactory` (`Source/Public/Code Editor/Widgets/N2CCodeEditorWidgetFactory.h`)**: Factory class for registering the UMG widget with the editor. Handles `Register()` and `Unregister()` lifecycle.

### Syntax System

*   **`FN2CSyntaxDefinition` (`Source/Public/Code Editor/Syntax/N2CSyntaxDefinition.h`)**: Abstract base class for language-specific syntax rules. Defines interface for keywords, operators, comment delimiters, string delimiters, and bracket types.

*   **`FN2CSyntaxDefinitionFactory` (`Source/Public/Code Editor/Syntax/N2CSyntaxDefinitionFactory.h`)**: Singleton factory that creates `FN2CSyntaxDefinition` instances based on `EN2CCodeLanguage`.

*   **`FN2CRichTextSyntaxHighlighter` (`Source/Public/Code Editor/Syntax/N2CRichTextSyntaxHighlighter.h`)**: Performs syntax highlighting using `FSyntaxHighlighterTextLayoutMarshaller`. The tokenizer's rules are the definition's operators, longest first (`FSyntaxTokenizer` compares rules literally and takes the first match; keywords arrive as whole words without rules). `ParseTokens` finds strings and comments character by character: a string closes at its own opening delimiter, skips delimiters escaped by an odd run of backslashes, and ends at the end of its line unless the definition allows a multi-line form (`SupportsTripleQuotedStrings`, `IsMultiLineStringDelimiter`, `SupportsRawStrings`, `SupportsVerbatimStrings`). Brackets, keywords and operators are one case-sensitive `TMap` lookup; lines over `MaxHighlightedLineLength` (2,000) render plain. `CreateJson` builds the JSON highlighter (JavaScript colors) that `SN2CCodeEditor::SetJsonSyntax` uses for the translation viewer's JSON tab. Specs: `NodeToCode.UI.SyntaxHighlighter`.

*   **`FN2CWhiteSpaceRun` (`Source/Public/Code Editor/Syntax/N2CWhiteSpaceRun.h`)**: Custom `FSlateTextRun` for handling tab width rendering with configurable spaces-per-tab.

### Language Syntax Definitions

Located in `Source/Public/Code Editor/Syntax/`:

| File | Class | Language |
|------|-------|----------|
| `N2CCPPSyntaxDefinition.h` | `FN2CCPPSyntaxDefinition` | C++ |
| `N2CPythonSyntaxDefinition.h` | `FN2CPythonSyntaxDefinition` | Python |
| `N2CJavaScriptSyntaxDefinition.h` | `FN2CJavaScriptSyntaxDefinition` | JavaScript |
| `N2CCSharpSyntaxDefinition.h` | `FN2CCSharpSyntaxDefinition` | C# |
| `N2CSwiftSyntaxDefinition.h` | `FN2CSwiftSyntaxDefinition` | Swift |
| `N2CPseudocodeSyntaxDefinition.h` | `FN2CPseudocodeSyntaxDefinition` | Pseudocode |
| `N2CJsonSyntaxDefinition.h` | `FN2CJsonSyntaxDefinition` | JSON (viewer JSON tab only; not an `EN2CCodeLanguage`, made by `FN2CSyntaxDefinitionFactory::CreateJsonDefinition`) |

### Models

*   **`EN2CCodeLanguage` (`Source/Public/Code Editor/Models/N2CCodeLanguage.h`)**: Enum defining supported languages: `Cpp`, `Python`, `JavaScript`, `CSharp`, `Swift`, `Pseudocode`.

*   **`FN2CCodeEditorStyle` (`Source/Public/Code Editor/Models/N2CCodeEditorStyle.h`)**: Manages global `FSlateStyleSet` for code editor syntax styles. Initializes per-language styles from `UN2CSettings` themes.

## Supported Languages

*   C++
*   Python
*   JavaScript
*   C#
*   Swift
*   Pseudocode

## Theme Configuration

Code editor themes are configured via `FN2CCodeEditorThemes` in `UN2CSettings` (`Source/Public/Core/N2CSettings.h`). Themes are stored as a `TMap<FName, FN2CCodeEditorColors>`.

### Color Properties (`FN2CCodeEditorColors`)

Each theme defines colors for:

*   **NormalText** - Default text color
*   **Operators** - Operators (+, -, *, /, etc.)
*   **Keywords** - Language keywords (if, while, class, etc.)
*   **Strings** - String literals
*   **Numbers** - Numeric literals (integers, floats, hex)
*   **Comments** - Single-line and block comments
*   **Preprocessor** - Preprocessor directives (#include, #define)
*   **Parentheses** - ( and )
*   **CurlyBraces** - { and }
*   **SquareBrackets** - [ and ]
*   **Background** - Editor background color

### Built-in Themes

The plugin includes several built-in themes:
*   Spacedust
*   Ubuntu
*   Renaissance
*   Unreal Engine (default)

### Syntax Highlighting Token Types

The `FN2CRichTextSyntaxHighlighter::FSyntaxTextStyle` struct maps colors to these token types:
*   Normal, Operator, Keyword, String, Number, Comment, Preprocessor, Parentheses, CurlyBraces, SquareBrackets

## Adding a New Language

1. Create a new syntax definition class inheriting from `FN2CSyntaxDefinition`
2. Implement all virtual methods (keywords, operators, comment delimiters, etc.)
3. Add the language to `EN2CCodeLanguage` enum
4. Update `FN2CSyntaxDefinitionFactory::CreateDefinition()` to handle the new language
5. Add style initialization in `FN2CCodeEditorStyle::InitializeLanguageStyles()`
