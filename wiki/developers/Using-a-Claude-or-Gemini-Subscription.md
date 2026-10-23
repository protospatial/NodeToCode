NodeToCode translates with an API key from each provider; it cannot sign in with a Claude Pro/Max, ChatGPT or
Google AI subscription. To use a subscription, run the vendor's own coding agent (Claude Code, Antigravity CLI or
Codex CLI), signed in with that subscription, and connect it to NodeToCode's MCP server. The agent reads your
Blueprints through NodeToCode's tools and writes the code itself, so NodeToCode makes no LLM request and needs no key.

The MCP server listens at **`http://127.0.0.1:27000/mcp`** while the editor is open. Every snippet below uses that
URL; if you change the port, change it in the snippet too.

## API keys

To translate inside the editor, set the **API Key** for your provider in **Project Settings → Plugins → Node to
Code** (see [Provider Configuration](Provider-Configuration)). Keys are saved in `secrets.json` in your user folder,
outside every project (see [Initial Configuration](Initial-Configuration#plugins-config-files--source-control)), so
they are not in anything you commit, and the MCP file tools refuse to read that file.

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
- Saved sign-in tokens are deleted from `secrets.json` the first time the editor loads it. API keys are kept.
- A translation with no API key set stops before anything is sent, and the error names the key to set.

## Before you connect an agent

- Open your project in the Unreal editor. The MCP server runs only while the editor is open.
- **Enable MCP Server** (**Project Settings → Plugins → Node to Code (User) → MCP Server**) is on by default.
  Changing it needs an editor restart.
- **Server Port** (**Project Settings → Plugins → Node to Code → MCP Server**) is 27000 by default.
- The server answers only on this machine (`localhost`, `127.0.0.1`), so run the agent on the same computer.

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
- For a client that only runs stdio servers, the plugin ships a bridge with no dependencies besides Python 3, in
  the plugin's folder at `Content/Python/mcp_bridge/nodetocode_bridge.py`:
  `claude mcp add nodetocode -- python <plugin folder>/Content/Python/mcp_bridge/nodetocode_bridge.py --port 27000`

Tested on Windows with Claude Code 2.1.300 and UE 5.8: both forms connected and listed 59 tools.

## Translating with your subscription

Focus a Blueprint graph in the editor and ask the agent, for example: "Use the nodetocode get-focused-blueprint
tool and translate that graph to Unreal C++." The agent does the translation with your subscription.

The `translate-focused-blueprint` tool is different: it runs NodeToCode's own translation with the provider and
**API Key** from Project Settings, and is billed to that key. Leave it out if you want to use only your subscription.

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
| Failed to connect | Open the editor first, and check **Enable MCP Server** and **Server Port** |
| Connected, but the agent sees no Blueprint | Click into a Blueprint graph so it has focus, then ask again |
| Requests rejected with 403 | Use `127.0.0.1` or `localhost` in the URL; the server refuses other host names |
