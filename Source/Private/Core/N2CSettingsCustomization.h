// Copyright (c) 2026 Nick McClure (Protospatial)
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "CoreMinimal.h"
#include "IPropertyTypeCustomization.h"
#include "MCP/Server/N2CMcpClientConfig.h"

/** What the Write MCP Client Config row in Project Settings writes: every client, one client, or the Claude Desktop snippet */
enum class EN2CClientConfigChoice : uint8
{
	All,
	ClaudeCode,
	Cursor,
	VSCode,
	Gemini,
	Codex,
	ClaudeDesktop,
	Num
};

/** What one click of Write MCP Client Config did */
struct FN2CClientConfigOutcome
{
	/** One entry per client file, as N2C.Mcp.WriteClientConfig returns them */
	TArray<FN2CMcpClientConfigResult> Results;

	/** The claude_desktop_config.json snippet, set when Claude Desktop was chosen (the button copies it) */
	FString DesktopSnippet;
};

/**
 * Project Settings > Plugins > Node to Code > MCP Server (card 0507): draws the MCP Client Config row, the property
 * UN2CSettings::McpClientConfigWriter (an empty FN2CMcpClientConfigWriter), as a client choice, a bearer token box and
 * a Write MCP Client Config button. The button runs the same code as the console command N2C.Mcp.WriteClientConfig
 * (FN2CMcpClientConfig::RunCommand) for the project folder, shows what it wrote in a notification, and for Claude
 * Desktop copies the snippet to the clipboard. Registered for the struct in NodeToCode.cpp.
 */
class FN2CSettingsCustomization : public IPropertyTypeCustomization
{
public:
	static TSharedRef<IPropertyTypeCustomization> MakeInstance();

	/** Registers this as FN2CMcpClientConfigWriter's property layout (module startup) */
	static void Register();

	/** Removes the layout (module shutdown) */
	static void Unregister();

	virtual void CustomizeHeader(TSharedRef<IPropertyHandle> PropertyHandle, FDetailWidgetRow& HeaderRow,
		IPropertyTypeCustomizationUtils& CustomizationUtils) override;
	virtual void CustomizeChildren(TSharedRef<IPropertyHandle> PropertyHandle, IDetailChildrenBuilder& ChildBuilder,
		IPropertyTypeCustomizationUtils& CustomizationUtils) override {}

	/** The choice's label in the client list */
	static FText GetChoiceLabel(EN2CClientConfigChoice Choice);

	/** The console command's arguments for a choice: the client name (or all, desktop), token or notoken, dir=<BaseDir> last */
	static TArray<FString> MakeCommandArgs(EN2CClientConfigChoice Choice, bool bWithToken, const FString& BaseDir);

	/** Runs the console command's code for Choice under BaseDir; the button passes the project folder */
	static FN2CClientConfigOutcome WriteClientConfig(EN2CClientConfigChoice Choice, bool bWithToken, const FString& BaseDir);

	/** The notification text for an outcome: each file written, unchanged or not written, and the token note */
	static FText Summarize(const FN2CClientConfigOutcome& Outcome, bool bWithToken);
};
