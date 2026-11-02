# 0014. Sign-in: the Claude Pro/Max and Google-account OAuth paths are removed; API keys only

## Status

decided (2026-11-02), by the owner on card 0038 (it had been applied provisionally since 2026-10-22 by #35)

## Sources

- Kanban card 0038 "Decide what replaces the Claude Pro/Max and Google-account OAuth paths" (owner decision,
  decided 2026-11-02).
- #35 (fbad04b) "remove the Claude subscription and Google-account sign-in paths".
- NodeToCode 2.0 review, 2026-10-22: the P0 finding that the Anthropic OAuth path impersonated Claude Code, and the
  Google-account path. Evidence: card 0038 and `python -I Reviews/2026-10-22/kanban/lookup.py` on its review ids.
- [0015](0015-api-keys-in-the-os-credential-store.md): where the keys are stored.

## Context

Early dev-2.0 builds let users sign in with a Claude Pro/Max subscription or a Google account instead of an API key (1.2.x had no OAuth). The
Anthropic path presented itself as Claude Code, which the provider's terms do not allow, and both paths kept
refresh tokens the plugin had no business holding.

## Decision

1. **Both OAuth paths are removed.** Every provider signs in with an API key (or needs none: Ollama, LM Studio).
2. **What subscription users do instead:** use Claude Code (or another MCP client) with the NodeToCode MCP server;
   the client's own sign-in covers the model, and NodeToCode provides the Blueprint tools.
3. **Google OAuth may come back** only as a provider-sanctioned flow for the Gemini API that does not impersonate
   another product, and only through a new record that supersedes this one.

## Consequences

- No refresh tokens are stored; settings saved by those early builds lose their OAuth fields on load, with a warning
  (`Context/release/migration-1.2-to-2.0.md`, rows K4 and D1).
- Subscription users connect through MCP (`wiki/developers/Connecting-an-MCP-Client.md`).
