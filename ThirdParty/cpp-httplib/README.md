# cpp-httplib (vendored)

Single-header HTTP library. **No source file includes it today**: its only user, the MCP notification side server on
`McpServerPort + 1` (`N2CSseServer.cpp`), was removed in card 0113, so nothing of it is compiled into any build. It
stays vendored (with `Source/NodeToCode.Build.cs`'s include path, the `NOTICE` section, `Config/FilterPlugin.ini` and
`Scripts/package-release.ps1`'s required files) for the UE 5.6/5.7 `/mcp` transport that card 0111 adds if decision
card 0085 picks httplib streaming for those engines. If 0085 keeps the held JSON response instead, delete this folder
and those four references together.

| | |
|---|---|
| Upstream | https://github.com/yhirose/cpp-httplib |
| Release tag | `v0.63.0` (published 2026-10-22) |
| Upstream commit | `5bcfea7b90575c80a3e54a6967b11b39afa60d5c` |
| `httplib.h` SHA-256 | `9021001a719768893a8a6e4051c56107690e8b8e49b41e5f903534fa19ae9afb` |
| `httplib.h` git blob | `a39da401a27614a415eef24cf7d0baf0305b786d` |
| `LICENSE` SHA-256 | `4b45cbe16d7b71b89ae6127e26e0d90a029198ca5e958ad8e3d0b8bbed364d8b` |
| License | MIT, see `LICENSE` (upstream file, unmodified) |

Both files are byte-for-byte copies of `httplib.h` and `LICENSE` at the tag above, fetched from
`https://raw.githubusercontent.com/yhirose/cpp-httplib/<commit>/<file>`. The SHA-256 values are of those files with
LF line endings, as stored in git (a Windows checkout with `core.autocrlf` shows CRLF and hashes differently; the git
blob ids match upstream either way).

## Updating

1. Download `httplib.h` and `LICENSE` from a tagged release commit into an empty folder, never from a branch.
2. Check the new version against https://github.com/yhirose/cpp-httplib/security/advisories and
   https://osv.dev (query the tag's commit hash).
3. Copy both files here, update the table above, and rebuild on every supported engine version.

The removed side server set request limits (header count, header and URI length, payload size, timeouts) before and
after its `#include "httplib.h"`; see `Source/Private/MCP/Server/N2CSseServer.cpp` at commit `99ee053` for a server
that uses this header again.
