### Plugin Updates → Plugin Updates
***

How to update Node to Code from the Epic Games Launcher or by hand, what to check afterwards, and what changes in
your project the first time NodeToCode 2.0 opens a project set up with 1.2.x.

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

The first time the editor opens a project with NodeToCode 2.0, it updates what 1.2.x left in the project once and
says what it did in the **Output Log** (category **LogNodeToCode**). The next start logs none of it.

| What 1.2.x left | What 2.0 does |
|---|---|
| A model 2.0 no longer offers (every 1.2.x Claude model, the older OpenAI and Gemini models) | Selects the provider's current default and saves it to `Config/DefaultNodeToCode.ini`, with one warning |
| A value 2.0 does not have (the Gemini 2.5 Pro Experimental selection, a price list naming it) | Uses the 2.0 value, saves it, and names the old value in a warning |
| Pricing entries for retired models, or still at the 1.2.x prices | Removes them so current prices apply; prices you entered for current models stay |
| Custom code editor themes per language | Not used: 2.0 has one theme for every language, under **Project Settings > Plugins > Node to Code > Theming > Code Editor Theme**. A warning names the old entries, which stay in `Config/DefaultNodeToCode.ini` so you can copy the colours |
| `HttpConnectionTimeout=300` and `HttpActivityTimeout=3600` under `[HTTP]` in `Config/DefaultEngine.ini` | Removes these two lines; other values there are yours and stay. NodeToCode 2.0 sets its timeouts per request |
| **Use Less CPU when in Background** turned off | Leaves it as it is, since it cannot tell whether 1.2.x or you turned it off, and says so once. Turn it back on in **Editor Preferences > General > Performance** if you want it |

If `Config/DefaultNodeToCode.ini` or `Config/DefaultEngine.ini` is read-only (checked in to source control), the
log says so and the update runs again on the next start; check the files out first.

## Troubleshooting Update Issues

If you encounter problems after updating:

1. Check the Output Log for error messages
2. Verify plugin compatibility with your UE version
3. Ensure all plugin files were properly updated
4. Try disabling and re-enabling the plugin
5. Consider reverting to the backup if issues persist

Remember to always backup your project before performing plugin updates, and test the updated plugin thoroughly in a development environment before using it in production.