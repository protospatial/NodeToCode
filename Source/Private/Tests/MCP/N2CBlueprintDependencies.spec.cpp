// Copyright (c) 2026 Nick McClure (Protospatial)
// SPDX-License-Identifier: Apache-2.0

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Tests/N2CTestHelpers.h"
#include "Tests/N2CToolSpecHarness.h"
#include "Tests/Dependencies/N2CDependencyFixtures.h"
#include "MCP/Tools/IN2CMcpTool.h"
#include "MCP/Tools/N2CMcpOutputSchema.h"
#include "MCP/Tools/Implementations/Dependencies/N2CMcpDependencies.h"
#include "MCP/Tools/Implementations/Dependencies/N2CMcpGetBlueprintDependenciesTool.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/ScopeExit.h"

/**
 * Card 0319: get-blueprint-dependencies over the dependency fixtures of card 0318 (N2CDependencyFixtures.h, the
 * test-only content plugin mounted at /NodeToCodeTestContent): the nodes, typed edges with their graph and node, the
 * exact dependencies-first order and the one cycle group {B, C} as structured content that matches the tool's
 * outputSchema; a single asset path with includeCallGraph false; the JSON export with toFile and when the result is
 * over the inline limit; and every argument error.
 */
BEGIN_DEFINE_SPEC(FN2CBlueprintDependenciesSpec, "NodeToCode.MCP.BlueprintDependencies", N2C_TEST_FLAGS)

	static FString Path(const TCHAR* AssetName)
	{
		return N2CTest::DependencyFixtures::ObjectPath(AssetName);
	}

	static FString FirstText(const FMcpToolCallResult& Result)
	{
		FString Text;
		if (Result.Content.Num() > 0 && Result.Content[0].IsValid())
		{
			Result.Content[0]->TryGetStringField(TEXT("text"), Text);
		}
		return Text;
	}

	static TSharedPtr<FJsonObject> Args(const TArray<FString>& Paths)
	{
		const TSharedPtr<FJsonObject> Json = MakeShared<FJsonObject>();
		TArray<TSharedPtr<FJsonValue>> Values;
		for (const FString& Value : Paths)
		{
			Values.Add(MakeShared<FJsonValueString>(Value));
		}
		Json->SetArrayField(TEXT("paths"), Values);
		return Json;
	}

	static TArray<FString> StringsOf(const TSharedPtr<FJsonObject>& Json, const TCHAR* Field)
	{
		TArray<FString> Out;
		const TArray<TSharedPtr<FJsonValue>>* Values = nullptr;
		if (Json.IsValid() && Json->TryGetArrayField(Field, Values))
		{
			for (const TSharedPtr<FJsonValue>& Value : *Values)
			{
				Out.Add(Value->AsString());
			}
		}
		return Out;
	}

	static TArray<TSharedPtr<FJsonObject>> ObjectsOf(const TSharedPtr<FJsonObject>& Json, const TCHAR* Field)
	{
		TArray<TSharedPtr<FJsonObject>> Out;
		const TArray<TSharedPtr<FJsonValue>>* Values = nullptr;
		if (Json.IsValid() && Json->TryGetArrayField(Field, Values))
		{
			for (const TSharedPtr<FJsonValue>& Value : *Values)
			{
				Out.Add(Value->AsObject());
			}
		}
		return Out;
	}

	static bool HasEdge(const TSharedPtr<FJsonObject>& Json, const FString& From, const FString& To, const FString& Kind,
		TSharedPtr<FJsonObject>* OutEdge = nullptr)
	{
		for (const TSharedPtr<FJsonObject>& Edge : ObjectsOf(Json, TEXT("edges")))
		{
			if (Edge->GetStringField(TEXT("from")) == From && Edge->GetStringField(TEXT("to")) == To && Edge->GetStringField(TEXT("kind")) == Kind)
			{
				if (OutEdge)
				{
					*OutEdge = Edge;
				}
				return true;
			}
		}
		return false;
	}

	/** The call's result: not an error, marked untrusted, its structuredContent valid against the outputSchema; its JSON */
	TSharedPtr<FJsonObject> Succeeds(const FString& What, const TSharedPtr<FJsonObject>& Arguments)
	{
		const FMcpToolCallResult Result = N2CTest::RunTool(TEXT("get-blueprint-dependencies"), Arguments);
		if (!TestFalse(What + TEXT(": not an error (") + FirstText(Result) + TEXT(")"), Result.bIsError))
		{
			return nullptr;
		}
		TestTrue(What + TEXT(": marked as untrusted"), N2CTest::IsMarkedUntrusted(Result));
		const TSharedPtr<IN2CMcpTool> Tool = N2CTest::FindRegisteredTool(TEXT("get-blueprint-dependencies"));
		if (TestTrue(What + TEXT(": structuredContent"), Tool.IsValid() && Result.StructuredContent.IsValid()))
		{
			TArray<FString> Errors;
			TestTrue(What + TEXT(": structuredContent matches the outputSchema: ") + FString::Join(Errors, TEXT("; ")),
				N2CMcpOutputSchema::Validate(Tool->GetDefinition().OutputSchema, MakeShared<FJsonValueObject>(Result.StructuredContent), Errors));
			for (const FString& Error : Errors)
			{
				AddError(What + TEXT(": ") + Error);
			}
		}
		return N2CTest::ResultJson(Result);
	}

	/** The call fails with a message that starts with Code */
	void Fails(const FString& What, const TSharedPtr<FJsonObject>& Arguments, const FString& Code)
	{
		const FMcpToolCallResult Result = N2CTest::RunTool(TEXT("get-blueprint-dependencies"), Arguments);
		const FString Text = FirstText(Result);
		TestTrue(What + TEXT(": an error"), Result.bIsError);
		TestTrue(FString::Printf(TEXT("%s: starts with %s (%s)"), *What, *Code, *Text), Text.StartsWith(Code));
	}

END_DEFINE_SPEC(FN2CBlueprintDependenciesSpec)

void FN2CBlueprintDependenciesSpec::Define()
{
	using namespace N2CTest::DependencyFixtures;

	It(TEXT("is read-only and in the Blueprint discovery toolset, and requires paths"), [this]()
	{
		const TSharedPtr<IN2CMcpTool> Tool = N2CTest::FindRegisteredTool(TEXT("get-blueprint-dependencies"));
		if (!TestTrue(TEXT("registered"), Tool.IsValid()))
		{
			return;
		}
		const FMcpToolDefinition Definition = Tool->GetDefinition();
		TestTrue(TEXT("read-only"), Definition.Annotations.IsReadOnly());
		TestTrue(TEXT("BlueprintDiscoveryTools"), Definition.Toolset == &N2CMcpToolsets::BlueprintDiscovery);
		TestTrue(TEXT("returns untrusted content"), Definition.bReturnsUntrustedContent);
		TestTrue(TEXT("declares an outputSchema"), Definition.OutputSchema.IsValid());
		TestEqual(TEXT("paths is required"), StringsOf(Definition.InputSchema, TEXT("required")), TArray<FString>({ TEXT("paths") }));
	});

	It(TEXT("returns the fixtures' nodes, typed edges, dependencies-first order and the one cycle group"), [this]()
	{
		const TSharedPtr<FJsonObject> Json = Succeeds(TEXT("fixture folder"), Args({ FolderPath }));
		if (!Json.IsValid())
		{
			return;
		}
		TestTrue(TEXT("inline"), Json->GetBoolField(TEXT("inline")));
		TestFalse(TEXT("no file without toFile"), Json->HasField(TEXT("savedTo")));
		TestEqual(TEXT("roots"), StringsOf(Json, TEXT("roots")), TArray<FString>({ FolderPath }));
		TestEqual(TEXT("eight nodes (Tests/NodeToCodeTestContent is junctioned into the host's Plugins folder by build.ps1)"),
			static_cast<int32>(Json->GetNumberField(TEXT("nodeCount"))), 8);

		TMap<FString, FString> Kinds;
		for (const TSharedPtr<FJsonObject>& Node : ObjectsOf(Json, TEXT("nodes")))
		{
			Kinds.Add(Node->GetStringField(TEXT("path")), Node->GetStringField(TEXT("kind")));
			TestTrue(Node->GetStringField(TEXT("name")) + TEXT(" is a root"), Node->GetBoolField(TEXT("root")));
		}
		TestEqual(TEXT("enum kind"), Kinds.FindRef(Path(EnumName)), FString(TEXT("enum")));
		TestEqual(TEXT("struct kind"), Kinds.FindRef(Path(StructName)), FString(TEXT("struct")));
		TestEqual(TEXT("interface kind"), Kinds.FindRef(Path(InterfaceName)), FString(TEXT("interface")));
		TestEqual(TEXT("macro library kind"), Kinds.FindRef(Path(MacroLibraryName)), FString(TEXT("macro-library")));
		TestEqual(TEXT("Blueprint kind"), Kinds.FindRef(Path(AName)), FString(TEXT("blueprint")));

		// The order FN2CDependencyIndex::ComputeOrder gives: kinds first among the ready nodes, the cycle {B, C} by path
		TestEqual(TEXT("order"), StringsOf(Json, TEXT("order")), TArray<FString>({ Path(EnumName), Path(StructName),
			Path(InterfaceName), Path(MacroLibraryName), Path(BName), Path(CName), Path(AName), Path(ChildName) }));
		const TArray<TSharedPtr<FJsonValue>>* Groups = nullptr;
		if (TestTrue(TEXT("one cycle group"), Json->TryGetArrayField(TEXT("cycleGroups"), Groups) && Groups->Num() == 1))
		{
			TArray<FString> Group;
			for (const TSharedPtr<FJsonValue>& Value : (*Groups)[0]->AsArray())
			{
				Group.Add(Value->AsString());
			}
			TestEqual(TEXT("the cycle group is {B, C}"), Group, TArray<FString>({ Path(BName), Path(CName) }));
		}

		TSharedPtr<FJsonObject> Call;
		if (TestTrue(TEXT("A calls B"), HasEdge(Json, Path(AName), Path(BName), TEXT("calls"), &Call)))
		{
			TestEqual(TEXT("the call is in A's event graph"), Call->GetStringField(TEXT("graph")), FString(TEXT("EventGraph")));
			TestEqual(TEXT("the call names its node's GUID"), Call->GetStringField(TEXT("node")).Len(), 32);
		}
		TestTrue(TEXT("A uses the macro library"), HasEdge(Json, Path(AName), Path(MacroLibraryName), TEXT("macro")));
		TestTrue(TEXT("A implements the interface"), HasEdge(Json, Path(AName), Path(InterfaceName), TEXT("interface")));
		TestTrue(TEXT("the child's parent is A"), HasEdge(Json, Path(ChildName), Path(AName), TEXT("parent")));
		TestTrue(TEXT("C casts to B"), HasEdge(Json, Path(CName), Path(BName), TEXT("cast")));
		TestTrue(TEXT("the struct has an enum member"), HasEdge(Json, Path(StructName), Path(EnumName), TEXT("hard-package")));
		TestEqual(TEXT("edgeCount counts the edges"), static_cast<int32>(Json->GetNumberField(TEXT("edgeCount"))), ObjectsOf(Json, TEXT("edges")).Num());
	});

	It(TEXT("takes one asset path, pulls in what it depends on, and reads only the AssetRegistry without the call graph"), [this]()
	{
		const TSharedPtr<FJsonObject> Arguments = Args({ FString::Printf(TEXT("%s/%s"), FolderPath, AName) });
		Arguments->SetBoolField(TEXT("includeCallGraph"), false);
		const TSharedPtr<FJsonObject> Json = Succeeds(TEXT("A alone"), Arguments);
		if (!Json.IsValid())
		{
			return;
		}
		TMap<FString, bool> Roots;
		for (const TSharedPtr<FJsonObject>& Node : ObjectsOf(Json, TEXT("nodes")))
		{
			Roots.Add(Node->GetStringField(TEXT("path")), Node->GetBoolField(TEXT("root")));
		}
		TestTrue(TEXT("A is the root"), Roots.FindRef(Path(AName)));
		TestTrue(TEXT("B was pulled in, not a root"), Roots.Contains(Path(BName)) && !Roots.FindRef(Path(BName)));
		TestFalse(TEXT("the child is not indexed"), Roots.Contains(Path(ChildName)));
		for (const TSharedPtr<FJsonObject>& Edge : ObjectsOf(Json, TEXT("edges")))
		{
			const FString Kind = Edge->GetStringField(TEXT("kind"));
			TestTrue(TEXT("only AssetRegistry edges: ") + Kind, Kind == TEXT("hard-package") || Kind == TEXT("parent") || Kind == TEXT("interface"));
		}
		const TArray<FString> Order = StringsOf(Json, TEXT("order"));
		TestTrue(TEXT("A comes last"), Order.Num() > 1 && Order.Last() == Path(AName));

		Arguments->SetBoolField(TEXT("includeReferenced"), false);
		const TSharedPtr<FJsonObject> Alone = Succeeds(TEXT("A without what it references"), Arguments);
		if (Alone.IsValid())
		{
			TestEqual(TEXT("only A"), StringsOf(Alone, TEXT("order")), TArray<FString>({ Path(AName) }));
		}
	});

	It(TEXT("writes the JSON export under the Dependencies folder with toFile, with the same index"), [this]()
	{
		N2CTest::FScratchDir Scratch(TEXT("N2CDependencyExport"));
		N2CMcpDependencies::SetExportDirectoryForTests(Scratch.Path);
		ON_SCOPE_EXIT { N2CMcpDependencies::SetExportDirectoryForTests(FString()); };

		const TSharedPtr<FJsonObject> Arguments = Args({ FolderPath });
		Arguments->SetBoolField(TEXT("toFile"), true);
		const TSharedPtr<FJsonObject> Json = Succeeds(TEXT("toFile"), Arguments);
		if (!Json.IsValid())
		{
			return;
		}
		TestTrue(TEXT("still inline"), Json->GetBoolField(TEXT("inline")));
		const FString SavedTo = Json->GetStringField(TEXT("savedTo"));
		TestTrue(TEXT("savedTo is inside the export folder"), FPaths::IsUnderDirectory(SavedTo, Scratch.Path));
		TestTrue(TEXT("named after the root"), FPaths::GetCleanFilename(SavedTo).StartsWith(TEXT("Dependencies__")));
		FString Text;
		const TSharedPtr<FJsonObject> File = FFileHelper::LoadFileToString(Text, *SavedTo) ? N2CTest::ParseJsonObject(Text) : nullptr;
		if (TestTrue(TEXT("the export parses"), File.IsValid()))
		{
			TestEqual(TEXT("the export's order"), StringsOf(File, TEXT("order")), StringsOf(Json, TEXT("order")));
			TestEqual(TEXT("the export's edges"), ObjectsOf(File, TEXT("edges")).Num(), ObjectsOf(Json, TEXT("edges")).Num());
			TestEqual(TEXT("bytes"), static_cast<int64>(Json->GetNumberField(TEXT("bytes"))), IFileManager::Get().FileSize(*SavedTo));
		}
		N2CMcpDependencies::FRequest Same;
		Same.Paths.Add(FolderPath);
		TestEqual(TEXT("the same call names the same file"), N2CMcpDependencies::ExportFileName(Same), FPaths::GetCleanFilename(SavedTo));
	});

	It(TEXT("moves the lists to the export file when the result is over the inline limit"), [this]()
	{
		N2CTest::FScratchDir Scratch(TEXT("N2CDependencyLarge"));
		N2CMcpDependencies::SetExportDirectoryForTests(Scratch.Path);
		ON_SCOPE_EXIT { N2CMcpDependencies::SetExportDirectoryForTests(FString()); };

		N2CMcpDependencies::FRequest Request;
		Request.Paths.Add(FolderPath);
		Request.MaxInlineTokens = 50;
		FString Error;
		const TSharedPtr<FJsonObject> Json = N2CMcpDependencies::Run(Request, Error);
		if (!TestTrue(TEXT("built: ") + Error, Json.IsValid()))
		{
			return;
		}
		TestFalse(TEXT("not inline"), Json->GetBoolField(TEXT("inline")));
		for (const TCHAR* Field : { TEXT("nodes"), TEXT("edges"), TEXT("order"), TEXT("cycleGroups") })
		{
			TestFalse(FString::Printf(TEXT("%s left out"), Field), Json->HasField(Field));
		}
		TestEqual(TEXT("counts stay"), static_cast<int32>(Json->GetNumberField(TEXT("nodeCount"))), 8);
		TestEqual(TEXT("cycleGroupCount"), static_cast<int32>(Json->GetNumberField(TEXT("cycleGroupCount"))), 1);
		TestTrue(TEXT("reason says to read the file"), Json->GetStringField(TEXT("reason")).Contains(TEXT("savedTo")));
		FString Text;
		const TSharedPtr<FJsonObject> File = FFileHelper::LoadFileToString(Text, *Json->GetStringField(TEXT("savedTo")))
			? N2CTest::ParseJsonObject(Text) : nullptr;
		TestTrue(TEXT("the file holds the order"), File.IsValid() && StringsOf(File, TEXT("order")).Num() == 8);
	});

	It(TEXT("refuses missing, empty, malformed and unknown paths and non-boolean flags"), [this]()
	{
		// A call that wrongly succeeded would write its export here, not in the host's Saved folder
		N2CTest::FScratchDir Scratch(TEXT("N2CDependencyErrors"));
		N2CMcpDependencies::SetExportDirectoryForTests(Scratch.Path);
		ON_SCOPE_EXIT { N2CMcpDependencies::SetExportDirectoryForTests(FString()); };

		Fails(TEXT("no arguments"), MakeShared<FJsonObject>(), TEXT("INVALID_ARGUMENT"));
		Fails(TEXT("empty paths"), Args({}), TEXT("INVALID_ARGUMENT"));
		{
			const TSharedPtr<FJsonObject> Arguments = MakeShared<FJsonObject>();
			Arguments->SetStringField(TEXT("paths"), FolderPath);
			Fails(TEXT("paths as a string"), Arguments, TEXT("INVALID_ARGUMENT"));
		}
		{
			const TSharedPtr<FJsonObject> Arguments = MakeShared<FJsonObject>();
			TArray<TSharedPtr<FJsonValue>> Values = { MakeShared<FJsonValueNumber>(3) };
			Arguments->SetArrayField(TEXT("paths"), Values);
			Fails(TEXT("a number in paths"), Arguments, TEXT("INVALID_ARGUMENT"));
		}
		TArray<FString> TooMany;
		for (int32 Index = 0; Index <= FN2CMcpGetBlueprintDependenciesTool::MaxPaths; ++Index)
		{
			TooMany.Add(FString::Printf(TEXT("/Game/N2CNoSuch%d"), Index));
		}
		Fails(TEXT("too many paths"), Args(TooMany), TEXT("INVALID_ARGUMENT"));
		Fails(TEXT("a path without a leading slash"), Args({ TEXT("Game/Blueprints") }), TEXT("INVALID_ARGUMENT"));
		Fails(TEXT("a folder with nothing to index"), Args({ TEXT("/Game/N2CNoSuchDependencyFolder") }), TEXT("NOT_FOUND"));
		{
			const TSharedPtr<FJsonObject> Arguments = Args({ FolderPath });
			Arguments->SetStringField(TEXT("toFile"), TEXT("yes"));
			Fails(TEXT("toFile not a boolean"), Arguments, TEXT("INVALID_ARGUMENT"));
		}
		{
			const TSharedPtr<FJsonObject> Arguments = Args({ FolderPath });
			Arguments->SetStringField(TEXT("includeCallGraph"), TEXT("false"));
			Fails(TEXT("includeCallGraph as the string false"), Arguments, TEXT("INVALID_ARGUMENT"));
		}
	});
}

#endif // WITH_DEV_AUTOMATION_TESTS
