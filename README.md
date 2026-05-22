<p align="center">
    <img src="https://github.com/protospatial/NodeToCode/blob/main/assets/Image_NodeToCode_Header_Docs.png" alt="Node to Code header" width="800">
</p>

# Transform Blueprints to Code in Seconds. Not Hours.

**Node to Code** transforms your Unreal Engine Blueprint graphs into clean, structured C++ with a single click. Stop spending hours on tedious manual conversions, struggling to explain complex visual logic, or navigating sprawling Blueprint systems. This LLM-powered plugin bridges the Blueprint-to-code gap, whether you're optimizing performance, improving collaboration, or learning/teaching the Unreal Engine C++ API.

<p align="center">
    <img src="https://github.com/protospatial/NodeToCode/blob/main/assets/Image_NodeToCode_BlueprintTranslation.gif" alt="Node to Code translate gif" width="800">
</p>

## 🔍 Why Node to Code?

### 💬 **Blueprint Communication Solved**
- 🆕 Support for Claude 4, Gemini 2.5 Flash, & OpenAI's latest models
- 🆕 Support for LM Studio -> [Quick Start Guide](https://github.com/protospatial/NodeToCode/wiki/LM-Studio-Quick-Start)
- 🆕 **Complete Blueprint Translation:** Translate entire Blueprints with all graphs, variables, and components at once
- 🆕 **Variable & Component Support:** Full support for Blueprint variables, local variables, and component overrides
- **Pseudocode Translation:** Convert Blueprints into universally understandable pseudocode
- **Automatic Translation Saving:** Each translation is saved locally for easy archiving & sharing via chat or docs
- Share complete Blueprint logic as text in forums, chat, emails, or documentation
- No more endless screenshot chains or lengthy meetings to explain systems
- Perfect for remote teams, forum help requests, and collaborating with AI assistants

### 🧭 **Navigate Complex Blueprint Architectures**
- Transform sprawling node networks into clear, structured text
- Capture entire Blueprint hierarchies with configurable depth (up to 5 levels deep)
- Spot patterns and relationships that are hidden in visual spaghetti

### 🧠 **Bridge Blueprint to C++ Knowledge**
- See your own Blueprint logic translated to proper Unreal C++
- Learn from implementation notes that explain key concepts and patterns
- Use your existing code as reference to influence translation style
- Compare multiple language outputs (C#, JavaScript, Python, Swift) to strengthen programming fundamentals

### ⚡ **Performance When You Need It**
- Quickly translate performance-critical Blueprint systems to C++
- Keep prototyping rapidly in Blueprint, knowing you can convert bottlenecks later
- Avoid the tedious manual conversion process that takes hours or days

### 📚 **Built-in Documentation & Knowledge Preservation**
- Create searchable text archives of your Blueprint systems
- Preserve design decisions in formats accessible to everyone on your team
- Generate code snippets for technical documentation or requirements

## 🔧 Under the Hood

- **Blueprint Analysis:** Captures your entire Blueprint graph structure, including execution flows, data connections, variable references, and comments
- **Complete Blueprint Translation:** Translate entire Blueprints with all graphs, variables, and components in a single operation
- **Variable & Component Support:** Full support for Blueprint-level variables, local variables, and component property overrides
- **Multiple LLM Options:** Use cloud providers (OpenAI, Anthropic Claude, Google Gemini, DeepSeek) or run 100% locally via Ollama for complete privacy
- **Efficient Serialization:** Converts blueprints into a bespoke JSON schema that reduces token usage by 60-90% compared to UE's verbose Blueprint text format
- **Style Guidance:** Supply your own C++ files as reference to maintain your project's coding standards and patterns
- **Integrated Editor Experience:** Review translations in a dockable Unreal editor window with syntax highlighting, implementation notes, & theming

## 👥 Value for Your Entire Team

- **Blueprint Creators:** Communicate systems clearly and gradually learn C++ through your own work
- **C++ Programmers:** Understand designer intent without deciphering complex visual graphs
- **Project Leads:** Improve team communication and maintain better system documentation
- **Educators & Students:** Bridge the visual-to-code learning gap with real examples

## 🆕 Recent Updates

### Complete Blueprint Translation
- **Translate Entire Blueprint:** New toolbar command to translate all graphs in a Blueprint at once
- **Batch Processing:** Each graph generates independent JSON and LLM requests, all sharing the same root directory
- **Error Feedback:** Comprehensive success/failure reporting for batch translations

### Enhanced Variable & Component Support
- **Blueprint Variables:** Full support for Blueprint-level variables with complete type mapping (including Array, Set, Map)
- **Local Variables:** Function-level local variable collection and serialization
- **Component Overrides:** Automatic detection and serialization of component property overrides
- **Component Hierarchy:** Support for component parent-child relationships
- **Map Key Types:** Improved Map key type extraction from Blueprint pin types

### ClassItSelf Graph Type
- **Class Structure Marking:** New graph type for representing class skeleton structure
- **C++ Class Generation:** Enables LLM to generate proper C++ class declarations, constructors, and member variables
- **Conditional Creation:** Automatically created when variables or components are present

### Code Generation Improvements
- **Enhanced Prompts:** Significantly expanded CodeGen_CPP.md with detailed guidance for components, variables, class structures, event handling, and network replication
- **Better Type Mapping:** Improved handling of complex Unreal Engine types

## 🏃Get Started

### :arrow_down: [Install the Plugin](https://github.com/protospatial/NodeToCode/releases)
Check out the Releases page for the latest stable builds of Node to Code - ready to install in your engine or project

### :books: [Visit the Wiki](https://github.com/protospatial/NodeToCode/wiki)
Explore the documentation, including setup guides, best practices, and troubleshooting steps

### 🆕 [See What's New](https://github.com/protospatial/NodeToCode/wiki/Latest-Updates)
Read the latest release notes

### 🛣️ [See What's Ahead](https://trello.com/b/iPOyaSvb)
Get excited about what's on the roadmap!

---

### :speech_balloon: [Discord Community](https://discord.gg/4t3Syvk4AG)
Have questions or need help? Join the Discord for support and discussion.

### 🤝 Support My Work
- ☕ [Buy me a coffee](https://buymeacoffee.com/protospatial)
- 🧡 [Sponsor me on GitHub](https://github.com/sponsors/NCMcClure)
- 🏪 [Get the plugin on Fab](https://www.fab.com/listings/29955a71-cd04-4111-ac43-6a0264429ce6)
