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
| 1.2.x | `main` (and the Fab release) | Critical fixes only |
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
  loopback unless the project's `[HTTPServer.Listeners]` engine config overrides it. The notification server on the
  next port listens on 127.0.0.1.
- **Origin and Host checks.** Both servers refuse a request whose `Origin` is not a loopback origin or whose
  `Host` is not `localhost`, `127.0.0.1` or `[::1]`, so a web page or a DNS-rebinding attack cannot drive them.
  POST requests to `/mcp` must be `application/json`.
- **Can be turned off.** **Enable MCP Server** (Project Settings, Plugins, Node to Code (User)) is a per-user
  setting. The server does not start in commandlets or unattended runs unless `-N2CMcp` is on the command line.
- **Python execution off by default.** `run-python`, `save-python-script` and `delete-python-script` exist only when
  the per-user **Allow MCP Python Execution** setting is on. With it on, any connected MCP client can run arbitrary
  Python in the editor.
- **API keys.** Keys entered in Project Settings are kept outside the project in a per-user file,
  `<user settings folder>/NodeToCode/secrets.json`, as plain (unencrypted) JSON; they are not written to the
  project's config files. The MCP file tools refuse to read or list that folder, any `secrets.json*` file, `.env`
  files and the project's `.git` folder.

Any process running as you on the same machine can reach the server, and text inside Blueprints (comments, names)
reaches the agent that reads it. Treat a connected agent as having the access your enabled tools give it.
