# Verse as a translation target (card 0328, exploration)

Should NodeToCode translate Blueprints to Verse? This page records what Verse is and where it runs as of 2026-11-02,
how N2C JSON maps onto it, what a prototype prompt produced on three fixture graphs, what a target would cost in this
codebase, and a recommendation. It changes no product code. Review items: later-11, rec-agentic-landscape-11.

**Recommendation: wait.** Keep the draft prompt here and add no target before UE6 Early Access (Epic: late 2027).
The trigger to revisit is listed under [Recommendation](#recommendation).

## Where Verse runs today

| Question | Answer (2026-11-02) | Source |
|---|---|---|
| Can a UE 5.6, 5.7 or 5.8 project contain or run Verse? | No supported workflow. The engines ship the Verse compiler as a module (`Engine/Source/Runtime/VerseCompiler`, `Binaries/Win64/UnrealEditor-VerseCompiler.dll`), and the plugin descriptor has a `CanContainVerse` field (`Runtime/Projects/Public/PluginDescriptor.h:130` on 5.8, `:123` on 5.6). In UE 5.8 only two disabled test fixture plugins use the field (`Engine/Plugins/Tests/VerseModuleIndependenceA` and `B`, each with one `.verse` file); 5.6 and 5.7 have none. Launcher builds compile with `bUseVerseBPVM = true` (`UnrealBuildTool/Configuration/Rules/TargetRules.cs:957` on 5.8, `Configuration/TargetRules.cs:918` on 5.6), which defines `WITH_VERSE_VM=0` (`Rules/RulesAssembly.cs:777-779`). | Engine sources, checked on this PC |
| Where can people write Verse? | Unreal Editor for Fortnite (UEFN) only, against Fortnite's API (`/Fortnite.com/...` modules, creative devices). UEFN offers Verse, not Blueprint scripting, so a Blueprint to translate comes from a UE5 project, and its Verse can only run in UEFN after its engine calls are replaced. | The coverage below; Epic's Verse style guide for UEFN ([link](https://dev.epicgames.com/documentation/fortnite/verse-code-style-guide-in-unreal-editor-for-fortnite)) |
| What did Epic announce? | At State of Unreal on 2026-06-17, Epic said UE6 merges UE5 and UEFN into one engine, with Verse as the main gameplay language and Scene Graph as the component framework. Actors and Blueprints stay in early UE6 versions and are deprecated later, "when the new framework is sufficiently mature". Epic promised "conversion tools to move projects from one framework to the other". | Game Developer, 2026-06-17 ([link](https://www.gamedeveloper.com/programming/unreal-engine-6-will-merge-ue5-and-uefn-into-a-single-unified-engine-)); Febucci, 2026-07-08, quoting Epic's "The road to UE 6" post ([link](https://blog.febucci.com/2026/07/the-epic-choice-of-removing-blueprints-in-unreal-engine-6-and-more-doubts/)); Epic forum thread, 2026-06-17 ([link](https://forums.unrealengine.com/t/state-of-unreal-2026-top-news-from-the-show/2729341)) |
| When? | UE6 Early Access is targeted for the end of 2027, with the full release 12 to 18 months later. No Verse or Scene Graph preview for UE 5.x was announced. | Same coverage; UnrealSource summary ([link](https://unrealsource.com/d/the-road-to-unreal-engine-6/)) |
| What will the conversion tools do? | Not published: no format, scope or date. | Searched 2026-11-02; none found |

Epic's own post (`unrealengine.com/news/the-road-to-ue-6`) refused automated fetches (HTTP 403), and the Verse
reference on dev.epicgames.com renders only with JavaScript, so the quotes above come from coverage that quotes Epic.
The language facts below are checked against the Verse compiler that ships with UE 5.8 instead.

## N2C JSON concepts in Verse

Verse keywords and effect specifiers cited from the UE 5.8 compiler:
`VerseCompiler/Public/uLang/Parser/ReservedSymbols.inl` (reserved words, by line),
`VerseCompiler/Private/uLang/Semantics/SemanticProgram.cpp:966-975` (effect attributes `suspends`, `decides`,
`computes`, `converges`, `transacts`, `reads`, `writes`, `allocates`), `:857` (`concrete`), `:926` (`override`),
`VerseCompiler/Private/uLang/Parser/ParserPass.cpp:255` (`if`) and `:1594` (`var`). Library calls (`Print`, `Sleep`,
`GetSimulationElapsedTime`, `Subscribe`) are UEFN API, not compiler symbols, and could not be checked here.

| N2C JSON | Verse | Fit |
|---|---|---|
| `metadata.blueprint_class` with its graphs | One `class` (`ReservedSymbols.inl:31`); a placed actor becomes `class(creative_device)` in UEFN. UE6's Scene Graph entity and component API is unpublished | Partial: the base class is a guess until UE6 |
| `Function` graph, `FunctionEntry` / `FunctionResult` pins | A method `Name(Param:type):result =`; several outputs become a tuple (`tuple`, `:132`) | Good |
| `EventGraph`, `Event`, `CustomEvent` | One method per event. BeginPlay becomes `OnBegin<override>()<suspends>:void` on a creative device (`override`, `SemanticProgram.cpp:926`). Input, overlap and component events become `Subscribe` calls on UEFN device events | Partial: depends on the UEFN or UE6 event API |
| Member variables (`VariableGet`, `VariableSet`), Instance Editable | `var Name:type = default` and `set Name = value` (`ParserPass.cpp:1594`, `set` at `:111`); `@editable` for exposed members | Good |
| Local variables (`LocalVariableGet` / `Set`) | A `var` local in the method | Good |
| `Branch` | `if (Condition?):` ... `else:` (`if` at `ParserPass.cpp:255`). Verse conditions are failure contexts, not booleans; a logic value needs `?` | Good, but an easy source of wrong code |
| `Sequence` | Statements in order | Good |
| `ForLoop`, `ForEachLoop`, `WhileLoop` (macros) | `for (I := A..B):`, `for (X : Array):` (`for`, `:59`), `loop:` with `break` (`:83`, `:24`) | Good |
| `SwitchInt`, `SwitchEnum`, `SwitchString` | `case (Value):` with `_ =>` for Default (`case`, `:25`) | Good |
| `Gate`, `DoOnce`, `FlipFlop` (macros with state) | A `var` logic member checked and set | Good, as in the other targets |
| `latent` nodes (`Delay`, `Timeline`, async actions) | The method gets `<suspends>` (`SemanticProgram.cpp:966`); Delay is `Sleep(t)`; parallel work is `spawn`, `race`, `sync`, `branch` (`:112`, `:103`, `:123`, `:23`). A Timeline has no counterpart: a `loop` with `Sleep(0.0)` per frame | Better than C++: Verse has structured concurrency built in |
| `Macro` and `Composite` graphs, `Tunnel` | A helper method, or inlined. Verse has no macros | Good |
| `Construction` script | None: logic moves to `OnBegin` | Behaviour changes |
| `Cast`, object references | An option `?t` (`option`, `:95`) tested with `if (X := Maybe?):`; casts are failable `type[]` calls | Partial |
| Types | bool to `logic` (`:82`), int and int64 to `int` (`:67`), float and double to `float` (`:53`), string, name and text to `string` (`:113`), arrays `[]t`, maps `[k]v` (`map`, `:84`), user structs to `struct` (`:117`), enums to `enum` (`:46`). Int to float has no implicit conversion; float to int is failable (`Floor[]`, `Round[]`, `Int[]`) | Good, with care at numeric conversions |
| Engine calls (`KismetSystemLibrary`, `Actor`, components) | No equivalent until UE6 publishes Verse APIs for them; UEFN has a Fortnite-specific subset | Poor: most calls become placeholders |

## Prototype

The draft prompt is [verse-target/CodeGen_Verse.draft.md](verse-target/CodeGen_Verse.draft.md). It has the same
structure as `Content/Prompting/CodeGen_Python.md`: the shared `{{N2C_JSON_SPEC}}` and `{{N2C_RESPONSE_FORMAT}}`
fragments, and Verse-specific steps for the class, graphs, nodes, flow and types. It was run on three capture goldens
from `Source/Private/Tests/Goldens/Capture/`:
- `GateMultiExec`: custom events driving a Gate macro.
- `TimelineMultiExec`: custom events driving a latent Timeline.
- `SpawnedVariableNodes`: a function with member and local variable gets and sets.

The run assembled the prompt as `UN2CSystemPromptManager::AssemblePrompt` and `WrapN2CJson` do
(`Source/Private/LLM/N2CSystemPromptManager.cpp:208-231`) and called `claude-sonnet-5-5` on 2026-11-02 through the
Messages API. Each call took about 10,200 input tokens (the shared JSON specification is most of it) and 900 to 3,100
output tokens, in 7 to 20 s. All three responses were valid N2C response JSON. Outputs:
[GateMultiExec](verse-target/outputs/GateMultiExec.claude-sonnet-5-5.verse),
[TimelineMultiExec](verse-target/outputs/TimelineMultiExec.claude-sonnet-5-5.verse),
[SpawnedVariableNodes](verse-target/outputs/SpawnedVariableNodes.claude-sonnet-5-5.verse) (the code and notes of
each entry; the raw responses with token usage are the `.json` files next to them).

Problems found by reading the outputs (no Verse compiler or UEFN was available to compile them):

| Fixture | What came out | Problems |
|---|---|---|
| GateMultiExec | A plain `class` with a `var IsGateClosed:logic = true` member and one method per custom event; the Gate is inlined correctly (Enter runs Exit only while open, Open and Close set the flag); CollectGarbage is an empty placeholder | `if (IsGateClosed = false):` compares a logic value with `=` where the idiom is `if (not IsGateClosed?):`; a method body that is only `return` is unidiomatic. The events have no callers, as in the fixture, so nothing would run in UEFN |
| TimelineMultiExec | A `<suspends>` RunTimeline loop advancing a position by elapsed simulation time each frame (`Sleep(0.0)`), started with `spawn{}` from the Play and Reverse events; Update and Finished outputs become methods; two placeholders | The most useful output: it rebuilt Timeline semantics (direction, finish at either end) that the C++ and Python prompts usually leave as a comment. Unverified: whether `spawn` is allowed in a method with default effects (spawn starts work that a transaction cannot roll back), and `GetSimulationElapsedTime` comes from UEFN's `/Verse.org/Simulation` |
| SpawnedVariableNodes | `var Score:int = 0` member, local `var Counter:int = 0`, two `set` statements in the right order | Adds three `using` lines it does not need (Devices, Simulation, Diagnostics) to a class that uses none of them; the note's claim that methods default to `<transacts>` is the model's, not checked |

Common to all three: the model followed the draft's rules (snake_case class names, plain `class` when there is no
BeginPlay, `set` for every write, placeholders for engine calls, notes that list them). It could not know which
engine calls UE6 will expose, so any real graph is mostly placeholders. Correctness needs a compiler loop. The
`UnrealEditor-VerseCompiler` module ships with 5.6 to 5.8, so a later spike could drive it from a commandlet against
Verse's core modules. That would check syntax and effects, but not the Fortnite or UE6 API.

## What a Verse target would cost here

The target-language table makes the core change small (`Source/Public/Core/N2CTargetLanguage.h:9-14` lists what a
language needs):

| Piece | Where | Size |
|---|---|---|
| Enum value, appended last so saved `TargetLanguage` values (`UN2CSettings`, `Source/Public/Core/N2CSettings.h:460`, a `uint8` enum) keep their meaning | `Source/Public/Code Editor/Models/N2CCodeLanguage.h:9-17` | 1 line |
| Table row (id `Verse`, extension `.verse`, no declaration file, prompt key `Verse`) | `Source/Private/Core/N2CTargetLanguage.cpp:11-37` | 5 lines |
| A new prompt, CodeGen_Verse.md in `Content/Prompting/` | from the draft here | ~70 lines |
| Syntax definition and its switch cases | new `N2CVerseSyntaxDefinition.h/.cpp` (Swift's is 20 + 55 lines), `Source/Private/Code Editor/Syntax/N2CSyntaxDefinitionFactory.cpp:31`, `N2CRichTextSyntaxHighlighter.cpp:31`, `Source/Private/Code Editor/Models/N2CCodeEditorStyle.cpp:93` and `:230`, `Source/Public/Code Editor/Widgets/SN2CCodeEditor.h:205` | ~100 lines |
| Specs that list every language | `NodeToCode.Languages` (`Source/Private/Tests/Core/N2CTargetLanguage.spec.cpp:48`) checks that the table and the code editor cover every enum value; also `Source/Private/Tests/LLM/N2CPromptAssembly.spec.cpp:56`, `N2CTranslationLoad.spec.cpp`, `Source/Private/Tests/Core/N2CGraphState.spec.cpp`, `N2CTranslationViewer.spec.cpp` | a row each |
| User docs and generated reference | `wiki/Target-Language-Settings.md`, `wiki/Code-Editor.md`, `wiki/Theme-&-Syntax-Highlighting.md`, `Context/mcp-server/tools.generated.*` (the targets tool reads the table) | small |

About one day of work. The cost that matters is quality: a Verse target that compiles needs a validation loop (a
compiler, then UEFN or UE6) and a prompt that tracks an API Epic has not published. Without that loop, users would
get confident Verse that does not compile, which is worse than no target.

## Recommendation

**Wait for UE6 Early Access.** Do not add the enum value or ship the prompt now:
- No UE 5.6 to 5.8 project can run Verse. UEFN users have no Blueprints to translate. The Scene Graph API that
  Blueprint logic would map onto is unpublished.
- The prototype shows that the mapping itself is not the hard part. The draft prompt already handles events,
  variables, flow, macros and latent nodes. What the codebase cannot provide yet is a target API and a compiler to
  check against.
- Epic has promised its own conversion tools. NodeToCode's value next to them is unknown until their scope is public.

Revisit when the first of these happens:
1. UE6 Early Access, or any UE 5.x preview, lets a project contain Verse (`CanContainVerse` plugins outside Epic's
   tests, or a documented Verse workflow).
2. Epic publishes the Scene Graph Verse API, or the scope of its Blueprint conversion tools.
3. A user or the owner asks for Blueprint-to-UEFN porting help. The draft prompt can then ship as a reference-only
   target (UEFN API, placeholders noted) at the cost in the table above.

Owner question, not decided here: rec-agentic-landscape-11 also suggests deprioritizing the Swift, JavaScript and C#
prompts. This exploration has no usage data to support or reject that.
