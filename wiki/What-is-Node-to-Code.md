### Getting Starting → What is Node to Code?
***


Node to Code is a powerful Unreal Engine plugin that transforms your Blueprint event graphs, functions, macros, composite graphs, and more into contextual C++ code through tightly integrated UE Blueprint editor systems and LLM-powered translation. By leveraging state-of-the-art cloud-based _or_ local LLMs of **_your_** choosing, it offers an intuitive path to harness the iteration speeds and system visualization of Blueprint scripting while maintaining the readability, portability, and performance benefits of C++.

## Core Purpose

The plugin serves as a translation layer between Unreal Engine's visual Blueprint system and text-based code. It analyzes your Blueprint graphs—including their node/pin metadata, execution flows, data connections, and even [nested user-created graphs](Translation-Depth)—to faithfully reproduce corresponding UE C++ code while still aiming to maintain reusability in your Blueprints. Node to Code is intended to _**empower**_ Unreal's Blueprint scripting, _not_ replace it.

## Key Benefits

### Enhanced Readability

- Convert unwieldy or complex Blueprint graphs into clean, structured code/pseudocode that's easier to review, understand, and maintain
- Make your game systems more maintainable and easier to review
- Facilitate better documentation and version control through text-based representations

### Debugging & Collaboration

- Share logic through code instead of Blueprint screenshots or specialized Blueprint sharing platforms
- Enable easier peer reviews and collaboration
- Integrate with existing code review workflows and tools
- Provide clear text-based context when debugging issues or discussing with AI assistants

### Learning Unreal C++

- See how your Blueprint systems translate to actual Unreal Engine C++ code
- Learn best practices through AI-assisted translations
- Bridge the knowledge gap between visual and text-based programming
- Use generated code as a foundation for learning more advanced C++ concepts

### Performance Optimization

- Quickly translate heavier Blueprint systems into their C++ counterparts, ultimately reducing game thread overhead
- Maintain the logic of your proven Blueprint implementations while gaining native code performance
- Identify opportunities for optimization during the translation process

## How It Works

1. **Node & Pin Collection**
    - Analyzes the current graph and any nested graphs
    - Identifies all nodes and pins alongside their types
    - Maps connections between nodes and their pins
    - Captures node and pin metadata alongside comments that have been added to them

2. **JSON Serialization**
    - Converts the collected node and pin data into a custom schema that is about 50-70% smaller than UE's standard Blueprint text format
    - Organizes execution and data flows
    - Includes relevant type information
    - Preserves node relationships and graph hierarchies

3. **LLM Processing**
    - Sends the serialized data to your configured [LLM provider](Provider-Configuration) (Anthropic, OpenAI, Gemini, DeepSeek, or Ollama), enforcing a structured output according to the aforementioned schema
    - Includes any [reference source files](Reference-Source-Files) files you've configured in the plugin settings.
    - Waits for the LLM to process and return the translation in the structured output required for successful deserialization

4. **Parsing, Deserialization, and Serving**
    - Parses the structured output from the LLM into separate graph objects and associated implementation notes
    - Deserializes all graphs into a N2CBlueprint USTRUCT

5. **Code Preview**
    - The N2CBlueprint is then parsed and served via the Node to Code's [Editor UI](Opening-the-Editor), complete with syntax highlighting, implementation notes, graph browsing, and theming options


    <p align="left">
        <img src="https://github.com/protospatial/NodeToCode/blob/main/assets/Image_NodeToCode_BlueprintTranslation.jpg" alt="Node to Code Blueprint on left, UI with code translation on right" width="1250">
    </p>

## Integration with Your Workflow

Node to Code is designed to compliment—not replace—your existing Unreal Engine development process. It allows you to:

- Still prototype systems quickly in Blueprints
- Quickly translate proven Blueprint functionality into their own C++ powered Blueprint functions when needed
- Learn Unreal Engine C++ patterns through your own Blueprint translations
- Maintain a balance between Blueprint flexibility and C++ performance

## Beyond C++

While C++ is the primary target language for Blueprint translation, Node to Code supports multiple target output programming languages to adapt to your needs. These can be configured in [Target Language Settings](Target-Language-Settings):

- C++ (Primary target for Unreal Engine integration)
- C# (Tool development or cross-engine compatibility)
- Python (For tool development or external processing)
- JavaScript (Web-based tools or external systems)
- Swift (VisionOS or iOS native code integrations)

## A Note on Expectations

While Node to Code significantly accelerates the translation process, it's important to understand that it is _**not**_ intended to completely replace all proper coding practices. The generated code often requires review and [proper integration](Copy-&-Integrate) into your project's architecture, especially when using less-capable LLMs. Think of it as an intelligent assistant that handles the heavy lifting of the typical Blueprint -> C++ workflow:

- Tedious, back-and-forth logic comparison between UE editor and IDE
- _Meticulous_ UE C++ API cross-referencing
- Digging through countless (potentially unrelated or dead-end) forum posts
- Remembering every instance of UE-specific C++ syntax and macros
- Duplicating the same comments you already made in your blueprints

Ultimately allowing you to focus on code quality and optimization.

**Node to Code's effectiveness can vary based on:**

- The [complexity](Handling-Large-Graphs) of your Blueprint graphs
- The chosen [LLM provider and model](Choosing-an-LLM-Provider)
- The target programming language
- The inclusion of reference source files for context

By understanding these factors and setting appropriate expectations, you can make the most of Node to Code's capabilities while maintaining high standards for your project's code quality. For more details on improving contextual code output using existing project code, see [Reference Source Files](Reference-Source-Files).
