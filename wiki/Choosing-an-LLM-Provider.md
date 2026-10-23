### Best Practices → Choosing an LLM Provider
***
<p align="center">
    <img src="https://github.com/protospatial/NodeToCode/blob/main/assets/Image_NodeToCode_ServiceProviders.jpg" alt="Node to Code header" width="1250">
</p>

## Provider/Model Breakdown

| Model | Strengths | Best For |
|-------|-----------|-----------|
| Anthropic Claude 3.5 Sonnet | - Exceptional use of UE C++ patterns<br>- Macro usage<br>- Great documentation | Complex systems, Performant code |
| Anthropic Claude 3.5 Haiku | - Use of UE C++ patterns<br>- Macro usage<br>- Minimal or no documentation<br>- Fast<br> | Less complex systems, Prototypes, Affordability |
| OpenAI GPT o3-mini | - Great use of UE C++ patterns<br>- Macro usage<br>- Great documentation | Complex systems requiring thoughtful logic reasoning, Performance-minded code, Great pricing
| OpenAI GPT o1 | - Exceptional use of UE C++ patterns<br>- Macro usage<br>- Exceptional documentation | Complex systems requiring advanced logic reasoning, Performance-minded code
| OpenAI GPT-4o | - Decent use of UE C++ patterns<br>- Moderate code structure | Medium complexity translations |
| OpenAI GPT-4o-mini | - Inconsistent use of UE C++ patterns<br>- General code structure<br>- Fast<br> | Less complex translations, Prototypes, Affordability |
| OpenAI GPT o3-mini | - Great use of UE C++ patterns<br>- Macro usage<br>- Great documentation | Complex systems requiring thoughtful logic reasoning, Performance-minded code, Great pricing
| Gemini 2.0 Flash Thinking | - Great use of UE C++ patterns<br>- Macro usage<br>- Decent documentation | Complex systems requiring advanced logic reasoning, Performance-minded code, Massive 2M token context window
| Gemini 2.0 Pro | - Good use of UE C++ patterns<br>- Macro usage<br>- Good documentation | Complex systems, Performance-minded code, Massive 2M token context window
| Gemini 2.0 Flash | - Decent use of UE C++ patterns<br>- Macro usage<br>- Good documentation | Fast, Complex systems, Performant code, Huge 1M token context window
| Gemini 1.5 Pro | - Adequate use of UE C++ patterns<br>- Okay documentation | Less complex systems, Massive 2M token context window
| Gemini 1.5 Flash | - Use of UE C++ patterns<br> Okay documentation | Very fast, Quick Conversions, Huge 1M token context window
| DeepSeek R1 | - Adequate use of UE C++ patterns<br>- Macro usage<br>- Acceptable documentation | Complex systems requiring thoughtful logic reasoning, Performance-minded code, Cost-Effective Capabilities
| DeepSeek V3 | - Adequate use of UE C++ patterns<br>- Moderate code structure<br>- Moderate Macro usage<br>- Documentation<br>| Quick conversions, Prototypes, Affordability |
| Ollama | - Local processing<br>- Wide range of model capabilities<br>- Ability to use custom models | Full control of model, 100% Privacy, Offline work, Cost-effectiveness |

> [!NOTE]
> Claude 3.5 Sonnet and o3-mini typically produce the most idiomatic Unreal Engine C++ code 
with proper macro usage and memory management patterns for reasonable pricing.

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