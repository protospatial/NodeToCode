NodeToCode translates with an API key from each provider; it cannot sign in with a Claude Pro/Max, ChatGPT or
Google AI subscription. To use a subscription, run the vendor's own coding agent (Claude Code, Antigravity CLI or
Codex CLI), signed in with that subscription, and connect it to NodeToCode's MCP server. The agent reads your
Blueprints through NodeToCode's tools and writes the code itself, so NodeToCode makes no LLM request and needs no key.

The MCP server listens at **`http://127.0.0.1:27000/mcp`** while the editor is open; on another port, edit the URLs.

## API keys

To translate inside the editor, set the **API Key** for your provider in **Project Settings → Plugins → Node to
Code** (see [Provider Configuration](Provider-Configuration)). On Windows keys are saved in Windows Credential
Manager, outside every project (see [where API keys are stored](Initial-Configuration#where-api-keys-are-stored)), so
they are not in anything you commit, and no MCP tool, Blueprint or Python call returns them.

| Provider | Get a key at | Notes |
|---|---|---|
| Anthropic | [platform.claude.com](https://platform.claude.com/) (Claude Console) | Billed to the Console account, not to a Claude subscription |
| OpenAI | [platform.openai.com](https://platform.openai.com/) | Billed separately from ChatGPT plans |
| Gemini | [aistudio.google.com](https://aistudio.google.com/) | A Gemini API key, not a Google account sign-in |
| DeepSeek | [platform.deepseek.com](https://platform.deepseek.com/) | |
| Ollama, LM Studio | none | Run on your machine |

## Why subscription sign-in was removed

Development builds of NodeToCode 2.0 could sign in with a Claude Pro/Max subscription or a Google account. Anthropic
does not allow third-party apps to offer Claude.ai login or to send requests with subscription credentials (see
[Authentication and credential use](https://code.claude.com/docs/en/legal-and-compliance#authentication-and-credential-use)),
and the Google sign-in reused another application's credentials. Both were removed. If your project had **Claude
Pro/Max** or **Google Account** selected:

- The setting is ignored and the provider uses its **API Key**. The Output Log says so once per editor session, and
  the old entry is dropped the next time you save **Project Settings → Plugins → Node to Code**.
- Saved sign-in tokens are deleted from `secrets.json` the first time the editor loads it. API keys are kept (on
  Windows they move to Credential Manager).
- A translation with no API key set stops before anything is sent, and the error names the key to set.

## Before you connect an agent

- Open your project in the Unreal editor. The MCP server runs only while the editor is open.
- **Enable MCP Server** (**Project Settings → Plugins → Node to Code (User) → MCP Server**) is on by default and
  per user: a project's config files cannot change it. Changing it needs an editor restart.
- **Server Port** (**Project Settings → Plugins → Node to Code → MCP Server**) is 27000; a change applies at once.
- The server answers only on this machine (`localhost`, `127.0.0.1`), so run the agent on the same computer.
- Blueprint text is data, not instructions. Node comments, tooltips, string literals and names can come from a Fab
  asset, a third-party pack or a teammate, and someone can write instructions aimed at your agent into them.
  NodeToCode marks that text as untrusted when it hands it to the agent (an `untrusted_content` notice at the top of
  each result, and a notice before the Blueprint in its prompts), but the agent can still be fooled: review what it
  wants to do before you allow a tool call that writes, deletes or runs code, and keep **Allow MCP Python Execution**
  off unless you need it.

## Claude Code

Sign in with your subscription: run `claude`, then `/login`. Then, from your Unreal project's folder:

```
claude mcp add --transport http nodetocode http://127.0.0.1:27000/mcp
claude mcp list
```

`claude mcp list` should show `nodetocode: http://127.0.0.1:27000/mcp (HTTP) - ✔ Connected`. In a Claude Code
session, `/mcp` lists the nodetocode tools, such as `get-focused-blueprint` and `list-blueprint-functions`.

- The server is added for the current folder only (Claude Code's default `local` scope). Add `--scope user` to use
  it in every folder.
- If `ANTHROPIC_API_KEY` is set in your environment, Claude Code can use that key instead of your subscription.
  Run `/status` to see which one it uses.
- For a client that only runs stdio servers, the plugin ships a bridge with no dependencies besides Python 3 in
  `Content/Python/mcp_bridge/`. Its launcher (`launch_bridge.bat`, or `launch_bridge.sh` on macOS and Linux) runs it
  under the Python of your newest Unreal Engine install (any version), or Python 3 from your PATH:
  `claude mcp add nodetocode -- "<plugin folder>/Content/Python/mcp_bridge/launch_bridge.bat"`
  (or `-- python <plugin folder>/Content/Python/mcp_bridge/nodetocode_bridge.py`). It keeps `café` or `日本` intact,
  passes on UE 5.8's translation progress, runs calls side by side and forwards Stop at once. It finds the editor
  through the file its MCP server writes in `%LOCALAPPDATA%\NodeToCode\servers\`, and can start before the editor.
  With several editors open, add `--project <name or .uproject path>` (or set `NODETOCODE_PROJECT`), else it answers
  with an error listing them; `--port 27000` or `--url http://127.0.0.1:27000/mcp` names the editor directly.

Tested on Windows with Claude Code 2.1.300 and UE 5.8: the HTTP form and both bridge forms connected and listed 59
tools. The launcher form also ran translations through `claude -p` on UE 5.6 and 5.8.

## Translating with your subscription

Click into a Blueprint graph and ask the agent, for example: "Use the nodetocode get-focused-blueprint tool and
translate that graph to Unreal C++." The focused Blueprint is the Blueprint editor you last clicked (its tab or inside
it) or that the agent last opened with `open-blueprint-asset`. The agent does the translation with your subscription.

The agent can also read a Blueprint you have not opened: `list-blueprint-graphs` lists its graphs and
`get-blueprint-n2cjson` returns one graph, or all of them, by asset path, for example "Use nodetocode
get-blueprint-n2cjson on /Game/Blueprints/BP_Door and explain its OpenDoor function." A large Blueprint is written to a
file under `Saved/NodeToCode/Exports` and the agent is given its path, since Claude Code cuts tool results at about
25,000 tokens.

The `translate-focused-blueprint` tool is different: it runs NodeToCode's own translation with the provider and
**API Key** from Project Settings, and is billed to that key. Leave it out if you want to use only your subscription.
The translation comes back as the tool's result once it finishes, which can take a few minutes. On UE 5.8 the call
also reports progress while it runs (for example "Waiting for the LLM response (12 s elapsed)"); on UE 5.6 and 5.7
it returns only the result.

## Antigravity CLI and Gemini CLI

Google moved Google AI Pro and Ultra subscribers from Gemini CLI to Antigravity CLI (`agy`) on 2026-06-18. Add
NodeToCode to `~/.gemini/config/mcp_config.json` (every workspace) or `.agents/mcp_config.json` (one workspace):

```json
{
  "mcpServers": {
    "nodetocode": { "serverUrl": "http://127.0.0.1:27000/mcp" }
  }
}
```

If you still use Gemini CLI (Gemini Code Assist Standard or Enterprise), run
`gemini mcp add --transport http nodetocode http://127.0.0.1:27000/mcp`, then `gemini mcp list`; `/mcp` in a session
lists the tools.

Untested: neither CLI was run against NodeToCode when this page was written. Both forms come from Google's docs.

## Codex CLI

Sign in to Codex with your ChatGPT plan, then add this to `~/.codex/config.toml`:

```toml
[mcp_servers.nodetocode]
url = "http://127.0.0.1:27000/mcp"
```

`codex mcp list` should list `nodetocode`, and `/mcp` in the Codex TUI shows its tools. Untested: this form comes
from OpenAI's Codex docs and was not run against NodeToCode when this page was written.

## If the agent does not connect

| What you see | What to do |
|---|---|
| Failed to connect, or the agent sees another project | Open the editor first, check **Enable MCP Server**, and use the URL in **Server Status** (a second editor moves to the next free port) |
| Connected, but the agent sees no Blueprint | Click into a Blueprint graph so it has focus, then ask again |
| Requests rejected with 403 | Use `127.0.0.1` or `localhost` in the URL; the server refuses other host names |
| `Request body too large` (413) | The server takes requests up to 8 MiB; split the work into smaller tool calls |
| `Session not found` (404) after the editor restarted | Each agent gets its own session and a restarted editor forgets them: reconnect the agent (Claude Code: `/mcp`, **Reconnect**) |
