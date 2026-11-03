Node to Code's colors are two settings: **UI Colors** for the window and the N2C bar, your own setting in **Editor
Preferences → Plugins → Node to Code (User) → Theming**, and **Code Editor Theme** for the syntax colors of the
translation viewer, a project setting in **Project Settings → Plugins → Node to Code → Theming**. There is one
code theme for every language; the theme collections and the theme and font pickers of 1.x are gone.

## UI Colors

**UI Colors** holds the colors of the Node to Code window and of the N2C bar on each graph: backgrounds, buttons,
borders, text and accents. It is saved in your user settings, not in the project, so each developer keeps their
own. A change shows at once in an open window. Each entry has a reset arrow that returns it to
the default.

The default **Text Muted** is a light gray (148, 148, 148), so muted text keeps a 4.5:1 contrast on the panels.
Earlier 2.0 builds kept UI Colors in the project's settings; the first start with this version copies them to your
own settings once, and a saved **Text Muted** still at the older, darker default (107, 107, 107) takes the new
default then. Every other saved color is kept.

## Code Editor Theme

**Code Editor Theme** has one theme, **Unreal Engine**, used for every [target language](Target-Language-Settings)
and for the **JSON** tab. Its colors:

- **Background**: the editor background
- **Normal Text**: text that is none of the below
- **Keywords**, **Operators**, **Strings**, **Numbers**, **Comments**, **Preprocessor**
- **Parentheses**, **Curly Braces**, **Square Brackets**

You can change these colors while a translation is open. The **Background** of an open viewer updates right away;
the syntax colors apply the next time the code view is rebuilt (for example when you open another translation).
A project that kept 1.x theme settings (per-language themes such as `CPPThemes`) loads, but 2.0 does not read
them: set the colors here again.

## How code is colored

- A string ends at the quote that opened it, so `'"'` or `"it's"` stays one string, and a quote after an odd number of backslashes (`\"`) does not end it.
- An unclosed string colors only the rest of its line. Strings that may span lines carry on to the next line: Python and Swift triple quotes (Python docstrings are colored as **Strings**), JavaScript template literals (`` ` ``), C++ raw strings (`R"(...)"`) and C# verbatim strings (`@"..."`).
- Keywords match case-sensitively: `If` is not the keyword `if`.
- A line longer than 2,000 characters is shown in **Normal Text**, without highlighting.
- The **JSON** tab is highlighted as JSON with the same colors.
