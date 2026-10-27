### User Interface → Code Editor
***

The code editor is the central component of the Node to Code Editor window where you'll spend most of your time reviewing and working with translated code. It provides a baseline environment for examining and modifying generated code before integration into your project.

## Features

### Syntax Highlighting
The editor highlights code in the language the translation was written in (C++, Python, JavaScript, C#, Swift or Pseudocode), and keeps that language when you change the font size or theme:

- Keywords
- Operators
- Strings
- Numbers
- Comments
- Preprocessor directives
- Brackets and braces

<p align="left">
    <img src="https://github.com/protospatial/NodeToCode/blob/main/assets/Image_NodeToCode_CodeEditor.png" alt="Node to Code header" width="500">
</p>

### The Code Editor widget in Editor Utility Widgets
The **Code Editor** UMG widget that 1.x Editor Utility Widgets embed still loads, but it is unsupported and will be
removed. Its **On Text Changed**, **On Cursor Moved** and **On Selection Changed** events fire when you type, move
the cursor or change the selection (selection events give the first and last line). Text you type is kept when the
widget's properties are synchronized.
