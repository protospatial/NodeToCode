### Translation Workflow → Utilizing Saved Translations
***

## Automatic Translation Saving
Translations run from the editor are automatically saved locally to your project's Saved path (or the custom output directory in settings) when a translation successfully completes. A translation an MCP client requests with `translate-focused-blueprint` is not saved: the client receives it as the tool's result. This allows you to easily revisit previous translations or share translation files with other people, LLMs, and applications.

An MCP client can read saved translations with the `read-path` and `read-file` tools. Folders in the default location are given relative to the project, for example `Saved/NodeToCode/Translations`. A custom output directory outside the project is given by its absolute path, which `get-translation-output-directory` returns as `absolutePath`. The tools refuse a custom directory that is a drive root or that contains your project or your home folder. `read-file` returns at most 500 KB of a file per call and can continue from a line number.

### Translation File Structure Visualization

The following file structure is created in the user's project:

```
ProjectRoot/
└── Saved/
    └── NodeToCode/
        └── Translations/
            └── BlueprintName_YYYY-MM-DD-HH.MM.SS/  (Timestamp format)
                ├── N2C_BP_BlueprintName_YYYY-MM-DD-HH.MM.SS.json  (Pretty-printed Blueprint JSON)
                ├── N2C_BP_Minified_BlueprintName_YYYY-MM-DD-HH.MM.SS.json  (Minified Blueprint JSON)
                ├── N2C_Translation_BlueprintName_YYYY-MM-DD-HH.MM.SS.json  (LLM translation response)
                └── GraphName/                      (Directory for each graph)
                    ├── GraphName.h                 (C++ header - only for C++ target)
                    ├── GraphName.ext               (Implementation file with language-specific extension)
                    └── GraphName_Notes.txt         (Implementation notes)
```

### Key Details:

1. **Root Location**: Files are saved in the project's `Saved/NodeToCode/Translations` directory
2. **Folder Naming**: Each translation gets a timestamped folder (`BlueprintName_YYYY-MM-DD-HH.MM.SS`)
    - `BlueprintName` and the Blueprint JSON files are the Blueprint you translated, as captured when the request was sent: opening other graphs, compiling or copying another Blueprint's JSON while you wait for the reply does not change them
3. **Blueprint JSON files**: A **Prettified** version for human readability and a **Minified** version for sharing with other LLMs or applications
    - This is the efficient JSON representation of your Blueprint that's sent to the LLM for translation
    - Can be really helpful if using nested translation on a Blueprint system and you want to discuss the entire system with a peer or another LLM
    - Since 2.0 these files say `"version": "2.0.0"`. When one output pin feeds several inputs, its entry under `flows` > `data` lists all of them (`"N1.P2": ["N2.P1", "N3.P4"]`); 1.x files kept only one, so the LLM had to guess the other inputs. Each entry under `flows` > `execution` is one exec wire from the output pin to the input pin it runs (`"N3.P5->N4.P1"`), so a Branch's True and False, a Switch's cases or a Gate's Enter, Open and Close stay apart; 1.x files listed only node pairs (`"N3->N4"`). Nodes Blueprint never runs are marked, each flag present only when true: `"unused": true` on a pure node whose result nothing that runs reads, `"unreachable": true` on a node with exec pins that no event or entry reaches, and `"disabled": true` on a node disabled in the editor (exec runs past a disabled node with one exec input and one exec output). A new Blueprint's greyed-out default events that nothing is wired to are left out. Names do not depend on the editor language or on **Show Friendly Names**: each node has a `class`, functions and variables keep their declared names, and a pin's `name` is its internal name (`execute`, `then`, `ReturnValue`); the English title or pin label you see in the editor appears as `title` or `label` only when it says more than that name (1.x files had display text, in the editor's language, under `name`). Files saved by 1.2.x (`"version": "1.0.0"`) are left as they are and still read: from Python, `nodetocode.read_n2c_json(text)` reads either version. A file written by a later 2.x version (for example 2.1.0) is read without loss: node, pin, graph and Blueprint types and fields this version does not know are kept as they are written, and `read_n2c_json` lists each one in its `warnings`
4. **Translation Response**: The graphs and code parsed from the LLM's reply, saved as JSON with the input/output token usage
5. **Per-Graph Folders**: Each graph in the Blueprint gets its own subfolder
6. **Language-Specific Files**: The file extension for implementation files depends on the target language:
   - C++: `.cpp`
   - Python: `.py`
   - JavaScript: `.js`
   - C#: `.cs`
   - Swift: `.swift`
   - Pseudocode: `.md`

7. **Header Files**: Header files (`.h`) are only generated for C++ translations

### Tags and translation history file

Graph tags and the record of which graphs have translations are kept in `Saved/NodeToCode/BlueprintGraphState.json`.
Each save writes a temporary file first and keeps the previous version as `BlueprintGraphState.json.bak`, so a crash
during a save cannot leave the file half written. If the file cannot be read (for example after a hand edit with a
JSON mistake), NodeToCode renames it to `BlueprintGraphState.json.corrupt-<date-time>`, shows a notification naming
it, and stops saving tag changes until you restart the editor or reload the tags, so nothing overwrites it. To
recover, fix that file (or copy the `.bak`) to `BlueprintGraphState.json` and restart the editor. After an upgrade
from 1.x, the old `Tags/BlueprintTags.json` is migrated once and renamed to `BlueprintTags.json.migrated`.

Each record names the graph's folder (relative to the project, or absolute when a custom output directory is outside
it) and the target language, so the translation viewer reopens a saved translation in any of the six languages. Records
written by earlier 2.0 builds still open, including Pseudocode files those builds expected as `.txt`.

## Quick Access
You can quickly navigate to the path of your last translation by [clicking the folder button in the Translations window](Viewing-the-Results).

## Practical Examples for Utilizing Saved Translations

Once you have your translations saved locally, there are numerous ways to leverage these files in your development workflow. Here are some practical examples:

### 1. Remote Collaboration & Communication

**Example: Troubleshooting Complex Blueprint Logic**
```
// In Discord/Slack/Email
Hey Jane! I'm struggling with understanding why the grabbing logic isn't working in BP_TrackedHand.
I think I narrowed it down to the DetectInteractableObjects function.
Instead of sending screenshots of the Blueprint, I've attached the pseudocode version of function. Thoughts?

[Attach the GraphName.md file from your Pseudocode translation]

The issue seems to be around line 42 where the path calculation occurs.
```

**Example: Code Reviews Without Editor Access**
```
// In a PR comment or code review tool
I've implemented the inventory system in Blueprint but looking for feedback on the structure.
Here's the C++ equivalent for easier review:

[Paste the contents of GraphName.h and GraphName.cpp]

Specifically, I'm unsure about the efficiency of the item lookup method.
```

### 2. Documentation & Knowledge Sharing

**Example: Adding to Internal Wiki/Documentation**
```markdown
## Inventory System Technical Documentation

### Design Overview
[Paste system diagram or high-level description]

### Implementation Details
Our inventory system uses the following core algorithm (extracted from Blueprint):

[Paste relevant sections from the C++ translation inside a code block]


### Blueprint Flow Explanation
For a complete overview of the Blueprint logic flow in text form:

[Paste pseudocode translation here inside a code block]

```

**Example: Onboarding New Team Members**
```
// In onboarding docs
To understand our camera system without diving into complex Blueprint graphs, review these
translated versions that show the core implementation patterns we use:

- Blueprint System #1: [link to saved .cpp file]
- Blueprint System #2: [link to saved .cpp file] 
- Blueprint System #3: [link to saved .cpp file]
```

### 3. AI-Assisted Development

**Example: Getting Help from other LLMs**
```
I have a Blueprint system in Unreal Engine that I've translated to C++.
I'm seeing performance issues in this specific part:

[Paste relevant section from the C++ translation inside a code block]

How can I optimize this to reduce game thread overhead while maintaining the same functionality?
```

**Example: Expanding Blueprint Logic with LLMs**
```
Here's my current Blueprint logic translated to C++:

[Paste GraphName.cpp content]

I want to expand this to include [new feature description]. 
Can you suggest how I would modify this code to implement that functionality?
```

### 4. C++ Learning & Conversion

**Example: Studying UE C++ Patterns**

Create side-by-side comparisons of your Blueprint screenshots and the generated C++ code to build a personal reference library of patterns:

```
# My UE C++ Pattern Reference

## Event Binding Implementation
Blueprint: [Screenshot of Event Binding nodes]
C++ Implementation:

[Paste the relevant event binding code section from translation]

## Custom Trace/Raycast Pattern
Blueprint: [Screenshot of trace/raycast nodes]
C++ Implementation:

[Paste the relevant trace/raycast code from translation]

```

**Example: Incremental Blueprint to C++ Migration**
```cpp
// Start with Node to Code translation
[Paste GraphName.h and GraphName.cpp to your IDE]

// Then annotate and optimize as needed:
// TODO: Optimize this loop for better cache coherency
// TODO: Add error handling for edge cases
// FIXME: This allocation could be moved outside the tick function
```

### 5. Performance Optimization

**Example: Profiling & Bottleneck Identification**
```
// In a development note or performance ticket
Title: Critical Performance Investigation - AI Navigation System Bottleneck
Priority: High
Platform: Quest 3

Profiling Results:
- Game Thread spike identified during heavy combat (15+ AI agents)
- Unreal Insights shows BP_EnemyController→UpdateTacticalPosition consuming 3.8ms average
- Occurring every tick (90fps target = 11.11ms budget)
- Major hitching when multiple agents recalculate paths simultaneously

I've translated the Blueprint to C++ using N2C:

[Paste relevant C++ translation]

Recommended Action:
1. Convert this entire function to C++
2. Implement spatial partitioning to reduce cover point checks
3. Batch raycasts using AsyncLineTraceMultiByChannel
4. Cache squad matrix calculations and update less frequently
5. Move intensive calculations to async task graph
```

**Example: Communication with Performance Engineers**
```
We need to optimize this Blueprint function that's running on every tick:

[Paste pseudocode or C++ translation]

The main issue appears to be at lines 36-63, where we're:
Computing quaternion interpolation (Slerp) between each bone's orientation

Each frame we're doing these calculations for every skeletal mesh component in view (currently ~50 characters).
The Blueprint VM overhead for matrix/quaternion math is significant here.
```

### 6. Integration with Other Tools

**Example: Version Control**
```bash
# Include translations in version control to track Blueprint evolution over time
git add ProjectRoot/Saved/NodeToCode/Translations/MyImportantSystem_*/*.{h,cpp,md}
git commit -m "Add Blueprint translations for reference"
```

**Example: Blueprint Diffing**
```
# To compare Blueprint changes over time:
diff ProjectRoot/Saved/NodeToCode/Translations/MySystem_2025-03-01/CalculatePath.cpp \
     ProjectRoot/Saved/NodeToCode/Translations/MySystem_2025-04-01/CalculatePath.cpp
```