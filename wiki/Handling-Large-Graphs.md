### Best Practices → Handling Large Graphs
***

## Breaking Down Complex Blueprints

1. **Identify Natural Boundaries**
    - Logical functional groups
    - Independent operations
    - Reusable sequences
    - Data processing stages

2. **Refactoring Strategies**
    - Collapse distinctive functionality into dedicated functions where possible
    - Collapse repetitive execution flow patterns into macros
    - Collapse other functionality into a composite (collapsed) graph that may not work in a function or macro (eg. delays, custom events, etc.)

<p align="center">
    <img src="https://github.com/protospatial/NodeToCode/blob/main/assets/Image_NodeToCode_Consolidate.png" alt="Node to Code header" width="1250">
</p>

> [!NOTE]
> The greyed-out default events of a new Blueprint (BeginPlay, ActorBeginOverlap and Tick with nothing wired to
> them) are not sent to the LLM, so they cost no tokens. Disabled, unused and unreachable nodes are still sent,
> marked so the LLM skips them; delete them from the graph to save their tokens.

The context window bar in the Node to Code window estimates a graph's size and cost for the selected **Model**: its
context size and prices, or your entry under [Model Price Overrides](Provider-Configuration#model-price-overrides).
The cost counts everything a request sends (the graph, the system prompt for the target language and your reference
files) at the input price, plus the expected reply (about the graph's size) at the output price; the batch total
shows the two parts. The context bars count the prompt with the graph, so a graph that only fits without it is not
shown as safe. For a **Custom Model ID** it shows the ID, assumes a 128K context and costs nothing unless you add a
price override.

The ring in a graph editor's overlay and the token counts in the Node to Code list estimate exactly what Translate
sends: the graph and its nested graphs up to the [Translation Depth](Translation-Depth), each counted once, at about
4 characters per token. The estimate follows your edits about a second after you stop, compiled or not, and a
Translation Depth change. Compiling one Blueprint re-estimates only that Blueprint's graphs. A graph with nothing to
translate shows **n/a**.

The provider and model you pick beside the bar are saved to the project's settings (`Config/DefaultNodeToCode.ini`),
the same place Project Settings saves them, so whichever you changed last is used after a restart.

## Translation Approaches

1. **Incremental Translation**

    - Translate core functionality first
    - Add supporting systems gradually
    - Test each component individually
    - Integrate pieces systematically

2. **Partial Translation**
    
    - Identify performance-critical paths
    - Translate compute-heavy sections
    - Keep simple logic in Blueprints
    - Balance maintenance needs

> [!NOTE]
> Not everything needs to be in C++. It's likely fine to
> keep other less intensive systems in Blueprints if they don't
> benefit from translation. Unreal Insights is your friend!