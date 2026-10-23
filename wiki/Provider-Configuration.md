### Plugin Settings → Provider Configuration
***

## Provider Selection & Setup

Choose and configure your [preferred LLM provider](Choosing-an-LLM-Provider) through the [Plugin Settings](Initial-Configuration#accessing-plugin-settings):

### [Anthropic](LLM-Provider-Documentation-&-Resources#anthropic-claude)
- **API Key**: Enter your Claude API key
- **Model**:
  - Claude3_5_Sonnet **(Recommended)**
  - Claude3_5_Haiku

### [OpenAI](LLM-Provider-Documentation-&-Resources#openai)
- **API Key**: Enter your OpenAI API key
- **Model**:
  - GPT_o1
  - GPT_o1_Preview
  - GPT_o3_Mini **(Recommended)**
  - GPT_o1_Mini
  - GPT4o_2024_08_06
  - GPT4o_Mini_2024_07_18

### [Google's Gemini](LLM-Provider-Documentation-&Resources#gemini)
- **API Key**: Enter your OpenAI API key
- **Model**:
  - Gemini 2.0 Flash Thinking Exp 01-21
  - Gemini 2.0 Pro Exp 02-05
  - Gemini 2.0 Flash
  - Gemini 2.0 Flash-Lite-Preview-02-05
  - Gemini 1.5 Pro
  - Gemini 1.5 Flash

### [DeepSeek](LLM-Provider-Documentation-&-Resources#deepseek)
- **API Key**: Enter your DeepSeek API key
- **Model**:
  - DeepSeek_R1 **(Recommended)**
  - DeepSeek_V3

### [Ollama (Local Processing)](LLM-Provider-Documentation-&-Resources#ollama-local)
- **Endpoint**: Default http://localhost:11434/
- **No API Key Required**
- **Advanced Settings**:
  - Model name (e.g., codellama:code)
  - Temperature (0.0 - 2.0)
  - Context Window size (see [Translation Depth](Translation-Depth) for guidance)
  - Max Output Tokens
  - Keep Alive Duration
  - Top P
  - Top K
  - Min P
  - Repeat Penalty
  - Mirostat Mode
  - Mirostat Eta
  - Mirostat Tau
  - Random Seed

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
   - Ensure sufficient system resources for Ollama
   - Monitor GPU/CPU usage
   - Adjust context window based on translation needs

## More Provider Resources

**Not sure which model will work best for your needs?**

- Check out [Choosing-an-LLM-Provider](Choosing-an-LLM-Provider)

**Want to understand more specific details about the LLM service providers?**

- Check out [LLM Provider Documentation & Resources](LLM-Provider-Documentation-&-Resources) for a quick reference to documentation and references from each supported provider.
