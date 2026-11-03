// Copyright (c) 2026 Nick McClure (Protospatial)
// SPDX-License-Identifier: Apache-2.0

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Tests/N2CTestHelpers.h"
#include "MCP/Resources/Implementations/N2CMcpAppResources.h"
#include "MCP/Server/N2CMcpJsonRpcTypes.h"
#include "MCP/Tools/N2CMcpToolRegistry.h"
#include "MCP/Tools/N2CMcpToolBase.h"
#include "MCP/Tools/N2CMcpToolTypes.h"
#include "Dom/JsonObject.h"

/**
 * NodeToCode's MCP Apps (card 0213): the ui:// HTML resources and the tools that name them.
 * - translate-focused-blueprint and get-focused-blueprint send _meta.ui.resourceUri in tools/list, naming
 *   ui://nodetocode/translation-viewer and ui://nodetocode/graph-preview;
 * - resources/read of each returns the MCP Apps MIME type and self-contained HTML: no remote script, style, font or
 *   fetch, the MCP Apps handshake (ui/initialize, ui/notifications/initialized) and the tool-result listener, and
 *   untrusted text set through textContent (no innerHTML);
 * - an unknown ui:// URI is ResourceNotFound.
 */
BEGIN_DEFINE_SPEC(FN2CMcpAppsSpec, "NodeToCode.MCP.Apps", N2C_TEST_FLAGS)

	FString ResourceUriOf(const TCHAR* ToolName)
	{
		// The registry's own tools (REGISTER_MCP_TOOL): the tool manager is empty while the server is not started
		const TSharedPtr<IN2CMcpTool>* Tool = FN2CMcpToolRegistry::Get().GetTools().FindByPredicate(
			[ToolName](const TSharedPtr<IN2CMcpTool>& Candidate) { return Candidate.IsValid() && Candidate->GetDefinition().Name == ToolName; });
		if (!TestTrue(FString::Printf(TEXT("%s is registered"), ToolName), Tool != nullptr))
		{
			return FString();
		}
		const TSharedPtr<FJsonObject> Json = (*Tool)->GetDefinition().ToJson();
		const TSharedPtr<FJsonObject>* Meta = nullptr;
		const TSharedPtr<FJsonObject>* Ui = nullptr;
		FString Uri;
		if (Json.IsValid() && Json->TryGetObjectField(TEXT("_meta"), Meta) && (*Meta)->TryGetObjectField(TEXT("ui"), Ui))
		{
			(*Ui)->TryGetStringField(TEXT("resourceUri"), Uri);
		}
		return Uri;
	}

	void ExpectSelfContainedApp(const TCHAR* Uri)
	{
		const FMcpResourceContents Contents = FN2CMcpAppResources::Read(Uri);
		const FString What(Uri);
		if (!TestFalse(What + TEXT(" reads (Content/MCPApps is in the plugin)"), Contents.IsError()))
		{
			AddError(Contents.ErrorMessage);
			return;
		}
		TestEqual(What + TEXT(" has the MCP Apps MIME type"), Contents.MimeType, FString(TEXT("text/html;profile=mcp-app")));
		const FString& Html = Contents.Text;
		TestTrue(What + TEXT(" is an HTML document"), Html.StartsWith(TEXT("<!doctype html>"), ESearchCase::IgnoreCase));
		TestTrue(What + TEXT(" starts the MCP Apps handshake"), Html.Contains(TEXT("\"ui/initialize\"")) && Html.Contains(TEXT("\"ui/notifications/initialized\"")));
		TestTrue(What + TEXT(" renders ui/notifications/tool-result"), Html.Contains(TEXT("\"ui/notifications/tool-result\"")));
		// Self-contained: the host's default CSP allows no remote origin, and the app needs none
		for (const TCHAR* Remote : { TEXT("<script src"), TEXT("<link"), TEXT("@import"), TEXT("url("), TEXT("fetch("), TEXT("XMLHttpRequest"), TEXT("https://") })
		{
			TestFalse(FString::Printf(TEXT("%s has no %s"), Uri, Remote), Html.Contains(Remote));
		}
		// The rendered text comes from the Blueprint (untrusted): it goes in as text, never as markup
		TestFalse(What + TEXT(" never sets innerHTML"), Html.Contains(TEXT("innerHTML")));
		TestTrue(What + TEXT(" sets text with textContent"), Html.Contains(TEXT("textContent")));
	}

END_DEFINE_SPEC(FN2CMcpAppsSpec)

void FN2CMcpAppsSpec::Define()
{
	It(TEXT("names the translation viewer from translate-focused-blueprint and the graph preview from get-focused-blueprint"), [this]()
	{
		TestEqual(TEXT("translate-focused-blueprint's _meta.ui.resourceUri"), ResourceUriOf(TEXT("translate-focused-blueprint")),
			FString(FN2CMcpAppResources::TranslationViewerUri));
		TestEqual(TEXT("get-focused-blueprint's _meta.ui.resourceUri"), ResourceUriOf(TEXT("get-focused-blueprint")),
			FString(FN2CMcpAppResources::GraphPreviewUri));
	});

	It(TEXT("lists both apps with the MCP Apps MIME type"), [this]()
	{
		const TArray<FMcpResourceDefinition> Apps = FN2CMcpAppResources::GetDefinitions();
		TestEqual(TEXT("two apps"), Apps.Num(), 2);
		for (const FMcpResourceDefinition& App : Apps)
		{
			TestTrue(App.Uri + TEXT(" is a ui:// URI"), App.Uri.StartsWith(TEXT("ui://nodetocode/")));
			TestEqual(App.Uri + TEXT(" MIME type"), App.MimeType, FString(FN2CMcpAppResources::MimeType));
		}
	});

	It(TEXT("serves the translation viewer as self-contained HTML"), [this]()
	{
		ExpectSelfContainedApp(FN2CMcpAppResources::TranslationViewerUri);
	});

	It(TEXT("serves the graph preview as self-contained HTML"), [this]()
	{
		ExpectSelfContainedApp(FN2CMcpAppResources::GraphPreviewUri);
	});

	It(TEXT("answers an unknown ui:// URI with ResourceNotFound"), [this]()
	{
		const FMcpResourceContents Missing = FN2CMcpAppResources::Read(TEXT("ui://nodetocode/no-such-app"));
		TestEqual(TEXT("ResourceNotFound"), Missing.ErrorCode, static_cast<int32>(JsonRpcErrorCodes::ResourceNotFound));
	});
}

#endif // WITH_DEV_AUTOMATION_TESTS
