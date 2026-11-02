// Copyright (c) 2026 Nick McClure (Protospatial)
// SPDX-License-Identifier: Apache-2.0

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Tests/N2CTestHelpers.h"
#include "MCP/Tools/Implementations/Translation/N2CCppPlacement.h"
#include "MCP/Tools/Implementations/Translation/N2CMcpPlaceCppTranslationTool.h"
#include "Interfaces/IProjectManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "ProjectDescriptor.h"
#include "Misc/ScopeExit.h"
#include "Tests/N2CToolSpecHarness.h"
#include "MCP/Tools/N2CMcpOutputSchema.h"
#include "Utils/N2CPathUtils.h"
#include "Dom/JsonObject.h"

namespace N2CCppPlacementSpec
{
	const TCHAR* const BuildCs = TEXT(
		"using UnrealBuildTool;\n\n"
		"public class N2CPlaceTest : ModuleRules\n{\n"
		"\tpublic N2CPlaceTest(ReadOnlyTargetRules Target) : base(Target)\n\t{\n"
		"\t\tPublicDependencyModuleNames.AddRange(new string[] { \"Core\", \"CoreUObject\" });\n"
		"\t}\n}\n");

	/** A translation of BP_Placed's Fire function that calls UKismetSystemLibrary and UGameplayStatics */
	FN2CCppPlacementRequest MakeRequest(const FString& SourceDir)
	{
		FN2CCppPlacementRequest Request;
		Request.ModuleName = TEXT("N2CPlaceTest");
		Request.ClassName = TEXT("PlacedActor");
		Request.ParentClass = TEXT("Actor");
		Request.ModuleSourceDirOverride = SourceDir;

		FN2CGraphTranslation& Fire = Request.Translation.Graphs.AddDefaulted_GetRef();
		Fire.GraphName = TEXT("Fire");
		Fire.GraphType = TEXT("Function");
		Fire.Code.GraphDeclaration = TEXT("/** Prints and finds the player pawn */\nUFUNCTION(BlueprintCallable, Category = \"Placed\")\nvoid Fire();");
		Fire.Code.GraphImplementation = TEXT("void ABP_Placed_C::Fire()\n{\n")
			TEXT("\tUKismetSystemLibrary::PrintString(this, TEXT(\"Fire\"));\n")
			TEXT("\tAPawn* Pawn = UGameplayStatics::GetPlayerPawn(this, 0);\n}");
		Fire.Code.ImplementationNotes = TEXT("Needs Kismet/KismetSystemLibrary.h and Kismet/GameplayStatics.h.");

		FN2CGraphTranslation& Ammo = Request.Translation.Graphs.AddDefaulted_GetRef();
		Ammo.GraphName = TEXT("FAmmo");
		Ammo.GraphType = TEXT("Struct");
		Ammo.Code.GraphDeclaration = TEXT("USTRUCT(BlueprintType)\nstruct FAmmo\n{\n\tGENERATED_BODY()\n\tUPROPERTY(EditAnywhere)\n\tint32 Count = 0;\n};");

		FN2CBlueprint Blueprint;
		Blueprint.Metadata.Name = TEXT("BP_Placed");
		FN2CGraph& Graph = Blueprint.Graphs.AddDefaulted_GetRef();
		FN2CNodeDefinition& Print = Graph.Nodes.AddDefaulted_GetRef();
		Print.MemberParent = TEXT("KismetSystemLibrary");
		Print.MemberName = TEXT("PrintString");
		FN2CNodeDefinition& GetPawn = Graph.Nodes.AddDefaulted_GetRef();
		GetPawn.MemberParent = TEXT("GameplayStatics");
		GetPawn.MemberName = TEXT("GetPlayerPawn");
		FN2CNodeDefinition& Own = Graph.Nodes.AddDefaulted_GetRef();
		Own.MemberParent = TEXT("BP_Placed");
		Own.MemberName = TEXT("Ammo");
		Request.Blueprint = Blueprint;
		return Request;
	}
}

/**
 * Card 0314: FN2CCppPlacement turns a C++ translation into a class a module compiles, and place-cpp-translation exposes
 * it. Each case uses a scratch module folder (N2CPlaceTest, with its Build.cs) through ModuleSourceDirOverride, except
 * the tool cases, which use the host project's own module in dry run. Review ids: rec-agentic-landscape-6,
 * rec-ue58-toolsets-overlap-11.
 */
BEGIN_DEFINE_SPEC(FN2CCppPlacementSpec, "NodeToCode.Translation.CppPlacement", N2C_TEST_FLAGS)

	TUniquePtr<N2CTest::FScratchDir> Scratch;
	FString SourceDir;

	FString Read(const FString& Path) const
	{
		FString Text;
		FFileHelper::LoadFileToString(Text, *Path);
		return Text;
	}

END_DEFINE_SPEC(FN2CCppPlacementSpec)

void FN2CCppPlacementSpec::Define()
{
	using namespace N2CCppPlacementSpec;

	BeforeEach([this]()
	{
		Scratch = MakeUnique<N2CTest::FScratchDir>(TEXT("N2CCppPlacement"));
		SourceDir = *Scratch / TEXT("Source/N2CPlaceTest");
		FFileHelper::SaveStringToFile(BuildCs, *(SourceDir / TEXT("N2CPlaceTest.Build.cs")));
		IFileManager::Get().MakeDirectory(*(SourceDir / TEXT("Public")), true);
		IFileManager::Get().MakeDirectory(*(SourceDir / TEXT("Private")), true);
	});

	AfterEach([this]()
	{
		Scratch.Reset();
	});

	It(TEXT("resolves the includes and modules of UKismetSystemLibrary and UGameplayStatics calls, and lists Engine as missing"), [this]()
	{
		FN2CCppPlacementPlan Plan;
		FString Error;
		if (!TestTrue(TEXT("plan: ") + Error, FN2CCppPlacement::BuildPlan(MakeRequest(SourceDir), Plan, Error)))
		{
			return;
		}
		TestTrue(TEXT("KismetSystemLibrary include"), Plan.Includes.Contains(TEXT("Kismet/KismetSystemLibrary.h")));
		TestTrue(TEXT("GameplayStatics include"), Plan.Includes.Contains(TEXT("Kismet/GameplayStatics.h")));
		TestTrue(TEXT("Engine required"), Plan.RequiredModules.Contains(TEXT("Engine")));
		TestTrue(TEXT("Engine missing from the Build.cs"), Plan.MissingModules.Contains(TEXT("Engine")));
		TestFalse(TEXT("Core is listed already"), Plan.MissingModules.Contains(TEXT("Core")));
		TestTrue(TEXT("the Build.cs gains Engine"), Plan.BuildCsContent.Contains(TEXT("\"CoreUObject\", \"Engine\"")));
		TestTrue(TEXT("the Blueprint member_parent is reported, not included"),
			Plan.Warnings.ContainsByPredicate([](const FString& Warning) { return Warning.Contains(TEXT("BP_Placed")); }));
	});

	It(TEXT("writes a UCLASS scaffold: generated.h last, parent from parent_class, declarations in the body, structs before it"), [this]()
	{
		FN2CCppPlacementPlan Plan;
		FString Error;
		if (!TestTrue(TEXT("plan: ") + Error, FN2CCppPlacement::BuildPlan(MakeRequest(SourceDir), Plan, Error)))
		{
			return;
		}
		const FString& Header = Plan.Header.Content;
		TestEqual(TEXT("class name"), Plan.ClassName, FString(TEXT("APlacedActor")));
		TestEqual(TEXT("parent"), Plan.ParentClassName, FString(TEXT("AActor")));
		TestTrue(TEXT("parent include"), Header.Contains(TEXT("#include \"GameFramework/Actor.h\"")));
		TestTrue(TEXT("class line"), Header.Contains(TEXT("class N2CPLACETEST_API APlacedActor : public AActor")));
		TestTrue(TEXT("GENERATED_BODY"), Header.Contains(TEXT("GENERATED_BODY()")));
		const int32 Generated = Header.Find(TEXT("#include \"PlacedActor.generated.h\""));
		TestTrue(TEXT("generated.h is the last include"), Generated != INDEX_NONE && Header.Find(TEXT("#include"), ESearchCase::CaseSensitive, ESearchDir::FromEnd) == Generated);
		TestTrue(TEXT("the struct comes before the class"), Header.Find(TEXT("struct FAmmo")) < Header.Find(TEXT("UCLASS()")));
		TestTrue(TEXT("the declaration is inside the class"), Header.Find(TEXT("\tvoid Fire();")) > Header.Find(TEXT("public:")));
		TestTrue(TEXT("header in Public/"), Plan.Header.Path.EndsWith(TEXT("/Public/PlacedActor.h")));
		TestTrue(TEXT("source in Private/"), Plan.Source.Path.EndsWith(TEXT("/Private/PlacedActor.cpp")));

		const FString& Source = Plan.Source.Content;
		TestTrue(TEXT("the .cpp includes its header"), Source.Contains(TEXT("#include \"PlacedActor.h\"")));
		TestTrue(TEXT("the .cpp includes KismetSystemLibrary"), Source.Contains(TEXT("#include \"Kismet/KismetSystemLibrary.h\"")));
		TestTrue(TEXT("the Blueprint qualifier is renamed"), Source.Contains(TEXT("void APlacedActor::Fire()")) && !Source.Contains(TEXT("ABP_Placed_C::")));
		TestTrue(TEXT("native qualifiers stay"), Source.Contains(TEXT("UKismetSystemLibrary::PrintString")));
		TestTrue(TEXT("the notes are a comment"), Source.Contains(TEXT("Implementation notes from the translation")));
	});

	It(TEXT("writes nothing in a dry run plan, writes the files and the Build.cs on Apply, and backs up what it replaces"), [this]()
	{
		FN2CCppPlacementPlan Plan;
		FString Error;
		if (!TestTrue(TEXT("plan: ") + Error, FN2CCppPlacement::BuildPlan(MakeRequest(SourceDir), Plan, Error)))
		{
			return;
		}
		TestFalse(TEXT("planning wrote no header"), FPaths::FileExists(Plan.Header.Path));

		TArray<FString> Written, Backups;
		TestTrue(TEXT("apply: ") + Error, FN2CCppPlacement::Apply(Plan, Written, Backups, Error));
		TestEqual(TEXT("three files written"), Written.Num(), 3);
		TestEqual(TEXT("the Build.cs existed: one backup"), Backups.Num(), 1);
		TestTrue(TEXT("the Build.cs lists Engine"), Read(Plan.BuildCsPath).Contains(TEXT("\"Engine\"")));
		TestEqual(TEXT("the header was written"), Read(Plan.Header.Path), Plan.Header.Content);

		// Again: the header and .cpp exist now and are backed up; the Build.cs needs nothing more
		FN2CCppPlacementPlan Second;
		TestTrue(TEXT("second plan"), FN2CCppPlacement::BuildPlan(MakeRequest(SourceDir), Second, Error));
		TestTrue(TEXT("the header exists"), Second.Header.bExists);
		TestTrue(TEXT("nothing missing now"), Second.MissingModules.Num() == 0 && Second.BuildCsContent.IsEmpty());
		Written.Reset();
		Backups.Reset();
		TestTrue(TEXT("second apply"), FN2CCppPlacement::Apply(Second, Written, Backups, Error));
		TestEqual(TEXT("header and .cpp backed up"), Backups.Num(), 2);
		TestTrue(TEXT("backup file"), FPaths::FileExists(Second.Header.Path + TEXT(".bak")));
	});

	It(TEXT("refuses a path outside the module's Source folder, a bad subfolder and a bad class name"), [this]()
	{
		TestFalse(TEXT("sibling folder"), FN2CCppPlacement::IsInside(*Scratch / TEXT("Source/Other/X.h"), SourceDir));
		TestFalse(TEXT("dot-dot"), FN2CCppPlacement::IsInside(SourceDir / TEXT("../Other/X.h"), SourceDir));
		TestTrue(TEXT("inside"), FN2CCppPlacement::IsInside(SourceDir / TEXT("Public/X.h"), SourceDir));

		FN2CCppPlacementPlan Plan;
		FString Error;
		FN2CCppPlacementRequest Request = MakeRequest(SourceDir);
		Request.Subfolder = TEXT("../../Escape");
		TestFalse(TEXT("subfolder with .."), FN2CCppPlacement::BuildPlan(Request, Plan, Error));
		TestTrue(TEXT("says why: ") + Error, Error.Contains(TEXT("subfolder")));
		Request.Subfolder = TEXT("C:/Windows");
		TestFalse(TEXT("absolute subfolder"), FN2CCppPlacement::BuildPlan(Request, Plan, Error));
		Request.Subfolder = TEXT("Weapons/Ranged");
		TestTrue(TEXT("nested subfolder: ") + Error, FN2CCppPlacement::BuildPlan(Request, Plan, Error));
		TestTrue(TEXT("header in Public/Weapons/Ranged"), Plan.Header.Path.EndsWith(TEXT("/Public/Weapons/Ranged/PlacedActor.h")));
		TestTrue(TEXT("the .cpp includes the subfolder path"), Plan.Source.Content.Contains(TEXT("#include \"Weapons/Ranged/PlacedActor.h\"")));

		Request = MakeRequest(SourceDir);
		Request.ClassName = TEXT("Bad Name");
		TestFalse(TEXT("class name with a space"), FN2CCppPlacement::BuildPlan(Request, Plan, Error));
		Request = MakeRequest(SourceDir);
		Request.ParentClass = TEXT("NoSuchClassAnywhere");
		TestFalse(TEXT("unknown parent"), FN2CCppPlacement::BuildPlan(Request, Plan, Error));
	});

	It(TEXT("adds modules to a Build.cs list, and says so when it has none"), [this]()
	{
		FString Out, Error;
		TestTrue(TEXT("private list preferred"), FN2CCppPlacement::AddModulesToBuildCs(
			TEXT("PublicDependencyModuleNames.AddRange(new string[] { \"Core\" });\nPrivateDependencyModuleNames.AddRange(new string[]\n{\n\t\"Slate\",\n});"),
			{ TEXT("Engine") }, Out, Error));
		TestTrue(TEXT("added after the trailing comma: ") + Out, Out.Contains(TEXT("\"Slate\", \"Engine\",")));
		TestTrue(TEXT("empty list"), FN2CCppPlacement::AddModulesToBuildCs(TEXT("PublicDependencyModuleNames.AddRange(new string[] {});"), { TEXT("Engine") }, Out, Error));
		TestTrue(TEXT("into the empty list: ") + Out, Out.Contains(TEXT("{ \"Engine\" }")));
		TestFalse(TEXT("no list"), FN2CCppPlacement::AddModulesToBuildCs(TEXT("public class X : ModuleRules {}"), { TEXT("Engine") }, Out, Error));
		TestTrue(TEXT("names the problem"), Error.Contains(TEXT("by hand")));
		TestTrue(TEXT("listed modules"), FN2CCppPlacement::GetListedModules(BuildCs).Contains(TEXT("CoreUObject")));
	});

	It(TEXT("finds the project's own module and refuses an engine module and an unknown one"), [this]()
	{
		FString Dir, Error;
		TestFalse(TEXT("Engine is not a project module"), FN2CCppPlacement::FindModule(TEXT("Engine"), Dir, Error));
		TestTrue(TEXT("says so: ") + Error, Error.Contains(TEXT("not a module of this project")));
		TestFalse(TEXT("NodeToCode itself"), FN2CCppPlacement::FindModule(TEXT("NodeToCode"), Dir, Error));
		const FProjectDescriptor* Project = IProjectManager::Get().GetCurrentProject();
		if (Project && Project->Modules.Num() > 0)
		{
			const FString Name = Project->Modules[0].Name.ToString();
			TestTrue(TEXT("the host's module: ") + Error, FN2CCppPlacement::FindModule(Name, Dir, Error));
			TestTrue(TEXT("under the project's Source"), Dir.Contains(TEXT("/Source/") + Name));
		}
	});

	It(TEXT("place-cpp-translation answers a dry run on a saved run with structuredContent matching its outputSchema, and writes nothing"), [this]()
	{
		const FProjectDescriptor* Project = IProjectManager::Get().GetCurrentProject();
		if (!Project || Project->Modules.Num() == 0)
		{
			AddInfo(TEXT("The host project has no C++ module; skipped"));
			return;
		}
		// A saved MCP run of a C++ translation, as FN2CTranslationArchive writes it
		const FString RunName = FString::Printf(TEXT("N2CTestPlace_%s"), *FGuid::NewGuid().ToString(EGuidFormats::Digits));
		const FString Run = FPaths::ConvertRelativePathToFull(FN2CPathUtils::GetTranslationsBasePath()) / RunName;
		ON_SCOPE_EXIT { IFileManager::Get().DeleteDirectory(*Run, false, true); };
		FFileHelper::SaveStringToFile(TEXT("{\"kind\":\"mcp\",\"language\":\"cpp\"}"), *(Run / TEXT("manifest.json")));
		FFileHelper::SaveStringToFile(TEXT("{\"graphs\":[{\"graph_name\":\"Fire\",\"graph_type\":\"Function\",\"graph_class\":\"\",")
			TEXT("\"code\":{\"graphDeclaration\":\"void Fire();\",\"graphImplementation\":\"void ABP_X_C::Fire()\n{\n\tUKismetSystemLibrary::PrintString(this);\n}\",")
			TEXT("\"implementationNotes\":\"\"}}]}"), *(Run / FString::Printf(TEXT("N2C_Translation_%s.json"), *RunName)));

		const TSharedRef<FJsonObject> Args = MakeShared<FJsonObject>();
		Args->SetStringField(TEXT("module"), Project->Modules[0].Name.ToString());
		Args->SetStringField(TEXT("class_name"), TEXT("N2CTestPlaced"));
		Args->SetStringField(TEXT("parent_class"), TEXT("Actor"));
		Args->SetStringField(TEXT("run_folder"), RunName);
		const FMcpToolCallResult Result = N2CTest::RunTool(TEXT("place-cpp-translation"), Args);
		if (!TestFalse(TEXT("succeeds"), Result.bIsError) || !TestTrue(TEXT("structuredContent"), Result.StructuredContent.IsValid()))
		{
			return;
		}
		TArray<FString> Errors;
		N2CMcpOutputSchema::Validate(FN2CMcpPlaceCppTranslationTool().GetDefinition().OutputSchema,
			MakeShared<FJsonValueObject>(Result.StructuredContent), Errors);
		TestEqual(TEXT("matches the outputSchema: ") + FString::Join(Errors, TEXT("; ")), Errors.Num(), 0);
		TestTrue(TEXT("a dry run"), Result.StructuredContent->GetBoolField(TEXT("dryRun")));
		const FString HeaderPath = Result.StructuredContent->GetObjectField(TEXT("header"))->GetStringField(TEXT("path"));
		TestFalse(TEXT("nothing written"), FPaths::FileExists(HeaderPath));
		TestTrue(TEXT("the include"), Result.StructuredContent->GetArrayField(TEXT("includes")).ContainsByPredicate(
			[](const TSharedPtr<FJsonValue>& V) { return V->AsString() == TEXT("Kismet/KismetSystemLibrary.h"); }));
	});

	It(TEXT("place-cpp-translation refuses a run folder outside the translation output directory"), [this]()
	{
		FString Folder, Error;
		TestFalse(TEXT("outside"), FN2CMcpPlaceCppTranslationTool::ResolveRunFolder(FPaths::ProjectSavedDir() / TEXT("Config"), Folder, Error));
		TestFalse(TEXT("dot-dot"), FN2CMcpPlaceCppTranslationTool::ResolveRunFolder(TEXT("../../Config"), Folder, Error));
		TestTrue(TEXT("names the directory: ") + Error, Error.Contains(TEXT("translation output directory")));
	});
}

#endif // WITH_DEV_AUTOMATION_TESTS
