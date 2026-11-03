Clients that support MCP Apps show two NodeToCode tool results as a small app inside the chat instead of raw JSON:
**translate-focused-blueprint** opens a translation viewer with syntax-highlighted code, and **get-focused-blueprint**
opens a graph preview that draws the focused graph's nodes and wires. Clients without MCP Apps show the same result as
text, as before. This page lists what each app shows and which clients render them.

## Which clients show the apps

MCP Apps is an optional MCP extension (spec 2026-01-26). Claude Desktop, VS Code (GitHub Copilot), Cursor and ChatGPT
render it; Claude Code and other terminal clients do not, and show the tool's text and structured result instead.
Nothing needs turning on in NodeToCode: each of the two tools names its app in the tool list, and a client that
supports MCP Apps fetches it and shows it next to the result.

## Translation viewer

`ui://nodetocode/translation-viewer`, for **translate-focused-blueprint**.

- One section per translated graph, with its name, graph type and Blueprint class.
- **Declaration** and **Implementation** tabs switch every section between the header part and the implementation.
- Keywords, types, strings, numbers, comments and preprocessor lines are colored; the colors follow the client's
  light or dark theme.
- The implementation notes are shown under each graph's code, and the result's warnings at the end.
- A translation too large for one result is saved to a file; the viewer then names the file.

## Graph preview

`ui://nodetocode/graph-preview`, for **get-focused-blueprint**.

- The Blueprint's name and class, then one panel per graph.
- Nodes are placed in columns by their place in the flow, left to right; execution wires are thick, data wires thin
  and colored. Each node shows its title and up to ten input and output pins, with default values on inputs. Hover a
  node for its type and member.

## Security

Both apps are self-contained HTML in the plugin's `Content/MCPApps` folder: they load no script, style or font from
anywhere, and the client runs them in a sandboxed frame with no network access. Graph and code text comes from the
Blueprint, so the apps insert it as plain text, never as markup.
