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

## Supported Engine Versions

| Node to Code version | Unreal Engine versions |
|----------------------|------------------------|
| 2.0 | 5.6, 5.7, 5.8 |
| 1.2.x | 5.3, 5.4, 5.5 (the [v1.2.3 GitHub release](https://github.com/protospatial/NodeToCode/releases/tag/v1.2.3) ships a build for each) |

> [!NOTE]
> On Fab, only the engine versions listed on the Fab page are offered. 2.0 does not build on Unreal Engine 5.5 or older; on those engines, stay on 1.2.x.

## Supported Platforms

Node to Code 2.0.0-beta.1 is an editor plugin for **Windows (Win64)** only. 1.2.x also listed Mac, but no Mac build of 2.0 has been verified yet, so 2.0 does not load on Mac; Mac users should stay on 1.2.x for now. Mac will be listed again, and Linux added, once a build on that platform has been verified.

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
    ├── Binaries/
    ├── LICENSE
    ├── Config/
    ├── Content/
    ├── Resources/
    ├── Source/
    └── ThirdParty/
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