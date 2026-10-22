## Quick Start with LM Studio + NodeToCode

### 1. Download & Install LM Studio
- Visit [https://lmstudio.ai/](https://lmstudio.ai/) and download for your platform (Windows, macOS, Linux)
- **System Requirements:** 
	- Windows/Linux: 24GB+ VRAM recommended or similar ARM unified memory architecture
	- An Apple Silicon Mac with 24GB+ of unified memory is also a very viable option.
> [!NOTE]
> Just as with Ollama, the amount, type, and speed of memory your LLM is loaded onto (alongside other system specs) will greatly impact not only the speed of translations, but also the model (and its capabilities) that you can feasibly run.

### 2. Download a Model
- Launch LM Studio and use the search page (Purple button) to find models (e.g., "qwen3-32b") that will work with your system (Reasoning models above 14b parameters are bare minimum for small blueprint graphs)
- Click the model and download appropriate quantization (Q4_K_M for good quality/size balance)
	- If you're not sure which one to select, then follow LM Studio's recommendations as it will try to auto-detect what your system can run.
- Once you have finished downloading the model, click the `Select a model to load` button at the top-center, and then toggle on `Manually choose model load parameters` at the bottom of that dialogue.
- When you click on the model you want to load, make sure you set the context length to at least 8000 tokens -- the higher you can do the better as this will determine how many blueprint nodes you can translate in a single request.
- Click the `Remember settings for ...` checkbox at the bottom left so you don't have to set this again.
- You have to click the `Load Model` button for the `Remember settings for ...` setting to save.

### 3. Start the LM Studio Server
- **Option A**: Through the LM Studio app's UI
	- Make sure you have `Power User` enabled at the bottom left of the window.
	- Go to the `Developer` page (Green button)
	- Click the toggle at the top left to start the server if it is not already started. You should see `Status: Running`
	- Click the `Settings` button to the right of that toggle and make sure `Just-In-Time Model Loading` is enabled
	- Make sure your local server address is `http://127.0.0.1:1234` at the top right of the window.
- **Option B**: Use CLI: `lms server start` (requires CLI bootstrap: `~/.lmstudio/bin/lms bootstrap`)

> [!NOTE]
> Default server will run on `http://localhost:1234`

### 4. Configure NodeToCode
- In the Unreal Engine Editor, go to **Edit → Project Settings → Plugins → Node to Code**
- Set **LLM Provider** to "LM Studio"
- Configure **LM Studio Settings**:
  - **Model Name**: Enter the exact model name (e.g., "qwen3-32b") that you see in the `Models` (Red button) page of LM Studio
	  - You can also click the `...` button to the right of the model and click `Copy Default Identifier` and paste it into the `Model Name` field in the Plugin settings
  - **Server Endpoint**: `http://localhost:1234` (default)
  - **Prepended Model Command**: Optional commands like `/no_think` for faster responses with Qwen3 at the expense of potential translation quality degradation

### 5. Test Your Setup
- Load any Blueprint in the Blueprint Editor
- Click the **Node to Code** button in the toolbar
- Select **"Translate Blueprint"**
- Your translation will be processed locally using LM Studio!

### Additional Resources
- [LM Studio Documentation](https://lmstudio.ai/docs) - Official setup guides and API reference
- [LM Studio CLI Guide](https://lmstudio.ai/docs/cli) - Command-line interface documentation
