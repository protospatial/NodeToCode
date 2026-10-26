// Copyright (c) 2026 Nick McClure (Protospatial)
// SPDX-License-Identifier: Apache-2.0

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Tests/N2CTestHelpers.h"
#include "MCP/Tools/N2CMcpToolReference.h"
#include "MCP/Tools/N2CMcpToolRegistry.h"
#include "Core/N2CUserSettings.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

/**
 * NodeToCode.MCP.ToolReference (card 0179): the reference the N2CToolReference commandlet writes lists every registered
 * tool once with its arguments, is deterministic, and the committed Context/mcp-server files are what the registry
 * builds now, so adding, removing or changing a tool without regenerating them fails here.
 */
BEGIN_DEFINE_SPEC(FN2CMcpToolReferenceSpec, "NodeToCode.MCP.ToolReference", N2C_TEST_FLAGS)
	TArray<FMcpToolDefinition> Tools;
	TArray<FMcpToolDefinition> MetaTools;
END_DEFINE_SPEC(FN2CMcpToolReferenceSpec)

void FN2CMcpToolReferenceSpec::Define()
{
	BeforeEach([this]()
	{
		Tools = N2CMcpToolReference::CollectTools();
		MetaTools = N2CMcpToolReference::CollectMetaTools();
	});

	It("lists every registered tool once, sorted by name", [this]()
	{
		int32 Registered = 0;
		for (const TSharedPtr<IN2CMcpTool>& Tool : FN2CMcpToolRegistry::Get().GetTools())
		{
			Registered += Tool.IsValid() ? 1 : 0;
		}
		TestEqual(TEXT("tool count"), Tools.Num(), Registered);
		for (int32 Index = 1; Index < Tools.Num(); ++Index)
		{
			TestTrue(FString::Printf(TEXT("%s sorts before %s"), *Tools[Index - 1].Name, *Tools[Index].Name),
				Tools[Index - 1].Name.Compare(Tools[Index].Name, ESearchCase::CaseSensitive) < 0);
		}
		TArray<FString> MetaNames;
		for (const FMcpToolDefinition& Definition : MetaTools)
		{
			MetaNames.Add(Definition.Name);
		}
		TestEqual(TEXT("tool-search meta-tools"), FString::Join(MetaNames, TEXT(",")), FString(TEXT("call_tool,describe_toolset,list_toolsets")));
	});

	It("builds byte-identical text twice", [this]()
	{
		TestEqual(TEXT("json"), N2CMcpToolReference::BuildJson(Tools, MetaTools),
			N2CMcpToolReference::BuildJson(N2CMcpToolReference::CollectTools(), N2CMcpToolReference::CollectMetaTools()));
		TestEqual(TEXT("markdown"), N2CMcpToolReference::BuildMarkdown(Tools, MetaTools),
			N2CMcpToolReference::BuildMarkdown(N2CMcpToolReference::CollectTools(), N2CMcpToolReference::CollectMetaTools()));
		TestFalse(TEXT("json has no CR"), N2CMcpToolReference::BuildJson(Tools, MetaTools).Contains(TEXT("\r")));
	});

	It("gives each tool its toolset, arguments from the input schema and default enablement", [this]()
	{
		const TSharedPtr<FJsonObject> Root = N2CTest::ParseJsonObject(N2CMcpToolReference::BuildJson(Tools, MetaTools));
		if (!TestTrue(TEXT("json parses"), Root.IsValid()))
		{
			return;
		}
		TestEqual(TEXT("toolCount"), static_cast<int32>(Root->GetNumberField(TEXT("toolCount"))), Tools.Num());
		const TArray<TSharedPtr<FJsonValue>>& Entries = Root->GetArrayField(TEXT("tools"));
		if (!TestEqual(TEXT("tools entries"), Entries.Num(), Tools.Num()))
		{
			return;
		}
		for (int32 Index = 0; Index < Tools.Num(); ++Index)
		{
			const FMcpToolDefinition& Definition = Tools[Index];
			const TSharedPtr<FJsonObject> Entry = Entries[Index]->AsObject();
			TestEqual(TEXT("name"), Entry->GetStringField(TEXT("name")), Definition.Name);
			TestEqual(Definition.Name + TEXT(" toolset"), Entry->GetStringField(TEXT("toolset")),
				Definition.Toolset ? FString(Definition.Toolset->Name) : FString());
			TestEqual(Definition.Name + TEXT(" longRunning"), Entry->GetBoolField(TEXT("longRunning")), Definition.bIsLongRunning);

			const bool bPythonExecution = UN2CUserSettings::IsPythonExecutionTool(Definition.Name);
			TestEqual(Definition.Name + TEXT(" defaultEnabled"), Entry->GetBoolField(TEXT("defaultEnabled")),
				Definition.Toolset && Definition.Toolset->bDefaultEnabled && !bPythonExecution);

			TArray<FString> Expected;
			const TSharedPtr<FJsonObject>* Properties = nullptr;
			if (Definition.InputSchema.IsValid() && Definition.InputSchema->TryGetObjectField(TEXT("properties"), Properties))
			{
				for (const auto& Pair : (*Properties)->Values)
				{
					Expected.Add(FString(Pair.Key));
				}
			}
			TArray<FString> Listed;
			for (const TSharedPtr<FJsonValue>& Argument : Entry->GetArrayField(TEXT("arguments")))
			{
				const TSharedPtr<FJsonObject> Row = Argument->AsObject();
				Listed.Add(Row->GetStringField(TEXT("name")));
				TestTrue(Definition.Name + TEXT(" argument has a type"), !Row->GetStringField(TEXT("type")).IsEmpty());
			}
			TestEqual(Definition.Name + TEXT(" arguments"), FString::Join(Listed, TEXT(",")), FString::Join(Expected, TEXT(",")));
		}
	});

	It("matches the committed reference files in Context/mcp-server", [this]()
	{
		const FString Dir = N2CMcpToolReference::DefaultOutputDir();
		const FString JsonPath = Dir / N2CMcpToolReference::JsonFileName;
		if (!FPaths::FileExists(JsonPath))
		{
			// A packaged plugin has no Context folder; a checkout (or a build.ps1 host junctioned to one) always has
			AddWarning(FString::Printf(TEXT("%s not found: not a NodeToCode checkout, nothing to compare"), *JsonPath));
			return;
		}
		const TPair<const TCHAR*, FString> Files[] = {
			{ N2CMcpToolReference::JsonFileName, N2CMcpToolReference::BuildJson(Tools, MetaTools) },
			{ N2CMcpToolReference::MarkdownFileName, N2CMcpToolReference::BuildMarkdown(Tools, MetaTools) } };
		for (const TPair<const TCHAR*, FString>& File : Files)
		{
			FString Committed;
			const FString Path = Dir / File.Key;
			if (!TestTrue(FString::Printf(TEXT("%s loads"), *Path), FFileHelper::LoadFileToString(Committed, *Path)))
			{
				continue;
			}
			Committed.ReplaceInline(TEXT("\r\n"), TEXT("\n"));
			TestTrue(FString::Printf(TEXT("%s is current: a tool changed without running Scripts/generate_tool_reference.py"), *Path),
				Committed == File.Value);
		}
	});
}

#endif // WITH_DEV_AUTOMATION_TESTS
