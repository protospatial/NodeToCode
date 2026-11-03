### Plugin Settings → Target Language
***

## Target Language

Choose the output programming language:

- C++ (Default, for Unreal Engine integration)
- C# (Tries to steer towards Unity C#)
- Python (For external tool development)
- JavaScript (Web-based tools or external systems)
- Swift (VisionOS or iOS)
- Pseudocode (Great for communicating language-agnostic code!)

**Target Language** is in **Project Settings → Plugins → Node to Code → Code Generation**, beside
[Max Translation Depth](Max-Translation-Depth) and [Reference Source Files](Reference-Source-Files):

<img src="images/settings/code-generation.png" alt="Project Settings, Code Generation: Target Language (C++), Max Translation Depth, Estimated Reference File Tokens and Reference Source Files" width="760">

    <p align="left">
        <img src="https://github.com/protospatial/NodeToCode/blob/main/assets/Image_NodeToCode_TargetLanguages.png" alt="Node to Code header" width="400">
    </p>

> [!NOTE]
> Due to the nature of UE Blueprints, using other target
> languages outside of the default C++ can have more varying degrees
> of successful compilation and should be used more as learning
> or communication vessels rather than ground-truth code.