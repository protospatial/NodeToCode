### Plugin Setup → Initial Configuration
***

## Accessing Plugin Settings

1. Open Project Settings (Edit → Project Settings)
2. Navigate to Plugins → Node to Code
3. You'll see several categories of settings to configure

<p align="left">
    <img src="https://github.com/protospatial/NodeToCode/blob/main/assets/Image_NodeToCode_PluginSettings.jpg" alt="Node to Code plugin settings" width="750">
</p>

## Settings

1. [LLM Provider Setup](Provider-Configuration)

2. [Target Language Selection](Target-Language-Settings)

3. [Translation Depth Configuration](Max-Translation-Depth)

4. [Reference Source Files](Reference-Source-Files)

5. [Logging Configuration](Logging-Configuration)

6. [Theme Customization](Theme-&-Syntax-Highlighting)

## Plugins Config Files & Source Control

Node to Code uses two separate files:
- DefaultNodeToCode.ini
  - Stores all of your modified non-sensitive plugin settings
  - Saved in your Project's **Config** path
  - Can be committed to source control without leaking API Keys

- secrets.json
  - Stores your LLM service provider API keys, in plain text
  - Saved at `Saved/NodeToCode/User/secrets.json` in your project folder
  - **Keep it out of source control.** A standard UE .gitignore or .p4ignore that ignores the whole `Saved/` folder already covers it; if yours does not, add `Saved/NodeToCode/User/secrets.json` (or `Saved/`) to it, and never submit or share this file