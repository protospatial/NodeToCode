### Translation Workflow → Viewing the Results
***

## The Node to Code Editor Window

After translation completes, you'll see the code appear within the Node to Code Editor window.

1. [**Code Editor**](Code-Editor)
    - Displays the translated code with basic syntax highlighting
    - Supports scrolling for longer translations
    - Basic text editing capabilities
    - **(1a)**: Copy the current text in the code editor to the system clipboard
2. [**Switching Between Implementation & Declaration**](Top-Navbar-Overview#b-implementation--declaration-tabs-center)
    - **(2a)**: Implementation tab, labelled with the translation's file extension: **.cpp**, **.py**, **.js**, **.cs**, **.swift** or **.md** (Pseudocode)
    - **(2b)**: Declaration tab (**.h**), shown only when the translation has a declaration, which in practice means C++
    - **JSON** tab: the Blueprint graph the translation was made from
    - The viewer opens on the implementation tab and highlights it in the translation's language. A translation you open from the graph list uses the language it was saved in; the result of a single translate uses the **Target Language** from the plugin settings
3. [**Translations Browser**](Top-Navbar-Overview#a-translations-browser-left)
    - **(3a)**: Browse and open other nested graphs included in your translation
    - **(3b)**: View what the estimated translation cost was in USD 
    - **(3c)**: Browse the local path of your [latest Blueprint translation files](Utilizing-Saved-Translations) 
4. [**Implementation Notes**](Implementation-Notes)
    - Additional context from the LLM
    - Usage suggestions
    - Integration tips
    - Potential optimization notes
    - **(4a)**: Copy the current text in the implementation notes to the system clipboard
5. [**Editor Styling**](Top-Navbar-Overview#d-styling-button-far-right)
    - Adjust the code editor font size
    - Change the current theme

<p align="left">
    <img src="https://github.com/protospatial/NodeToCode/blob/main/assets/Image_NodeToCode_UICallouts.png" alt="Node to Code header" width="1020">
</p>


## Reviewing the Translation

When examining the results:

1. Check that the overall structure matches your Blueprint logic
2. Review any warnings or notes in the [Implementation Notes panel](Implementation-Notes)
3. Verify that critical functionality is properly translated
4. Look for any TODO comments or areas needing attention

## Handling Warnings or Errors

If issues occur during translation:

1. Check the [Output Log](Logging-Configuration) for detailed error messages
2. Verify your LLM provider connection is stable
3. Ensure your Blueprint graph isn't [too complex](Handling-Large-Graphs) for the [selected model](Choosing-an-LLM-Provider)
4. Consider reducing [translation depth](Translation-Depth) if dealing with nested graphs
5. Review any error-specific suggestions in the [Implementation Notes](Implementation-Notes)
6. Take a look at the [Troubleshooting](Translation-Fails-or-Hangs) section