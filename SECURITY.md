# Security policy

## Reporting a vulnerability

Report vulnerabilities privately through GitHub's security advisories:
https://github.com/protospatial/NodeToCode/security/advisories/new (the **Report a vulnerability** button on the
repository's **Security** tab).

Please do not open a public issue, pull request or Discord post for a vulnerability. Include the plugin version or
branch and commit, the Unreal Engine version, and the steps or a proof of concept. Never include a real API key: if
a key was exposed while you found the problem, revoke it with its provider.

The maintainer works on this project alone and has not set a response time; reports are handled as soon as
possible, and the advisory is published once a fix is available.

## Supported versions

| Version | Branch | Security fixes |
|---|---|---|
| 2.0-beta | `dev-2.0` | Yes |
| 1.2.x | `main` (and the Fab release) | No; `main` stays at 1.2.x, fixes go into 2.0 |
| Older | | No |

## Scope

In scope: the plugin's own code in this repository, including the MCP server, the Python bridge and scripts under
`Content/Python`, the LLM provider integrations and how API keys are stored. Out of scope: Unreal Engine itself,
the LLM providers' services, MCP clients, and the vendored cpp-httplib (report those upstream; tell us if
NodeToCode ships an affected version).

## The MCP server (2.0)

NodeToCode 2.0 runs an MCP server inside the Unreal Editor so that agents on the same machine can read and edit
Blueprints. It is designed for a single developer's machine; it has no authentication of its own.

- **Local only.** The MCP endpoint (`/mcp`, port 27000 by default) uses Unreal's HTTP server, which listens on
  loopback unless the project's `[HTTPServer.Listeners]` engine config overrides it. It is the only port the plugin
  opens.
- **Origin and Host checks.** The server refuses a request whose `Origin` is not a loopback origin or whose
  `Host` is not `localhost`, `127.0.0.1` or `[::1]`, so a page from another site or a DNS-rebinding attack cannot
  drive it. A page served from any port on localhost is a loopback origin and is allowed. POST requests to `/mcp`
  must be `application/json`.
- **Request size.** A request body over 8 MiB is refused with 413 before NodeToCode parses it. Unreal's HTTP server
  reads the whole body (and the headers) into memory before NodeToCode sees the request, so this limits the work
  NodeToCode does, not the memory the engine uses for one request.
- **Can be turned off.** **Enable MCP Server** (Project Settings, Plugins, Node to Code (User)) is a per-user
  setting. The server does not start in commandlets or unattended runs unless `-N2CMcp` is on the command line.
- **Python execution off by default.** `run-python`, `save-python-script` and `delete-python-script` exist only when
  the per-user **Allow MCP Python Execution** setting is on. With it on, any connected MCP client can run arbitrary
  Python in the editor, including reading your API keys. Only your own value counts (in
  `Saved/Config/<Platform>Editor/EditorPerProjectUserSettings.ini`, or a `-ini:` override on your command line): a
  value in the project's `Config/DefaultEditorPerProjectUserSettings.ini` or any other shared config file is ignored
  with a warning in the log, so a committed file cannot switch it on for everyone who opens the project. The same
  applies to **Enable MCP Server**. A project can still run its own code when you open it (C++ modules,
  `Content/Python/init_unreal.py`), so this protects against an accidental or careless commit, not a hostile
  project.
- **API keys.** On Windows, keys entered in Project Settings are kept in Windows Credential Manager (entries
  `NodeToCode/<Provider>`, encrypted for your Windows account, not roaming); a plain-text `secrets.json` left by an
  earlier version is moved there and deleted at the first start. On other platforms they are kept outside the
  project in a per-user file, `<user settings folder>/NodeToCode/secrets.json`, as plain (unencrypted) JSON. Keys
  are never written to the project's config files, the API Key fields are masked and write-only, and no Blueprint
  or Python call returns a key. The MCP file tools refuse to read or list the secrets folders, any `secrets.json*`
  file, `.env` files and the project's `.git` folder.

Any process running as you on the same machine can reach the server, and text inside Blueprints (comments, names,
tooltips, string literals) reaches the agent that reads it. Treat a connected agent as having the access your
enabled tools give it.

The threat model, with every control, its evidence and the open risks, is
[Context/mcp-server/threat-model.md](Context/mcp-server/threat-model.md).
