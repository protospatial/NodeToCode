### Best Practices → Choosing an LLM Provider
***
<p align="center">
    <img src="https://github.com/protospatial/NodeToCode/blob/main/assets/Image_NodeToCode_ServiceProviders.jpg" alt="Node to Code header" width="1250">
</p>

## Provider/Model Breakdown

Each cloud provider offers a recommended default, a more capable model and a cheaper one. Prices are per
million tokens on the provider's standard tier when this page was written (2026-10-22); check the provider's
pricing page before relying on them.

| Model | Input / output price | Context window | Best For |
|-------|----------------------|----------------|----------|
| Anthropic Claude Sonnet 5.5 (default) | $2 / $10 | 1M | Most translations: strong UE C++ at a moderate price |
| Anthropic Claude Opus 5.5 | $4 / $20 | 1M | Large or complex graphs where quality matters most |
| Anthropic Claude Haiku 5.5 | $0.10 / $0.50 | 1M | Quick conversions, prototypes, high volume |
| OpenAI GPT-6.1 Sol (default) | $2 / $10 | 1.05M | Most translations |
| OpenAI GPT-6 Astra | $10 / $50 | 1.05M | The hardest translations, cost no object |
| OpenAI GPT-6 Luna | $0.10 / $0.50 | 1.05M | Quick conversions, prototypes, high volume |
| Gemini 3.8 Flash (default) | $0.75 / $3.75 | 1M | Most translations, very large graphs |
| Gemini 4 Argon | $2 / $10 | 1M | Complex graphs |
| Gemini 3.5 Flash-Lite | $0.30 / $2.50 | 1M | Quick conversions, prototypes |
| DeepSeek R1 | - | - | Complex systems requiring thoughtful logic reasoning, Cost-Effective Capabilities |
| DeepSeek V3 | - | - | Quick conversions, Prototypes, Affordability |
| Ollama | Free (local) | Set by you | Full control of model, 100% Privacy, Offline work, Cost-effectiveness |

> [!NOTE]
> Google lists higher prices for Gemini 3.8 Flash from 2027-01-01 and Gemini 4 Argon from 2026-12-01. Older
models that are still listed in **Project Settings** show their shutdown date in their name; see
[Provider Configuration](Provider-Configuration#retired-models) for what happens after it.

> [!IMPORTANT]
> DeepSeek very likely does use API data for training
their models, so use at your own discretion.


## Model Selection Guidelines

1. **Consider Your Needs**
    - Translation complexity
    - Budget constraints
    - Privacy requirements
    - Quality expectations

2. **Balancing Factors**
    - Cost per token
    - Translation quality
    - Processing speed
    - Context window size
    
> [!NOTE]
> More capable models typically cost more but require less manual
refinement of the generated code.