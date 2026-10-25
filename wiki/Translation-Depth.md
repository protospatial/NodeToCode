### Best Practices → Translation Depth
***

## Setting Appropriate Depth
The [Max Translation Depth setting](Max-Translation-Depth) affects how Node to Code translates nested user-created graphs like functions, macros, collapsed graphs, and math expressions into code:

Depth | Use Case | Considerations
-- | -- | --
0 | Single graph translation | Fastest, lowest cost
1 | Include direct function calls | Good for most cases
2 | Include nested functions | Balance of depth/cost
3+ | Deep system translation | Higher cost, longer processing

Each level counts one more call away from the graph you translate, along the shortest call path (see [Max Translation Depth](Max-Translation-Depth) for an example). Before NodeToCode 2.0, any depth above 1 captured the same graphs as depth 1.


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