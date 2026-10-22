<p align="center">
    <img src="https://github.com/protospatial/NodeToCode/blob/main/assets/Image_NodeToCode_Header_Docs.png" alt="Node to Code header" width="1250">
</p>

# Welcome to Node to Code!

First off, thank you for your interest in Node to Code to help accelerate your Unreal Engine development workflow. Whether you're a technical designer looking to quickly squeeze more performance from your existing Blueprint systems, a Blueprint wizard eager for a unique way to learn C++, or a professional UE developer seeking to spend less time on Blueprint-to-C++ translations, you're in the right place.

## About This Documentation

This guide is designed to help you get started with Node to Code, from basic setup to best practices. I've tried to organize it to be both an approachable knowledge base for newcomers and a useful reference for more experienced users.

### How to Use This Guide

- **Don't know what this is?** Check out [What is Node to Code?](What-is-Node-to-Code) and [Why I Made This](Why-I-Made-This) for a primer.
- **New to Node to Code?** Start with the [Plugin Installation](Plugin-Installation) and [Initial Configuration](Initial-Configuration) guides, then move on to [Translation Workflow](Accessing-and-Triggering-Translation).
- **Looking to optimize your workflow?** Check out the [Best Practices](Blueprint-Selection) section.
- **Want to fine-tune your setup?** Dive into [Plugin Settings](Provider-Configuration).
- **Need to troubleshoot?** Head to the [Troubleshooting](Translation-Fails-or-Hangs) section.

## Quick Start

If you're eager to jump in, here's the TL;DR version:

1. Install the plugin through your Epic Games Launcher -> Library
2. Ensure your project is a Code project. If it's not already, you can use Tools -> Add C++ Class via the editor to add an empty class to your project.
2. Configure your [preferred LLM provider](Choosing-an-LLM-Provider) via the Node to Code [Plugin settings](Initial-Configuration) found in Project settings
3. Open any Blueprint graph
4. Click the Node to Code toolbar button
5. Select "Translate Blueprint Graph to Code"

    <p align="left">
        <img src="https://github.com/protospatial/NodeToCode/blob/main/assets/Image_NodeToCode_Button_Translate.png" alt="Node to Code Blueprint on left, UI with code translation on right" width="512">
    </p>

That's it! After a few moments, your translated code should be ready for review in the [Node to Code Editor](Opening-the-Editor).

<p align="center">
    <img src="https://github.com/protospatial/NodeToCode/blob/main/assets/Image_NodeToCode_BlueprintTranslation.gif" alt="Node to Code translate button" width="1250">
</p>

## Getting Help

If you run into any issues or have questions:

- Check out the [Troubleshooting](Translation-Fails-or-Hangs) section
- Join the [Discord community](https://discord.gg/4t3Syvk4AG)

## Ready to Begin?

Let's start by getting Node to Code [installed and configured](Plugin-Installation) in your Unreal Engine project!

## About These Pages

This wiki is published from the `wiki/` folder of the [NodeToCode repository](https://github.com/protospatial/NodeToCode). To suggest a change, open a pull request against that folder. Edits made here in the browser are overwritten by the next publish.
