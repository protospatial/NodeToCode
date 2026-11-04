// Copyright (c) 2026 Nick McClure (Protospatial)
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "CoreMinimal.h"
#include "IPropertyTypeCustomization.h"
#include "LLM/N2CLLMTypes.h"
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

/**
 * Project Settings > Plugins > Node to Code > LLM Services > <Provider> (card 0488): draws the Saved Key row, a
 * UN2CSettings FN2CApiKeyRemover property, as a Remove Saved <Provider> Key button. The button is enabled while a key
 * is saved for the provider (not one from the environment), removes it through UN2CSettings::SetApiKey (empty key) and
 * says so in a notification. Registered beside FN2CSettingsCustomization.
 */
class FN2CApiKeyRemoverCustomization : public IPropertyTypeCustomization
{
public:
	static TSharedRef<IPropertyTypeCustomization> MakeInstance();

	virtual void CustomizeHeader(TSharedRef<IPropertyHandle> PropertyHandle, FDetailWidgetRow& HeaderRow,
		IPropertyTypeCustomizationUtils& CustomizationUtils) override;
	virtual void CustomizeChildren(TSharedRef<IPropertyHandle> PropertyHandle, IDetailChildrenBuilder& ChildBuilder,
		IPropertyTypeCustomizationUtils& CustomizationUtils) override {}

	/** The button's label: "Remove Saved <Provider> Key" */
	static FText GetButtonLabel(EN2CLLMProvider Provider);

	/** Whether there is a saved key to remove (the store holds one; an environment key is not removable here) */
	static bool CanRemove(EN2CLLMProvider Provider);
};

/**
 * Project Settings > Plugins > Node to Code > LLM Provider (card 0216): draws the Model List row, the property
 * UN2CSettings::ModelListRefresher, as the active provider's list status (FN2CModelDiscovery::DescribeActiveStatus)
 * and a Refresh Model List button. The button is enabled when the active provider is in Live Model Lists, no fetch for
 * it is running and the session is not -unattended; it calls FN2CModelDiscovery::RequestActiveRefresh, which says
 * what it got (or why it fetched nothing) in a notification. Registered beside FN2CSettingsCustomization.
 */
class FN2CModelListRefresherCustomization : public IPropertyTypeCustomization
{
public:
	static TSharedRef<IPropertyTypeCustomization> MakeInstance();

	virtual void CustomizeHeader(TSharedRef<IPropertyHandle> PropertyHandle, FDetailWidgetRow& HeaderRow,
		IPropertyTypeCustomizationUtils& CustomizationUtils) override;
	virtual void CustomizeChildren(TSharedRef<IPropertyHandle> PropertyHandle, IDetailChildrenBuilder& ChildBuilder,
		IPropertyTypeCustomizationUtils& CustomizationUtils) override {}

	/** Whether Refresh Model List can run now */
	static bool CanRefresh();
};
