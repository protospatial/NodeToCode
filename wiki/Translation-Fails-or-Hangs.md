### Troubleshooting → Translation Fails or Hangs
***

## Common Error Responses

When using cloud-based LLM providers (Anthropic, OpenAI, DeepSeek), you may encounter these standard HTTP error responses:

**Tip:** Check the [Output Log](https://dev.epicgames.com/documentation/en-us/unreal-engine/logging-in-unreal-engine) with [MinSeverity](Logging-Configuration) set to `Debug` to see detailed error responses.


| Code | Type | Description | Resolution |
|------|------|-------------|------------|
| 400 | invalid_request_error | Issue with request format/content | Check plugin settings and reduce complexity if needed |
| 401 | authentication_error | Invalid or missing API key | Verify API key in Project Settings |
| 403 | permission_error | Insufficient permissions | Check API key permissions and usage tier |
| 404 | not_found_error | Resource not found | Verify endpoint URLs and model availability |
| 413 | request_too_large | Request exceeds size limits | Reduce reference files or translation depth |
| 429 | rate_limit_error | Rate limit exceeded | Wait before retrying or check usage quotas |
| 500 | api_error | Provider internal error | Wait and retry; check provider status |
| 529 | overloaded_error | Service temporarily overloaded | Wait and retry later |

## How a Failed Translation Is Reported

NodeToCode keeps the provider's HTTP status and its own error message and sorts every failure into a kind:
`RateLimited`, `Overloaded`, `Auth`, `InvalidRequest`, `Billing`, `Server`, `Network` (no response, a timeout, or a
proxy or gateway error such as 502), `Truncated`, `Refused`, `Blocked`, `Parse` (the answer arrived but could not be
read as a translation) or `Unknown`. `RateLimited`, `Overloaded`, `Server` and `Network` are marked retryable: the same
request may work later.

A successful (HTTP 200) answer can still stop early. NodeToCode reads the provider's stop or finish reason before the
answer and reports it by name, with the provider's reason in `provider_error_type`:

| Kind | Provider reason | What to do |
|------|-----------------|------------|
| `Truncated` | Anthropic `stop_reason` `max_tokens` or `model_context_window_exceeded`; OpenAI, DeepSeek and LM Studio `finish_reason` `length`; OpenAI Responses `status` `incomplete` (`max_output_tokens`); Gemini `finishReason` `MAX_TOKENS`; Ollama `done_reason` `length` | The answer hit the output token limit. Raise that provider's **Max Output Tokens** (0 = Auto; for Ollama, **Max Tokens** or **Context Window**), see [Output Token Budget](Provider-Configuration#output-token-budget). Thinking tokens count against it on reasoning models. |
| `Refused` | Anthropic `stop_reason` `refusal` (with `stop_details` category and explanation); an OpenAI `refusal` message | The model declined the request. The message carries the provider's explanation. |
| `Blocked` | `finish_reason` `content_filter`; Gemini `finishReason` `SAFETY`, `RECITATION`, `BLOCKLIST`, `PROHIBITED_CONTENT`, `SPII` and other filter reasons; Gemini `promptFeedback.blockReason` | A provider safety filter stopped the prompt or the answer. |

Reasoning text (`reasoning_content`, `reasoning`, `thinking`) is logged but never used as the answer: an answer that is
only reasoning is a `Parse` error that names the stop reason.

- **Output Log:** `Translation failed: Gemini request failed with HTTP 503 (Overloaded, retryable): <the provider's message>`.
- **Progress dialog:** a failed graph shows the same line under its name and as its tooltip.
- **Batch translation:** each failed item in `BatchSummary.json` has `error` (that line), `errorKind`, `httpStatus`,
  `retryable` and `providerMessage`.
- **MCP `translate-focused-blueprint`:** the result has `isError: true` and its text is JSON:

```json
{"error": "Gemini request failed with HTTP 400 (Auth): API key not valid. Please pass a valid API key.",
 "error_kind": "Auth", "message": "API key not valid. Please pass a valid API key.", "http_status": 400,
 "retryable": false, "provider": "Gemini", "model": "gemini-3.5-flash-lite", "provider_error_type": "INVALID_ARGUMENT"}
```

A translation cut at the output limit looks like this (no `http_status`, since the provider answered 200):

```json
{"error": "Anthropic request failed (Truncated): The answer stopped at the output token limit (stop_reason \"max_tokens\") ...",
 "error_kind": "Truncated", "message": "The answer stopped at the output token limit (stop_reason \"max_tokens\") before the translation was complete. Raise Project Settings > Node to Code > LLM Services > Anthropic > Max Output Tokens (0 = Auto); thinking tokens count against it on reasoning models.",
 "retryable": false, "provider": "Anthropic", "model": "claude-haiku-5-5", "provider_error_type": "max_tokens"}
```

`http_status` is left out when there was no HTTP response (for example a connection failure or a missing API key).

## Translation Timeout Issues

If your translation appears to hang indefinitely:

> [!NOTE]
> Node to Code's default timeout is 1000 seconds, but translations typically
> complete within 30-60 seconds depending on graph complexity and LLM provider & model.

**Check for:**
- Network connectivity issues
- LLM provider service status
- Token limit exceeded
- Request size too large

**Resolution Steps:**
1. Check the Output Log for timeout messages
2. Verify internet connection
3. Confirm LLM provider status
4. Reduce translation complexity if needed:
   - Lower translation depth
   - Remove unnecessary reference files
   - Split large graphs into smaller components
   - Use a model with larger context window

## Ollama-Specific Issues

When using local LLM processing through Ollama, unique issues may arise:

1. **Service Connection**
   ```bash
   # Verify Ollama is running with:
   curl http://localhost:11434/api/version
   ```
   - Check endpoint configuration in Plugin Settings
   - Ensure Ollama service is running
   - Verify network access if running on remote machine

2. **Model Management**
   ```bash
   # List installed models
   ollama list
   
   # Pull required model if missing
   ollama pull codellama:code
   ```
   - Confirm model is installed
   - Check model name matches Plugin Settings
   - Verify model compatibility

3. **Resource Constraints**
   - Monitor system resources:
     - System RAM usage
     - CPU utilization
     - GPU availability and VRAM usage (if using GPU acceleration)
     - Disk space for model storage

4. **Common Ollama Errors**
   - You may see errors pertaining to the following:
     - Failed to connect to Ollama service
     - Model not found
     - Out of memory
     - GPU not available

   **Resolution Steps:**
   1. Restart Ollama service
   2. Free up system resources
   3. Check Ollama logs for detailed error messages
   4. Refer to Ollama's documentation and/or community for further troubleshooting

## General Troubleshooting Steps

1. **Check Logs**
   - Review Output Log for specific error messages
   - Check HTTP response codes
   - Monitor token usage warnings
   - Look for timeout indicators

> [!NOTE]
> Set MinSeverity to Debug in Plugin Settings for maximum logging
> detail during troubleshooting.

2. **Validate Configuration**
   - Confirm plugin settings
   - Test API connectivity
   - Verify file paths for reference files
   - Check model selection and availability

3. **Reduce Complexity**
   - Lower translation depth
   - Remove reference files
   - Simplify Blueprint graph
   - Split into smaller translations

> [!NOTE]
>  Tip: Start with a simple graph translation to establish baseline functionality
>  before attempting more complex translations.

## Prevention Tips

1. **Regular Maintenance**
   - Keep API keys updated
   - Monitor token usage and costs
   - Update plugin regularly
   - Clean up reference files

2. **Best Practices**
   - Start with simple translations
   - Gradually increase complexity
   - Document successful configurations
   - Maintain backup API keys

> [!NOTE]
> Keep reference files under 50KB - 100KB total (depending on LLM context window)
> for optimal performance and reduced token usage.

## When to Seek Support

If issues persist after trying the above solutions, prepare the following information before submitting an issue in the [Node to Code Discord](https://discord.gg/4t3Syvk4AG):
1. Complete error messages from Output Log
2. Plugin version
3. Unreal Engine version
4. LLM provider and model
5. Sample Blueprint causing issues (if possible)
6. Steps to reproduce the problem
7. Any relevant plugin settings

Remember that most translation issues can be resolved by:
1. Checking error responses in the [Output Log](https://dev.epicgames.com/documentation/en-us/unreal-engine/logging-in-unreal-engine)
2. Validating [configuration and credentials](Provider-Configuration)
3. Ensuring proper resource availability
4. Starting with simpler translations to isolate issues. See [Blueprint Selection](Blueprint-Selection) for guidance.
