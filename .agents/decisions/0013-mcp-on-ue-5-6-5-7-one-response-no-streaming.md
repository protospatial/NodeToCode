# 0013. MCP on UE 5.6 and 5.7: the engine HTTP server, one JSON response, no progress streaming

## Status

decided (2026-11-02), by the owner on card 0085 (option b; it had been applied provisionally since October)

## Sources

- Kanban card 0085 "Decide how /mcp streams progress on UE 5.6 and 5.7" (owner decision, decided 2026-11-02). Cards that
  build on it: the 5.6/5.7 streaming card and the side-server deletion card it names, 0437, 0209, 0211 and 0455.
- NodeToCode 2.0 review, 2026-10-22: the transport findings behind card 0085. Evidence: the card's "What was seen".
- Card 0512 (#275): a held call is cancelled when its client closes the connection, on 5.6-5.8 (Windows).

## Context

UE 5.8's engine HTTPServer can send one response in several writes
(`EHttpServerResponseFlags::MultipleWriteStream`), so on 5.8 translate progress and the result go out on the same
POST. The 5.6 and 5.7 HTTPServer cannot. Options for 5.6/5.7: (a) serve `/mcp` from the bundled cpp-httplib bound
to 127.0.0.1 with chunked `text/event-stream` responses, keeping a second HTTP stack in the plugin; (b) keep the
engine HTTPServer and hold one JSON response until the tool finishes.

## Decision

Option (b).

1. **On UE 5.6 and 5.7** a long-running tool call holds the POST and returns one JSON result, with no progress
   notifications. The switch point is `FN2CMcpHttpServerManager::CanStreamToolProgress()` /
   `N2C_MCP_HTTP_CAN_STREAM`.
2. **On UE 5.8** progress streams on the same POST when the client accepts `text/event-stream`.
3. **No cpp-httplib MCP server** is kept for 5.6/5.7.

## Consequences

- No second HTTP stack to patch for CVEs or licences.
- On 5.6/5.7 a client that aborts calls without progress (Claude Code, at about 5 minutes) may drop a very long
  translation. Card 0512 makes sure the editor then stops the provider call instead of running it to the end.
- Choosing streaming on 5.6/5.7 later needs a new record that supersedes this one.
