// Copyright (c) 2025 Nick McClure (Protospatial). All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "N2CUserSettings.generated.h"

class FConfigBranch;

/** One per-user switch as read by UN2CUserSettings::ResolvePerUserBool. */
struct FN2CPerUserBool
{
	/** The value the plugin uses. */
	bool bValue = false;

	/** True when the user's own layer (Saved/Config or a -ini: command-line override) set it. */
	bool bFromUserLayer = false;

	/** True when a project-shared layer (Default*, Base, platform, plugin) set a different value that was ignored. */
	bool bIgnoredSharedValue = false;
};

/**
 * Per-user Node to Code settings, saved to the user's Saved/Config/<Platform>Editor/EditorPerProjectUserSettings.ini.
 *
 * Unreal also reads the project's Config/DefaultEditorPerProjectUserSettings.ini (and the Base and platform files) as
 * lower layers of that config, so a committed file could set these properties for everyone who opens the project.
 * The security switches are therefore not read from the loaded properties: IsMcpServerEnabled() and
 * IsMcpPythonExecutionAllowed() take the value only from the user's own layers and fall back to the code default,
 * ignoring (with one warning) any value from a project-shared layer (card 0423).
 */
UCLASS(Config = EditorPerProjectUserSettings, meta = (DisplayName = "Node to Code (User)"))
class NODETOCODE_API UN2CUserSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	virtual FName GetCategoryName() const override { return TEXT("Plugins"); }

	/** Code defaults of the two security switches; a project-shared config layer cannot change them. */
	static constexpr bool bDefaultEnableMcpServer = true;
	static constexpr bool bDefaultAllowMcpPythonExecution = false;

	/** Start the MCP server when the editor starts. Requires an editor restart. Read through IsMcpServerEnabled(). */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "MCP Server",
		meta = (DisplayName = "Enable MCP Server", ConfigRestartRequired = true))
	bool bEnableMcpServer = bDefaultEnableMcpServer;

	/**
	 * Expose run-python, save-python-script and delete-python-script over MCP. Any connected MCP client can then
	 * run arbitrary Python in the editor and write script files. Requires an editor restart. Read through
	 * IsMcpPythonExecutionAllowed(), never directly: this property also holds values from project-shared layers.
	 */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "MCP Server",
		meta = (DisplayName = "Allow MCP Python Execution", ConfigRestartRequired = true,
			ToolTip = "Lets any connected MCP client run arbitrary Python in the editor and save or delete script files. Off by default, and only your own setting counts: a value in the project's Config folder is ignored. Requires an editor restart."))
	bool bAllowMcpPythonExecution = bDefaultAllowMcpPythonExecution;

	/**
	 * Whether the MCP server may start: the user's own Enable MCP Server value, or on when the user never set it.
	 * Read once per editor session (the setting needs a restart); a value from a project-shared layer is ignored.
	 */
	static bool IsMcpServerEnabled();

	/**
	 * Whether run-python, save-python-script and delete-python-script may be registered: the user's own Allow MCP
	 * Python Execution value, or off when the user never set it. A committed project file cannot turn it on.
	 */
	static bool IsMcpPythonExecutionAllowed();

	/**
	 * Reads Key in Section of Branch from the user's own layers only: the Saved/Config file (Branch->SavedLayer) and
	 * -ini: command-line overrides (Branch->CommandLineOverrides), the last one winning. Without either it returns
	 * bCodeDefault and reports whether the loaded value (from a project-shared or plugin layer) differed.
	 * A null Branch gives bCodeDefault. Public for the NodeToCode.Security.UserSettings spec.
	 */
	static FN2CPerUserBool ResolvePerUserBool(const FConfigBranch* Branch, const FString& Section, FName Key, bool bCodeDefault);

	/** Tools that only exist when bAllowMcpPythonExecution is on. */
	static bool IsPythonExecutionTool(const FString& ToolName)
	{
		return ToolName == TEXT("run-python") || ToolName == TEXT("save-python-script") || ToolName == TEXT("delete-python-script");
	}
};
