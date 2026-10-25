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