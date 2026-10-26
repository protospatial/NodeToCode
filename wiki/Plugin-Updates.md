### Plugin Updates → Plugin Updates
***

How to update Node to Code from the Epic Games Launcher or by hand, and what to check afterwards. Moving a project
from 1.2.x to 2.0 has its own page: [Upgrading from 1.x](Upgrading-from-Version-1).

## Updates via Epic Games Launcher

1. Open Epic Games Launcher
2. Navigate to Library → Vault
3. Look for available updates to Node to Code
4. Click Update if available
5. Restart Unreal Editor after updating

## Manual Updates

When updating a manual installation:

1. Close the Unreal Editor
2. Backup your existing NodeToCode folder
3. Delete the old plugin files
4. Copy the new version into place
5. Restart the editor

## Post-Update Tasks

After any update:

1. Verify plugin settings are preserved
2. Re-check LLM provider configuration
3. Test a simple translation to ensure functionality
4. Review the [changelog](https://github.com/protospatial/NodeToCode/blob/dev-2.0/CHANGELOG.md) for any breaking changes

## Updating from 1.2.x to 2.0

The first time the editor opens a 1.2.x project with NodeToCode 2.0, it upgrades the settings, moves the API keys
into Windows Credential Manager and says what it did in the **Output Log**. Every 1.2.x setting and what happens to
it is listed in [Upgrading from 1.x](Upgrading-from-Version-1).

## Troubleshooting Update Issues

If you encounter problems after updating:

1. Check the Output Log for error messages
2. Verify plugin compatibility with your UE version
3. Ensure all plugin files were properly updated
4. Try disabling and re-enabling the plugin
5. Consider reverting to the backup if issues persist

Remember to always backup your project before performing plugin updates, and test the updated plugin thoroughly in a development environment before using it in production.