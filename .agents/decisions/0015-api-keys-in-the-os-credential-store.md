# 0015. API keys: per user, in Windows Credential Manager, with environment variables as the fallback

## Status

decided (2026-11-02), by the owner on card 0061 (applied provisionally since 2026-10-23: first the per-user file of
card 0062, then the Credential Manager backend of card 0161)

## Sources

- Kanban card 0061 "Decide where API keys live" (owner decision, decided 2026-11-02). Cards that built on it: 0062
  (keys out of the project), 0161 (the OS keychain backend), #190 (serialized, read-back Credential Manager calls).
- `Context/auth-secrets/AGENTS-auth-secrets.md` (the store as built), `wiki/Initial-Configuration.md`.
- NodeToCode 2.0 review, 2026-10-22: keys in the project folder (`Saved/NodeToCode/User/secrets.json`) and the
  read-file exposure of `secrets.json`.

## Context

NodeToCode 1.x kept API keys in a file inside the project, where version control, sharing a project and agent file
tools could expose them.

## Decision

1. **Per user, never per project.** Keys belong to the person, not the project; nothing is written under the
   project folder.
2. **Windows (the 2.0-beta platform, record 0001):** Windows Credential Manager, one generic credential per
   provider, target `NodeToCode/<ProviderId>` (`FN2CCredentialManagerBackend`). The 1.x project file and the interim
   per-user file are moved into it once.
3. **macOS and Linux:** the per-user file `FPlatformProcess::UserSettingsDir()/NodeToCode/secrets.json`
   (`FN2CSecretFileBackend`), written atomically, until a Keychain or Secret Service backend is verified on real
   hardware (gate 0393).
4. **Environment variables are the fallback:** a stored key wins; otherwise the first set of `OPENAI_API_KEY`,
   `ANTHROPIC_API_KEY`, `GEMINI_API_KEY` then `GOOGLE_API_KEY`, `DEEPSEEK_API_KEY` then `DEEPSEEK_API`.
5. **Agents never read keys:** the agent path policy denies the key store's folders and any `secrets.json*`.

## Consequences

- Removing a key is `cmdkey /delete:NodeToCode/<Provider>` (a Project Settings button is card 0488).
- Tests use `NodeToCodeTest/<guid>/` targets only and clean up; the owner's `NodeToCode/*` entries are never touched.
- A Mac or Linux keychain backend is a new card, not a change to this record.
