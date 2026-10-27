# NodeToCode stdio bridge

`nodetocode_bridge.py` lets an MCP client that only starts local commands (Claude Desktop, other stdio clients) talk
to NodeToCode's MCP server in the open Unreal Editor. It uses the Python standard library only. `launch_bridge.bat`
and `launch_bridge.sh` start it with the Python of your newest Unreal Engine install (`ue_python_finder.py`), or a
Python 3 from your PATH. Setup for each client: [Connecting an MCP Client](../../../wiki/developers/Connecting-an-MCP-Client.md).

## MCPB bundle (one-click install in Claude Desktop)

`manifest.json` describes the bridge as an [MCP Bundle](https://github.com/modelcontextprotocol/mcpb) (manifest 0.3,
server type `python`). The bundle holds only `manifest.json` and `nodetocode_bridge.py` (`.mcpbignore` leaves the
rest out). Claude Desktop runs it with `python` (`python3` on macOS and Linux) and two optional settings:

| Setting | Passed as | When to set it |
|---|---|---|
| Unreal project | `--project` | Several editors run NodeToCode: the project's name, `.uproject` path or folder |
| NodeToCode MCP URL | `--url` | The editor is not found through its discovery file: `http://127.0.0.1:<Server Port>/mcp` |

Left empty, the bridge finds the one running editor itself. Build it into a folder outside the checkout (build output is
never committed: `*.mcpb` is gitignored and left out of plugin packages):

```
npx -y @anthropic-ai/mcpb@2.1.2 validate Content/Python/mcp_bridge/manifest.json
npx -y @anthropic-ai/mcpb@2.1.2 pack Content/Python/mcp_bridge <scratch>/nodetocode.mcpb
```

`manifest.json`'s `version` is the plugin's `VersionName` (`Tests/python/test_mcpb_bundle.py` checks it).

### Install check in Claude Desktop (manual)

This is the manual check for the owner's install card. It cannot run without a signed-in Claude Desktop:

1. Open the editor with NodeToCode (Enable MCP Server on), then double-click `nodetocode.mcpb`, or drag it onto
   **Settings > Extensions**. Claude Desktop shows the name, description and the two settings; install it.
2. In a new chat the tools menu lists NodeToCode's tools (64 by default). Ask for `get-available-translation-targets`;
   the answer lists the targets.
3. With two editors open, set **Unreal project** to one of them and check that the tools read that editor's Blueprint.
4. Close the editor: a tool call answers with an error that names the missing editor, not a crash of the extension.

## MCP Registry entry

`server.json` is the [MCP Registry](https://github.com/modelcontextprotocol/registry) entry
(`io.github.protospatial/nodetocode`, schema `2025-12-11`). It points at the bundle on the GitHub release and carries
the bundle's SHA-256. The registry is still a preview (API frozen at v0.1, v1 planned for general availability), so
the schema can change before publishing. To publish:

1. Pack the bundle from the release commit and attach it to the GitHub release as `nodetocode-<VersionName>.mcpb`.
2. Put its `sha256sum` in `packages[0].fileSha256`, and the release URL and version in `identifier` and `version`.
   The hash in the checkout is from a local pack and changes with every pack.
3. `mcp-publisher login github` (as a member of the protospatial organization), then `mcp-publisher publish`.
