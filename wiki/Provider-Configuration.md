### Plugin Settings → Provider Configuration
***

## Provider Selection & Setup

Choose and configure your [preferred LLM provider](Choosing-an-LLM-Provider) through the [Plugin Settings](Initial-Configuration#accessing-plugin-settings):

### [Anthropic](LLM-Provider-Documentation-&-Resources#anthropic-claude)
- **API Key**: Enter your Claude API key
- **Model**:
  - **Claude Sonnet 5.5** (default, recommended)
  - **Claude Opus 5.5** (most capable)
  - **Claude Haiku 5.5** (lowest cost)
  - **Claude Opus 4.5**
  - **Claude Sonnet 4.5 (retires 2026-11-30)**
- **Max Output Tokens**: 0 (Auto) by default, see [Output Token Budget](#output-token-budget)

### [OpenAI](LLM-Provider-Documentation-&-Resources#openai)
- **API Key**: Enter your OpenAI API key
- **Model**:
  - **GPT-6.1 Sol** (default, recommended)
  - **GPT-6 Astra** (most capable)
  - **GPT-6 Luna** (lowest cost)
  - **GPT-5.2**, **GPT-5.2 Pro**
  - **GPT-5**, **GPT-5 Mini**, **GPT-5 Nano** and **o3** (shut down 2026-12-11)
  - **GPT-4.1**, **GPT-4o** and **GPT-4o Mini** (shut down 2027-01-14)
- **Max Output Tokens**: 0 (Auto) by default, see [Output Token Budget](#output-token-budget)

### [Google's Gemini](LLM-Provider-Documentation-&-Resources#gemini)
- **API Key**: Enter your Gemini API key
- **Model**:
  - **Gemini 3.8 Flash** (default, recommended)
  - **Gemini 4 Argon** (most capable)
  - **Gemini 3.5 Flash-Lite** (lowest cost)
  - **Gemini 3 Flash Preview**
- **Max Output Tokens**: 0 (Auto) by default, see [Output Token Budget](#output-token-budget)

A model whose name ends in a date still works until that date, then the provider turns it off. Switch to one of
the recommended models before then.

### Retired models

Models the provider has shut down are no longer offered in the **Model** list. If your project had one selected
(for example Claude 3.7 Sonnet, o4-mini or Gemini 2.5 Flash Preview), Node to Code uses that provider's default
model instead when the editor starts and writes a warning to the Output Log. Your project's config file is not
changed until you choose a **Model** in **Project Settings → Plugins → Node to Code**, so the warning repeats on
each start until you do.

### [DeepSeek](LLM-Provider-Documentation-&-Resources#deepseek)
- **API Key**: Enter your DeepSeek API key
- **Model**:
  - **DeepSeek Flash, thinking (high effort)** (default, recommended): `deepseek-flash` with thinking on
  - **DeepSeek Flash, no thinking (JSON output)**: `deepseek-flash` with thinking off and JSON output, faster and cheaper
  - **DeepSeek V4 Pro, thinking (high effort)** (most capable): `deepseek-v4-pro` with thinking on
- **Max Output Tokens**: 0 (Auto) by default, see [Output Token Budget](#output-token-budget)

DeepSeek discontinued `deepseek-reasoner` and `deepseek-chat` on 2026-07-24. A project that saved DeepSeek R1 or
DeepSeek V3 keeps working without changes: R1 now sends `deepseek-flash` with thinking on, and V3 sends
`deepseek-flash` with thinking off. A model chosen per request through the MCP `translate-focused-blueprint` tool uses
that model's own thinking mode (for `deepseek-flash` that is thinking on, unless it is also the model selected
here).

### [Ollama (Local Processing)](LLM-Provider-Documentation-&-Resources#ollama-local)
- **Endpoint**: Default http://localhost:11434/
- **No API Key Required**
- **Advanced Settings**:
  - Model name (e.g., codellama:code)
  - Use System Prompts (on by default; turn it off only for a model whose template drops system messages)
  - Think: **Model Default** sends nothing, **On** and **Off** switch thinking on or off, and **Level** sends a
    level name for models that take one (for example `low`, `medium` or `high` for gpt-oss; `ollama show <model>`
    lists them). This replaces typing `/no_think` into Prepended Model Command.
  - Context Window (0 = the server's context length). The system prompt alone is about 4-5K tokens, so 32K-64K is
    recommended (see [Translation Depth](Translation-Depth) for guidance). Ollama cuts a prompt that is longer than
    the window without an error; NodeToCode logs a warning when its estimate of the prompt does not fit.
  - Max Tokens (0 = no limit, the server default)
  - Keep Alive Duration (seconds; 0 unloads the model after each request, -1 keeps it loaded)
  - Temperature, Top P, Top K, Min P, Repeat Penalty, Mirostat Mode, Mirostat Eta, Mirostat Tau and Random Seed:
    each is sent only when its checkbox is ticked. Unticked, the model's own defaults apply, which are usually
    tuned for that model.

Settings saved by an earlier version are updated once when the editor loads them: values equal to the old built-in
defaults (Context Window 8192, Max Tokens 8192, Temperature 0, Top P 0.5, Top K 40, Min P 0.05, Repeat Penalty 1.1,
Use System Prompts off) are cleared so the server and model defaults apply, and any other value you had set is kept
with its checkbox ticked.

### [LM Studio (Local Processing)](LM-Studio-Quick-Start)
- **Server Endpoint** (default http://localhost:1234) and **Model Name** (the model loaded in LM Studio, default `qwen3-32b`); no API key
- **Prepended Model Command**: optional text added to the start of each message, for example `/no_think` to turn off
  thinking on reasoning models. Sampling settings are set in the LM Studio app. Setup: [LM Studio Quick Start](LM-Studio-Quick-Start)
- **Max Output Tokens**: 0 (Auto) by default, which sends no `max_tokens`, so the server uses whatever context the
  prompt leaves. This is also the setting to use for an OpenAI-compatible server such as vLLM reached through the
  LM Studio provider. See [Output Token Budget](#output-token-budget)

## Output Token Budget

Each provider except Ollama has a **Max Output Tokens** setting: the most tokens one translation may write. On
reasoning models the thinking tokens count against it too.

- **0 (Auto, the default)**: for Anthropic, OpenAI, Gemini and DeepSeek, NodeToCode sends the model's maximum output,
  but at most 32,000 tokens (65,536 for DeepSeek thinking modes, whose reasoning counts against the limit), and less
  when needed so the prompt plus the output fits the model's context window. The ceiling is there because requests
  are not streamed yet, and a very long request without streaming can time out; it goes away once NodeToCode
  streams responses. Anthropic always gets a value
  because its API requires one; for a model name NodeToCode does not list (such as a newer ID passed by an MCP
  client) the other providers get none, so the provider's own default applies. For LM Studio, Auto sends no limit
  at all, so the server uses the context left after the prompt. A local server with a small window, such as vLLM
  started with `max_model_len 16384`, needs this: any fixed limit plus the prompt can be larger than the window,
  and the server then rejects the request.
- **Any other value** is sent as-is, capped at the model's maximum output when NodeToCode lists the model (not at
  the Auto ceiling: set a larger value yourself if a translation needs it and your requests finish in time). OpenAI
  reasoning models receive it as `max_completion_tokens`, other OpenAI models as `max_tokens`, and models on the
  Responses API as `max_output_tokens`. Set a value to limit the cost of a single translation; if translations end
  in a truncated or malformed JSON error, raise it or set it back to 0.

Before 2.0 every request asked for 16,000 output tokens and there was no setting. Projects saved by an earlier
version load with Max Output Tokens 0 (Auto). Ollama keeps its own **Max Tokens** setting in the Advanced Settings
above.

## Subscription sign-in removed in 2.0

Every cloud provider (Anthropic, OpenAI, Gemini, DeepSeek) needs its **API Key**; Ollama and LM Studio need
none. Sign-in with a Claude Pro/Max subscription or a Google account was removed; a project that had it selected
now uses the API key. To use a Claude, ChatGPT or Google AI subscription, connect Claude Code, Antigravity CLI or
Codex CLI to the NodeToCode MCP server: see [Using a Claude or Gemini Subscription](Using-a-Claude-or-Gemini-Subscription),
which also covers the old setting and saved tokens.

## Sampling Settings for Cloud Models

For cloud providers, NodeToCode sends a temperature only to models that accept one, and otherwise leaves the provider's default in place. Current Claude models (Opus 4.7 and later), OpenAI reasoning models (the o-series and GPT-5 family) and Gemini 3 models get no temperature, top P or top K, because they reject those settings or work worse with them. A model name NodeToCode does not list, such as a newer ID passed by an MCP client, is treated the same way. Ollama uses the **Advanced Settings** above, and LM Studio uses the sampling settings in the LM Studio app.

## Best Practices

1. **API Key Security**
   - Never commit API keys to version control
   - Regularly rotate keys for security
   - Configure through [Plugin Settings](Initial-Configuration#accessing-plugin-settings)

2. **Model Selection**
   - Start with recommended models for your use case
   - Consider [cost vs capability](Choosing-an-LLM-Provider) tradeoffs
   - Monitor token usage and adjust as needed

3. **Local Processing**
   - Ensure sufficient system resources for Ollama or LM Studio
   - Monitor GPU/CPU usage
   - Adjust context window based on translation needs

## More Provider Resources

- Not sure which model fits your needs? See [Choosing an LLM Provider](Choosing-an-LLM-Provider).
- Provider documentation and references: [LLM Provider Documentation & Resources](LLM-Provider-Documentation-&-Resources).
