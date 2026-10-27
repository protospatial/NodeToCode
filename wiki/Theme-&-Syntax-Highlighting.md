### Plugin Settings → Theme & Syntax Highlighting
***

## Theme Management

Each supported language has its own theme collection:

- **C++ Themes**
- **Python Themes**
- **JavaScript Themes**
- **C# Themes**
- **Swift Themes**

## Color Configuration

Customize syntax highlighting for [target language](Target-Language-Settings):

- **Background**: Editor background color
- **NormalText**: Default text color
- **Keywords**: Language keywords
- **Operators**: Mathematical and logical operators
- **Strings**: String literals
- **Numbers**: Numeric values
- **Comments**: Single and multi-line comments
- **Preprocessor**: Preprocessor directives
- **Parentheses**: Round brackets
- **CurlyBraces**: Curly braces
- **SquareBrackets**: Square brackets

You can change these colors while a translation is open. The **Background** color of an open code editor updates right away; syntax colors apply the next time the code view is rebuilt (for example when you switch language or reopen the translation).

    <p align="left">
        <img src="https://github.com/protospatial/NodeToCode/blob/main/assets/Image_NodeToCode_ThemeSettings.png" alt="Node to Code theme settings" width="500">
    </p>


## How code is colored

- A string ends at the quote that opened it, so `'"'` or `"it's"` stays one string, and a quote after an odd number of backslashes (`\"`) does not end it.
- An unclosed string colors only the rest of its line. Strings that may span lines carry on to the next line: Python and Swift triple quotes (Python docstrings are colored as **Strings**), JavaScript template literals (`` ` ``), C++ raw strings (`R"(...)"`) and C# verbatim strings (`@"..."`).
- Keywords match case-sensitively: `If` is not the keyword `if`.
- A line longer than 2,000 characters is shown in **NormalText**, without highlighting.
- The translation window's **JSON** tab is highlighted as JSON with the **JavaScript** colors.


## [Built-in themes](Initial-Configuration#theme-customization-optional)

Several pre-configured themes available in [Plugin Settings](Initial-Configuration):

- **Unreal Engine** (Default): Professional dark theme matching UE5 editor colors
- **Spacedust**: Dark teal background with vibrant orange and blue accents
- **Ubuntu**: Deep purple background with Ubuntu-style greens and reds
- **Renaissance**: Dark charcoal with warm earth tones and coral accents
- **Midnight Code**: Modern dark theme inspired by popular IDEs
- **Midnight Neon**: Dark theme with bright neon accent colors
- **Mono Amber**: Monochromatic theme using amber color variations
- **Beige Earth**: Light theme with natural earth tones
- **Beige Earth Dark**: Dark version of earth tones theme
- **Citrus Delight**: Dark theme with energizing orange and yellow accents
- **Cuppa Joe**: Coffee-inspired browns and caramel tones
- **Cyber Night**: Cyberpunk-inspired with neon blues and purples
- **Forbidden Forest**: Mystical dark theme with forest-inspired colors
- **Night Sky**: Celestial theme with stars and nebula-inspired colors
- **Midnight Byte**: Cyberpunk dark theme with electric blues
- **Pixel Phosphor**: Retro terminal-inspired with phosphor greens
- **Night Shift**: Warm dark theme with no cool tones for reduced eye strain
- **Stealth**: High-contrast monochrome dark theme
- **Mono White**: Clean, professional light theme in grayscale
- **Studio Blue**: Classic Visual Studio-inspired light theme
- **Crisp**: Professional light theme with muted, balanced colors

    <p align="center">
        <img src="https://github.com/protospatial/NodeToCode/blob/main/assets/Image_NodeToCode_Theming.jpg" alt="Node to Code themes" width="1250">
    </p>

