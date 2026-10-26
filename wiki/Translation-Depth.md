### Best Practices → Translation Depth
***

## Setting Appropriate Depth
The [Max Translation Depth setting](Max-Translation-Depth) affects how Node to Code translates nested user-created graphs like functions and macros into code. Collapsed graphs are always translated with the graph that holds them, whatever the depth, and math expressions are translated from the node itself:

Depth | Use Case | Considerations
-- | -- | --
0 | Single graph translation | Fastest, lowest cost
1 | Include direct function calls | Good for most cases
2 | Include nested functions | Balance of depth/cost
3+ | Deep system translation | Higher cost, longer processing

Each level counts one more call away from the graph you translate, along the shortest call path (see [Max Translation Depth](Max-Translation-Depth) for an example). Before NodeToCode 2.0, any depth above 1 captured the same graphs as depth 1.

Each graph is translated once, however many times it is reached: a function that calls itself, or one that several of your graphs call, appears once. A function from another Blueprint is marked with that Blueprint's name, so two functions with the same name in different Blueprints are both translated (before NodeToCode 2.0 one of them could be left out, and a function that called itself was sent twice).


## Depth Configuration Guidelines

1. **Start Conservative**
   - Begin with depth 1 for most translations
   - Evaluate results before increasing depth
   - Consider token usage and costs

2. **When to Increase Depth**
   - When you know there are multiple nested graphs within your top-level graph that you will need to translate anyways and are comfortable with the increase in compute time

> [!IMPORTANT]
> Higher translation depths can exponentially
> increase token usage and LLM processing time and
> are more prone to LLM context window issues.

3. **When to Decrease Depth**

   - Token limit concerns
   - Quick reference translations
   - Simple graph conversion
   - Cost optimization needed