// Copyright (c) 2026 Nick McClure (Protospatial)
// SPDX-License-Identifier: Apache-2.0

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Tests/N2CTestHelpers.h"
#include "MCP/Tools/IN2CMcpTool.h"
#include "MCP/Tools/N2CMcpToolManager.h"
#include "MCP/Tools/N2CMcpToolReference.h"
#include "MCP/Tools/N2CMcpToolRegistry.h"
#include "MCP/Tools/N2CMcpToolsets.h"
#include "MCP/Tools/N2CToolCatalog.h"
#include "Core/N2CUserSettings.h"
#include "Dom/JsonObject.h"

namespace N2CToolPresentationSpecPrivate
{
	/** What a tool's four hints must be: read-only, destructive, idempotent, open world */
	struct FExpectedHints
	{
		const TCHAR* Name;
		bool bReadOnly;
		bool bDestructive;
		bool bIdempotent;
		bool bOpenWorld;
	};

	constexpr bool R = true;
	constexpr bool W = false;

	/**
	 * The behaviour of every tool, written down apart from the table so a changed hint has to be changed twice. Read-only
	 * tools are all (R, no, yes, no). search-blueprint-nodes (card 0050) and list-components-in-actor (card 0167) are
	 * read-only now that they change nothing.
	 */
	const FExpectedHints Expected[] = {
		{ TEXT("add-bp-node-to-active-graph"), W, false, false, false },
		{ TEXT("add-component-class-to-actor"), W, false, false, false },
		{ TEXT("add-function-input-pin"), W, false, false, false },
		{ TEXT("add-function-return-pin"), W, false, false, false },
		{ TEXT("call_tool"), W, true, false, true },
		{ TEXT("compile-blueprint"), W, false, true, false },
		{ TEXT("connect-pins"), W, true, true, false },
		{ TEXT("copy-asset"), W, false, false, false },
		{ TEXT("create-blueprint-class"), W, false, false, false },
		{ TEXT("create-blueprint-function"), W, false, false, false },
		{ TEXT("create-comment-node"), W, false, false, false },
		{ TEXT("create-folder"), W, false, true, false },
		{ TEXT("create-get-local-function-variable-node"), W, false, false, false },
		{ TEXT("create-get-member-variable-node"), W, false, false, false },
		{ TEXT("create-local-variable"), W, false, false, false },
		{ TEXT("create-set-local-function-variable-node"), W, false, false, false },
		{ TEXT("create-set-member-variable-node"), W, false, false, false },
		{ TEXT("create-variable"), W, false, false, false },
		{ TEXT("delete-blueprint-function"), W, true, true, false },
		{ TEXT("delete-blueprint-node"), W, true, true, false },
		{ TEXT("delete-component-in-actor"), W, true, true, false },
		{ TEXT("delete-python-script"), W, true, true, false },
		{ TEXT("describe_toolset"), R, false, true, false },
		{ TEXT("export-n2cjson-batch"), R, false, true, false },
		{ TEXT("find-nodes-in-graph"), R, false, true, false },
		{ TEXT("get-available-llm-providers"), R, false, true, false },
		{ TEXT("get-available-translation-targets"), R, false, true, false },
		{ TEXT("get-blueprint-function-local-variables"), R, false, true, false },
		{ TEXT("get-blueprint-member-variables"), R, false, true, false },
		{ TEXT("get-blueprint-n2cjson"), R, false, true, false },
		{ TEXT("get-focused-blueprint"), R, false, true, false },
		{ TEXT("get-open-blueprint-editors"), R, false, true, false },
		{ TEXT("get-python-script"), R, false, true, false },
		{ TEXT("get-script-functions"), R, false, true, false },
		{ TEXT("get-translation-output-directory"), R, false, true, false },
		{ TEXT("list-blueprint-functions"), R, false, true, false },
		{ TEXT("list-blueprint-graphs"), R, false, true, false },
		{ TEXT("list-blueprint-tags"), R, false, true, false },
		{ TEXT("list-components-in-actor"), R, false, true, false },
		{ TEXT("list-overridable-functions"), R, false, true, false },
		{ TEXT("list-python-scripts"), R, false, true, false },
		{ TEXT("list_toolsets"), R, false, true, false },
		{ TEXT("move-asset"), W, true, false, false },
		{ TEXT("move-folder"), W, true, false, false },
		{ TEXT("open-blueprint-asset"), W, false, true, false },
		{ TEXT("open-blueprint-function"), W, false, true, false },
		{ TEXT("open-content-browser-path"), W, false, true, false },
		{ TEXT("read-content-browser-path"), R, false, true, false },
		{ TEXT("read-file"), R, false, true, false },
		{ TEXT("read-path"), R, false, true, false },
		{ TEXT("remove-function-entry-pin"), W, true, true, false },
		{ TEXT("remove-function-return-pin"), W, true, true, false },
		{ TEXT("remove-tag-from-graph"), W, true, true, false },
		{ TEXT("rename-asset"), W, true, false, false },
		{ TEXT("rename-folder"), W, true, false, false },
		{ TEXT("run-python"), W, true, false, true },
		{ TEXT("save-blueprint"), W, true, true, false },
		{ TEXT("save-python-script"), W, true, true, false },
		{ TEXT("search-blueprint-classes"), R, false, true, false },
		{ TEXT("search-blueprint-nodes"), R, false, true, false },
		{ TEXT("search-content-browser"), R, false, true, false },
		{ TEXT("search-python-scripts"), R, false, true, false },
		{ TEXT("search-variable-types"), R, false, true, false },
		{ TEXT("set-input-pin-value"), W, true, true, false },
		{ TEXT("set-local-function-variable-default-value"), W, true, true, false },
		{ TEXT("set-member-variable-default-value"), W, true, true, false },
		{ TEXT("tag-blueprint-graph"), W, false, true, false },
		{ TEXT("translate-batch"), W, false, false, true },
		{ TEXT("translate-focused-blueprint"), W, false, false, true },
	};

	const FExpectedHints* FindExpected(const FString& Name)
	{
		for (const FExpectedHints& Entry : Expected)
		{
			if (Name == Entry.Name)
			{
				return &Entry;
			}
		}
		return nullptr;
	}

	/** The tools whose results can be large: _meta["anthropic/maxResultSizeChars"] = 200000 */
	const TSet<FString>& LargeResultTools()
	{
		static const TSet<FString> Names = { TEXT("get-focused-blueprint"), TEXT("read-file"), TEXT("translate-focused-blueprint") };
		return Names;
	}

	/** The entry points a client keeps loaded: _meta["anthropic/alwaysLoad"] */
	const TSet<FString>& AlwaysLoadTools()
	{
		static const TSet<FString> Names = { TEXT("get-focused-blueprint"), TEXT("get-blueprint-n2cjson"),
			TEXT("list_toolsets"), TEXT("describe_toolset"), TEXT("call_tool") };
		return Names;
	}

	/** Names of every registered tool */
	TSet<FString> RegisteredNames()
	{
		TSet<FString> Names;
		for (const TSharedPtr<IN2CMcpTool>& Tool : FN2CMcpToolRegistry::Get().GetTools())
		{
			if (Tool.IsValid())
			{
				Names.Add(Tool->GetDefinition().Name);
			}
		}
		return Names;
	}

	/** What tools/list serves with every toolset on (the Python execution tools only when the user allowed them) */
	TArray<FMcpToolDefinition> ServedWithAllToolsets()
	{
		FN2CMcpToolManager Manager([]() { return FN2CMcpToolRegistry::Get().GetTools(); });
		FN2CMcpToolConfig Config;
		for (const FN2CMcpToolset* Toolset : N2CMcpToolsets::All)
		{
			Config.Toolsets.Add(Toolset->Name);
		}
		Manager.Configure(Config);
		return Manager.GetAllToolDefinitions();
	}

	/** A tool the table does not know */
	class FUnlistedTool : public IN2CMcpTool
	{
	public:
		virtual FMcpToolDefinition GetDefinition() const override
		{
			FMcpToolDefinition Definition(TEXT("n2c-spec-presentation-unlisted"), TEXT("Spec tool of NodeToCode.MCP.ToolPresentation"));
			Definition.Title = TEXT("Its own title");
			Definition.Annotations = FMcpToolAnnotations::ReadOnly();
			Definition.InputSchema = MakeShared<FJsonObject>();
			Definition.InputSchema->SetStringField(TEXT("type"), TEXT("object"));
			return Definition;
		}

		virtual FMcpToolCallResult Execute(const TSharedPtr<FJsonObject>& Arguments) override
		{
			return FMcpToolCallResult::CreateTextResult(TEXT("ran"));
		}
	};
}

/**
 * NodeToCode.MCP.ToolPresentation (card 0205): the catalog's presentation table gives every registered tool and every
 * tool-search meta-tool a title and all four behaviour hints, the hints match what the tools do, and tools/list, the
 * catalog and the generated tool reference all carry them. Review ids: rec-mcp-spec-clients-7,
 * imp-mcp-tools-variables-9, imp-mcp-tools-bp-structure-3, imp-mcp-tools-graph-functions-7,
 * imp-mcp-tools-contentbrowser-fs-6.
 */
BEGIN_DEFINE_SPEC(FN2CToolPresentationSpec, "NodeToCode.MCP.ToolPresentation", N2C_TEST_FLAGS)

	/** Checks one definition as a client gets it: a title, the four hints, the expected values and the _meta entries */
	void ExpectPresented(const FString& Where, const FMcpToolDefinition& Definition)
	{
		using namespace N2CToolPresentationSpecPrivate;
		const FString& Name = Definition.Name;
		TestFalse(FString::Printf(TEXT("%s %s: title"), *Where, *Name), Definition.Title.IsEmpty());
		const FMcpToolAnnotations& Hints = Definition.Annotations;
		if (!TestTrue(FString::Printf(TEXT("%s %s: all four hints set"), *Where, *Name), Hints.ReadOnlyHint.IsSet()
			&& Hints.DestructiveHint.IsSet() && Hints.IdempotentHint.IsSet() && Hints.OpenWorldHint.IsSet()))
		{
			return;
		}

		// As tools/list sends it
		const TSharedPtr<FJsonObject> Json = Definition.ToJson();
		const TSharedPtr<FJsonObject>* Annotations = nullptr;
		TestTrue(FString::Printf(TEXT("%s %s: tools/list title"), *Where, *Name), Json->HasTypedField<EJson::String>(TEXT("title")));
		if (TestTrue(FString::Printf(TEXT("%s %s: tools/list annotations"), *Where, *Name), Json->TryGetObjectField(TEXT("annotations"), Annotations)))
		{
			TestEqual(FString::Printf(TEXT("%s %s: four annotation fields"), *Where, *Name), (*Annotations)->Values.Num(), 4);
		}

		if (const FExpectedHints* Want = FindExpected(Name))
		{
			TestEqual(FString::Printf(TEXT("%s %s: readOnlyHint"), *Where, *Name), Hints.ReadOnlyHint.GetValue(), Want->bReadOnly);
			TestEqual(FString::Printf(TEXT("%s %s: destructiveHint"), *Where, *Name), Hints.DestructiveHint.GetValue(), Want->bDestructive);
			TestEqual(FString::Printf(TEXT("%s %s: idempotentHint"), *Where, *Name), Hints.IdempotentHint.GetValue(), Want->bIdempotent);
			TestEqual(FString::Printf(TEXT("%s %s: openWorldHint"), *Where, *Name), Hints.OpenWorldHint.GetValue(), Want->bOpenWorld);
		}
		else
		{
			AddError(FString::Printf(TEXT("%s %s: no expected hints in this spec; add the tool to Expected"), *Where, *Name));
		}

		const TSharedPtr<FJsonObject> Meta = Definition.BuildMeta();
		double MaxChars = 0;
		const bool bLarge = Meta.IsValid() && Meta->TryGetNumberField(N2CMcpToolMetaKeys::MaxResultSizeChars, MaxChars);
		TestEqual(FString::Printf(TEXT("%s %s: anthropic/maxResultSizeChars sent"), *Where, *Name), bLarge, LargeResultTools().Contains(Name));
		if (bLarge)
		{
			TestEqual(FString::Printf(TEXT("%s %s: anthropic/maxResultSizeChars"), *Where, *Name), static_cast<int32>(MaxChars), 200000);
		}
		bool bAlwaysLoad = false;
		const bool bSent = Meta.IsValid() && Meta->TryGetBoolField(N2CMcpToolMetaKeys::AlwaysLoad, bAlwaysLoad) && bAlwaysLoad;
		TestEqual(FString::Printf(TEXT("%s %s: anthropic/alwaysLoad"), *Where, *Name), bSent, AlwaysLoadTools().Contains(Name));
	}

END_DEFINE_SPEC(FN2CToolPresentationSpec)

void FN2CToolPresentationSpec::Define()
{
	using namespace N2CToolPresentationSpecPrivate;

	It(TEXT("has one entry for every registered tool and meta-tool and none for anything else"), [this]()
	{
		TSet<FString> Tools = RegisteredNames();
		Tools.Add(FN2CMcpToolManager::ListToolsetsName);
		Tools.Add(FN2CMcpToolManager::DescribeToolsetName);
		Tools.Add(FN2CMcpToolManager::CallToolName);

		TSet<FString> InTable;
		for (const FN2CToolPresentation& Entry : FN2CToolCatalog::GetPresentations())
		{
			TestFalse(FString::Printf(TEXT("%s: listed once"), *Entry.Name), InTable.Contains(Entry.Name));
			InTable.Add(Entry.Name);
			TestTrue(FString::Printf(TEXT("%s: names a registered tool or meta-tool"), *Entry.Name), Tools.Contains(Entry.Name));
		}
		for (const FString& Name : Tools)
		{
			TestNotNull(FString::Printf(TEXT("%s: has a table entry"), *Name), FN2CToolCatalog::FindPresentation(Name));
			TestNotNull(FString::Printf(TEXT("%s: has expected hints in this spec"), *Name), FindExpected(Name));
		}
		TestEqual(TEXT("Expected covers exactly the tools"), static_cast<int32>(UE_ARRAY_COUNT(Expected)), Tools.Num());
	});

	It(TEXT("serves every tool in tools/list with a title, all four hints and its _meta"), [this]()
	{
		const TArray<FMcpToolDefinition> Served = ServedWithAllToolsets();
		int32 ExpectedServed = 0;
		for (const FString& Name : RegisteredNames())
		{
			ExpectedServed += UN2CUserSettings::IsPythonExecutionTool(Name) && !UN2CUserSettings::IsMcpPythonExecutionAllowed() ? 0 : 1;
		}
		TestEqual(TEXT("serves every registered tool (the Python execution tools only when allowed)"), Served.Num(), ExpectedServed);
		for (const FMcpToolDefinition& Definition : Served)
		{
			ExpectPresented(TEXT("tools/list"), Definition);
		}

		// The tool-search exposure lists only the meta-tools
		FN2CMcpToolManager SearchManager([]() { return TArray<TSharedPtr<IN2CMcpTool>>(); });
		FN2CMcpToolConfig SearchConfig;
		SearchConfig.Exposure = EN2CMcpToolExposure::ToolSearch;
		SearchManager.Configure(SearchConfig);
		const TArray<FMcpToolDefinition> MetaTools = SearchManager.GetAllToolDefinitions();
		TestEqual(TEXT("three meta-tools"), MetaTools.Num(), 3);
		for (const FMcpToolDefinition& Definition : MetaTools)
		{
			ExpectPresented(TEXT("tool search"), Definition);
		}
	});

	It(TEXT("gives the catalog's descriptors the table's titles and hints"), [this]()
	{
		FN2CMcpToolManager Manager([]() { return FN2CMcpToolRegistry::Get().GetTools(); });
		FN2CMcpToolConfig Config;
		for (const FN2CMcpToolset* Toolset : N2CMcpToolsets::All)
		{
			Config.Toolsets.Add(Toolset->Name);
		}
		Manager.Configure(Config);
		const FN2CToolCatalog Catalog(Manager);
		const TArray<FN2CToolDescriptor> Descriptors = Catalog.Describe(FN2CToolFilter::Served());
		TestTrue(TEXT("describes tools"), Descriptors.Num() > 0);
		for (const FN2CToolDescriptor& Descriptor : Descriptors)
		{
			const FN2CToolPresentation* Entry = FN2CToolCatalog::FindPresentation(Descriptor.Name);
			if (!TestNotNull(FString::Printf(TEXT("%s: table entry"), *Descriptor.Name), Entry))
			{
				continue;
			}
			TestEqual(FString::Printf(TEXT("%s: title"), *Descriptor.Name), Descriptor.Title, Entry->Title);
			TestTrue(FString::Printf(TEXT("%s: hints"), *Descriptor.Name), Descriptor.Hints.ReadOnlyHint == Entry->Hints.ReadOnlyHint
				&& Descriptor.Hints.DestructiveHint == Entry->Hints.DestructiveHint && Descriptor.Hints.IdempotentHint == Entry->Hints.IdempotentHint
				&& Descriptor.Hints.OpenWorldHint == Entry->Hints.OpenWorldHint);
		}
	});

	It(TEXT("puts the same titles, hints and _meta in the generated tool reference"), [this]()
	{
		const TArray<FMcpToolDefinition> Tools = N2CMcpToolReference::CollectTools();
		TestEqual(TEXT("every registered tool"), Tools.Num(), RegisteredNames().Num());
		for (const FMcpToolDefinition& Definition : Tools)
		{
			ExpectPresented(TEXT("reference"), Definition);
		}
		for (const FMcpToolDefinition& Definition : N2CMcpToolReference::CollectMetaTools())
		{
			ExpectPresented(TEXT("reference"), Definition);
		}
	});

	It(TEXT("follows the hint rules by tool name"), [this]()
	{
		const TSet<FString> OpenWorld = { TEXT("translate-batch"), TEXT("translate-focused-blueprint"), TEXT("run-python"), TEXT("call_tool") };
		for (const FN2CToolPresentation& Entry : FN2CToolCatalog::GetPresentations())
		{
			const FString& Name = Entry.Name;
			const bool bReadOnly = Entry.Hints.ReadOnlyHint.Get(false);
			const bool bDestructive = Entry.Hints.DestructiveHint.Get(true);
			const bool bIdempotent = Entry.Hints.IdempotentHint.Get(false);
			if (Name.StartsWith(TEXT("delete-")) || Name.StartsWith(TEXT("remove-")) || Name.StartsWith(TEXT("move-")) || Name.StartsWith(TEXT("rename-")))
			{
				TestTrue(FString::Printf(TEXT("%s: destructive"), *Name), !bReadOnly && bDestructive);
			}
			if (Name.StartsWith(TEXT("create-")) || Name.StartsWith(TEXT("add-")) || Name.StartsWith(TEXT("tag-")))
			{
				TestTrue(FString::Printf(TEXT("%s: additive"), *Name), !bReadOnly && !bDestructive);
			}
			if (Name.StartsWith(TEXT("set-")))
			{
				TestTrue(FString::Printf(TEXT("%s: idempotent"), *Name), !bReadOnly && bIdempotent);
			}
			if (Name.StartsWith(TEXT("get-")) || Name.StartsWith(TEXT("list-")) || Name.StartsWith(TEXT("search-")) || Name.StartsWith(TEXT("read-")))
			{
				TestTrue(FString::Printf(TEXT("%s: read-only"), *Name), bReadOnly);
			}
			if (bReadOnly)
			{
				TestTrue(FString::Printf(TEXT("%s: read-only tools are not destructive and are idempotent"), *Name), !bDestructive && bIdempotent);
			}
			TestEqual(FString::Printf(TEXT("%s: openWorldHint"), *Name), Entry.Hints.OpenWorldHint.Get(true), OpenWorld.Contains(Name));
		}

		// The tools the card names
		for (const TCHAR* Name : { TEXT("search-blueprint-nodes"), TEXT("list-components-in-actor"),
			TEXT("get-blueprint-function-local-variables"), TEXT("get-open-blueprint-editors"), TEXT("read-content-browser-path") })
		{
			const FN2CToolPresentation* Entry = FN2CToolCatalog::FindPresentation(Name);
			TestTrue(FString::Printf(TEXT("%s: read-only"), Name), Entry && Entry->Hints.IsReadOnly());
		}
		const FN2CToolPresentation* Open = FN2CToolCatalog::FindPresentation(TEXT("open-content-browser-path"));
		TestTrue(TEXT("open-content-browser-path is not read-only (create_if_missing)"), Open && !Open->Hints.IsReadOnly());
		for (const TCHAR* Name : { TEXT("connect-pins"), TEXT("set-input-pin-value") })
		{
			const FN2CToolPresentation* Entry = FN2CToolCatalog::FindPresentation(Name);
			TestTrue(FString::Printf(TEXT("%s: idempotent"), Name), Entry && Entry->Hints.IdempotentHint.Get(false));
		}
	});

	It(TEXT("keeps each tool's Play In Editor rule: the table's read-only hint is the one the tool sets itself"), [this]()
	{
		for (const TSharedPtr<IN2CMcpTool>& Tool : FN2CMcpToolRegistry::Get().GetTools())
		{
			if (!Tool.IsValid())
			{
				continue;
			}
			const FMcpToolDefinition Own = Tool->GetDefinition();
			const FN2CToolPresentation* Entry = FN2CToolCatalog::FindPresentation(Own.Name);
			if (TestNotNull(FString::Printf(TEXT("%s: table entry"), *Own.Name), Entry))
			{
				TestEqual(FString::Printf(TEXT("%s: read-only as the tool says"), *Own.Name), Entry->Hints.IsReadOnly(), Own.Annotations.IsReadOnly());
			}
		}
	});

	It(TEXT("leaves a tool the table does not know as it defined itself"), [this]()
	{
		const TSharedRef<FUnlistedTool> Tool = MakeShared<FUnlistedTool>();
		FMcpToolDefinition Definition = Tool->GetDefinition();
		TestFalse(TEXT("no entry"), FN2CToolCatalog::ApplyPresentation(Definition));
		TestEqual(TEXT("own title"), Definition.Title, FString(TEXT("Its own title")));
		TestTrue(TEXT("own hints"), Definition.Annotations.IsReadOnly());

		FN2CMcpToolManager Manager([]() { return TArray<TSharedPtr<IN2CMcpTool>>(); });
		if (TestTrue(TEXT("registers"), Manager.RegisterTool(Tool)))
		{
			const TSharedPtr<const FMcpToolDefinition> Cached = Manager.GetToolDefinition(TEXT("n2c-spec-presentation-unlisted"));
			TestTrue(TEXT("cached with its own title"), Cached.IsValid() && Cached->Title == TEXT("Its own title"));
		}
	});
}

#endif // WITH_DEV_AUTOMATION_TESTS
