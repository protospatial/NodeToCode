// Copyright (c) 2025 Nick McClure (Protospatial). All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "N2CUserSettings.generated.h"

/**
 * Per-user Node to Code settings, stored in the user's EditorPerProjectUserSettings config (never in the
 * project's shared Config/DefaultNodeToCode.ini), so a committed project file cannot switch them on for
 * everyone who opens the project.
 */
UCLASS(Config = EditorPerProjectUserSettings, meta = (DisplayName = "Node to Code (User)"))
class NODETOCODE_API UN2CUserSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	virtual FName GetCategoryName() const override { return TEXT("Plugins"); }

	/** Start the MCP server (and its notification server) when the editor starts. Requires an editor restart. */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "MCP Server",
		meta = (DisplayName = "Enable MCP Server", ConfigRestartRequired = true))
	bool bEnableMcpServer = true;

	/**
	 * Expose run-python, save-python-script and delete-python-script over MCP. Any connected MCP client can then
	 * run arbitrary Python in the editor and write script files. Requires an editor restart.
	 */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "MCP Server",
		meta = (DisplayName = "Allow MCP Python Execution", ConfigRestartRequired = true,
			ToolTip = "Lets any connected MCP client run arbitrary Python in the editor and save or delete script files. Off by default. Requires an editor restart."))
	bool bAllowMcpPythonExecution = false;

	/** Tools that only exist when bAllowMcpPythonExecution is on. */
	static bool IsPythonExecutionTool(const FString& ToolName)
	{
		return ToolName == TEXT("run-python") || ToolName == TEXT("save-python-script") || ToolName == TEXT("delete-python-script");
	}
};
