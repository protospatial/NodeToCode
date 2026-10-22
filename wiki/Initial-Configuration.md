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

Node to Code utilizes two separate Config files:
- DefaultNodeToCode.ini
  - Stores all of your modified non-sensitive plugin settings
  - Saved in your Project's **Config** path
  - Can be committed to source control without leaking API Keys

- NodeToCodeSecrets.ini
  - Stores your LLM service provider API keys
  - Saved in your Project's **Saved** path
  - Should be ignored by default when using a properly configured UE .gitignore or .p4ignore