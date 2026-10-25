### Plugin Settings → Max Translation Depth
***

## Max Translation Depth

Control how deep the translator traverses nested graphs:

- **0**: Current graph only
- **1**: Include direct function calls
- **2**: Include nested functions
- **3-5**: Deep system translation

The depth counts calls away from the graph you translate. Only your own graphs (functions, collapsed graphs and macros
in your project's content) are followed; engine functions are never added. If function A calls B, B calls C and C
calls D, translating A sends:

| Depth | Graphs sent |
|---|---|
| 0 (the default) | A |
| 1 | A, B |
| 2 | A, B, C |
| 3 | A, B, C, D |

A graph reached along several paths counts at its shortest one.

> [!NOTE]
> Higher depths increase token usage and processing time

## More Information

Check out [Translation Depth](Translation-Depth) for more information on how this setting affects your translations and LLM usage.