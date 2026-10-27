# NodeToCode Code Editor

In-plugin code viewing component with syntax highlighting in one color theme.

## Directory Structure

```
Source/Public/Code Editor/
├── Models/
│   ├── N2CCodeLanguage.h           - Language enum (EN2CCodeLanguage)
│   └── N2CCodeEditorStyle.h        - Global style management
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
├── Models/
│   └── N2CCodeEditorStyle.cpp
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

*   **`SN2CCodeEditor` (`Source/Public/Code Editor/Widgets/SN2CCodeEditor.h`)**: Core Slate widget for code display/editing, using `SMultiLineEditableText` and `FN2CRichTextSyntaxHighlighter`. Supports language switching (`SetLanguage`, and `SetJsonSyntax` for the translation viewer's JSON tab), font size, word wrap, tab size, cursor positioning, and text selection. It has no theme API: `SN2CCodeEditor::CurrentTheme` is the constant `"Unreal Engine"` (`Source/Private/Code Editor/Widgets/SN2CCodeEditor.cpp`), passed to the highlighter and used to look up the background brush.

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

*   **`FN2CCodeEditorStyle` (`Source/Public/Code Editor/Models/N2CCodeEditorStyle.h`, `Source/Private/Code Editor/Models/N2CCodeEditorStyle.cpp`)**: Owns the `N2CCodeEditor` `FSlateStyleSet`. `InitializeLanguageStyles` registers the same colors for each language id (`CPP`, `Python`, `JavaScript`, `CSharp`, `Swift`, and `Pseudocode`, which draws every token in `NormalText`) under `N2CCodeEditor.<LanguageId>.Unreal Engine.<StyleId>`, plus a `.Background` brush per language. `Reinitialize` (called by `UN2CSettings::PostEditChangeProperty` when `CodeEditorTheme` changes) updates the brushes and text styles in place, so open editors' background retints at once; it then broadcasts `FN2CCodeEditorStyle::OnThemeChanged()`, on which every open `SN2CCodeEditor` rebuilds its highlighter and text widget (`RebuildEditor`, keeping text, cursor, language, font size, tab size and wrap), so syntax colors apply at once too (card 0490). `GetLanguageStyle(LanguageId, ThemeName, StyleId)` falls back to the `Unreal Engine` entry.

## Supported Languages

*   C++
*   Python
*   JavaScript
*   C#
*   Swift
*   Pseudocode

## Colors

### Code editor theme (`CodeEditorTheme`)

There is one code editor theme for every language, and no way to pick another at run time. It is `UN2CSettings::CodeEditorTheme` (`FN2CCodeEditorThemes`, `Source/Public/Core/N2CSettings.h`; Project Settings > Plugins > Node to Code > Theming > Code Editor Theme), whose `Themes` map (`TMap<FName, FN2CCodeEditorColors>`) holds a single default entry, `"Unreal Engine"`. Only that entry is read: `FN2CCodeEditorStyle` looks it up by name, and `UN2CSettings::GetThemeColors(Language, ThemeName)` ignores both arguments and returns it. Other keys a user adds to the map are never drawn. 1.2.x's per-language theme maps and its 21 named themes are gone; on the first start 2.0 logs one warning naming any of those keys still in the config (see `Context/release/migration-1.2-to-2.0.md`, S18).

### Color Properties (`FN2CCodeEditorColors`)

`FN2CCodeEditorColors` (`Source/Public/Core/N2CSettings.h`) holds:

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

### Syntax Highlighting Token Types

The `FN2CRichTextSyntaxHighlighter::FSyntaxTextStyle` struct maps colors to these token types:
*   Normal, Operator, Keyword, String, Number, Comment, Preprocessor, Parentheses, CurlyBraces, SquareBrackets

### UI colors and design tokens (the rest of the window)

Everything around the code editor uses two other sources; widget code should use them instead of literal colors, sizes or fonts:

*   **`FN2CUIColors` (`Source/Public/Core/Style/N2CUIColors.h`)**: the color tokens (backgrounds, buttons, borders, text, accents, ...). The live palette is `UN2CSettings::UIColors` (Theming > UI Colors). Read it through `N2C::UI::Colors()` (defined in `Source/Private/Core/Style/N2CUIColors.cpp`, the one place that reads the settings) so widgets need not include `Core/N2CSettings.h`; `N2C::UI::Linear` / `LinearAlpha` convert a token to `FLinearColor`, and `N2C::UI::Bind` / `BindAlpha` return an attribute that re-reads the token each frame, so a Project Settings edit shows at once. Changing `UIColors` also calls `N2CStyle::UpdateButtonStyles()`. The code editor itself takes `TextShadow`, `TextHighlight` and `FallbackBorder` from it. Spec: `Source/Private/Tests/Core/N2CUIColors.spec.cpp`.
*   **`Source/Public/Core/Style/N2CDesignTokens.h`**: constant spacing (`FN2CSpacing::XXS` ... `XXL`, 2 to 20 px), font (`FN2CFonts::Tiny()` ... `LargeBold()`, `Mono()`, `MonoSmall()`) and sizing (`FN2CSizing::IconSM/MD/LG`, `SeparatorThickness`) tokens. These are not settings.

## Adding a New Language

1. Add the language to the `EN2CCodeLanguage` enum (`Source/Public/Code Editor/Models/N2CCodeLanguage.h`) and a row to the target-language table (`Source/Private/Core/N2CTargetLanguage.cpp`, with its `Content/Prompting/CodeGen_<PromptKey>.md` prompt; see `Context/architecture/AGENTS-architecture.md`)
2. Create a new syntax definition class inheriting from `FN2CSyntaxDefinition` and implement its virtual methods (keywords, operators, comment delimiters, etc.)
3. Update `FN2CSyntaxDefinitionFactory::CreateDefinition()` to handle the new language (`NodeToCode.Languages` fails until every enum value has a definition)
4. Give it a language id: add a case to `FN2CRichTextSyntaxHighlighter::Create()` and to `FN2CCodeEditorStyle::GetLanguageString()`
5. Register its styles with one `InitializeStylesForLanguage(<LanguageId>, *Colors)` call in `FN2CCodeEditorStyle::InitializeLanguageStyles()`; it uses the same `CodeEditorTheme` colors as every other language, so no theme work is needed
