### Translation Workflow → Accessing and Triggering Translation
***

When you open any Blueprint in the Unreal Editor, you'll find the Node to Code button in the toolbar that also contains the familiar Save and Compile buttons. To use it simply:

1. Look for the "Node to Code" button in the Blueprint Editor's main toolbar
2. Click the button to see a dropdown of the available actions

    <p align="left">
        <img src="https://github.com/protospatial/NodeToCode/blob/main/assets/Image_NodeToCode_ButtonHover.png" alt="Node to Code header" width="400">
    </p>

## Prerequisites for Translation

Before the toolbar actions become available, ensure:

- You have an active Blueprint editor window open
- The Blueprint editor is focused on a graph (Event Graph, Function, etc.)
- The graph contains at least one node
- Your LLM provider is properly configured in the plugin settings

> [!NOTE]
> If the toolbar button appears grayed out, make sure that you have a graph focused.

## Which graphs can be translated

NodeToCode reads Blueprint graphs: event graphs, functions, macros, collapsed graphs and interface functions a
Blueprint implements, wherever they are nested. You can tag, copy as JSON and translate any of them, from the graph
overlay or from the NodeToCode window.

Graphs that are not Blueprint graphs, such as an Animation Blueprint's state machines, cannot be translated. On such a
graph the overlay's Copy JSON, Translate and tag buttons are grayed out, and their tooltip says why.

If a tagged graph cannot be found any more (its Blueprint was moved or deleted, or the graph was removed), translating,
exporting or double-clicking its row shows a notification naming the graph and its Blueprint. Remove the tag and tag
the graph again.

## Starting a Translation

Once you've located the toolbar:

1. Click the "Translate Blueprint Graph to Code" button

    <p align="left">
        <img src="https://github.com/protospatial/NodeToCode/blob/main/assets/Image_NodeToCode_ButtonWithTip.png" alt="Node to Code header" width="400">
    </p>

2. The plugin will:
    1. Open/Focus the Node to Code Editor window if it's not already open or within view
    2. Immediately begin processing your graph
    3. Display a status overlay within the Editor window while the request is processed

    <p align="left">
        <img src="https://github.com/protospatial/NodeToCode/blob/main/assets/Image_NodeToCode_ProcessingOverlay.png" alt="Node to Code header" width="400">
    </p>

## Behind the Scenes

When you trigger a translation, Node to Code [performs several steps](What-is-Node-to-Code#how-it-works).

> [!NOTE]
> Translation time can vary based on:
> - Graph complexity
> - LLM provider response time
> - Network conditions (for cloud-based LLMs)
> - Number of reference files included