### Plugin Setup → Plugin Installation
***

## Use a Code Project

Regardless of whether you install via the launcher or manually, you'll want to make sure that the project you're installing the plugin to is a code project. If your project is not already a code project, then you can convert it to one by creating a C++ class via the Tools -> New C++ Class dialogue found in-editor.

## Epic Launcher Installation

1. Open the Epic Games Launcher
2. Navigate to the Library tab
3. Search for "Node to Code" in your library
4. Click "Install to Engine" and select your Unreal Engine version
5. Wait for the download and installation to complete

> [!NOTE]
> Currently only Unreal Engine versions 5.3+ are supported

## Manual Installation

If you are copying the plugin from your engine installation over to a project:

1. Locate your Unreal Engine installation's Plugins directory:
    
    ```
    <ue-install-path>/Engine/Plugins/Marketplace
    ```
    
2. Copy the NodeToCode plugin folder into your project's Plugin directory (create it if it doesn't exist):
    
    ```
    YourProject/Plugins/NodeToCode/
    ├── NodeToCode.uplugin
    ├── Resources/
    ├── Source/
    ├── Docs/
    └── Content/
    ```
    

## Verifying Installation

1. Launch your Unreal Engine project
2. From the main menu, select Edit → Plugins
3. Search for "Node to Code" in the plugins window
4. Ensure the plugin is enabled (checkbox is checked)
5. Restart the editor when prompted

    <p align="left">
        <img src="https://github.com/protospatial/NodeToCode/blob/main/assets/Image_NodeToCode_Plugins.png" alt="Node to Code code generation settings" width="600">
    </p>


## Post-Installation Check

After restarting, verify that:

- The Node to Code button [appears in Blueprint Editor toolbars](Accessing-and-Triggering-Translation)
- The Node to Code category exists in Project Settings under the Plugins category
- No error messages appear in the Output Log related to the plugin