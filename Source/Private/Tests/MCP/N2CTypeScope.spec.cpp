// Copyright (c) 2026 Nick McClure (Protospatial)
// SPDX-License-Identifier: Apache-2.0

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Tests/N2CTestHelpers.h"
#include "Tests/N2CToolSpecHarness.h"
#include "Tests/N2CTestBlueprintFactory.h"
#include "MCP/Utils/N2CTypeScope.h"
#include "MCP/Utils/N2CPinTypeCodec.h"
#include "MCP/Utils/N2CMcpVariableUtils.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Dom/JsonObject.h"
#include "EdGraphSchema_K2.h"
#include "Engine/Blueprint.h"
#include "Engine/BlueprintGeneratedClass.h"
#include "Engine/EngineTypes.h"
#include "Engine/HitResult.h"
#include "GameFramework/Actor.h"
#include "Interfaces/IPluginManager.h"
#include "Interfaces/IProjectManager.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "ProjectDescriptor.h"

namespace N2CTypeScopeSpec
{
/**
 * Card 0348 (review arch-arch-mcp-7): one classifier of where a type comes from, and one type description.
 * - FN2CTypeScope: /Script/<Module> by the module's owner (a plugin's descriptor, the project's, or the engine), content
 *   by its mount point. search-blueprint-classes knew four engine roots (so /Script/AIModule was a plugin class) and
 *   tested Contains("/Plugins/"), which no object path contains (so a plugin's Blueprints were never left out).
 * - N2C::PinType::Describe: list-overridable-functions described a TSubclassOf<AActor> parameter as objectType Class
 *   (the property's own class, not its meta class) and soft classes as objects; BuildTypeInfo is its condensed form.
 * - Every typeIdentifier search-variable-types returns parses with N2C::PinType::Parse.
 */

	/** The entry of an array of objects whose Field is Value, or null */
	TSharedPtr<FJsonObject> FindBy(const TArray<TSharedPtr<FJsonValue>>* Array, const TCHAR* Field, const FString& Value)
	{
		if (Array)
		{
			for (const TSharedPtr<FJsonValue>& Item : *Array)
			{
				const TSharedPtr<FJsonObject> Object = Item.IsValid() ? Item->AsObject() : nullptr;
				FString Text;
				if (Object.IsValid() && Object->TryGetStringField(Field, Text) && Text == Value)
				{
					return Object;
				}
			}
		}
		return nullptr;
	}

	FString Str(const TSharedPtr<FJsonObject>& Object, const TCHAR* Field)
	{
		FString Text;
		return Object.IsValid() && Object->TryGetStringField(Field, Text) ? Text : FString(TEXT("<missing>"));
	}

	TSharedPtr<FJsonObject> Obj(const TSharedPtr<FJsonObject>& Object, const TCHAR* Field)
	{
		const TSharedPtr<FJsonObject>* Inner = nullptr;
		return Object.IsValid() && Object->TryGetObjectField(Field, Inner) ? *Inner : nullptr;
	}

	/** search-blueprint-classes with the largest page; the classes array (empty when the call failed) */
	TArray<TSharedPtr<FJsonValue>> SearchClasses(const FString& Term, bool bIncludeEngine, bool bIncludePlugin)
	{
		const TSharedPtr<FJsonObject> Args = MakeShared<FJsonObject>();
		Args->SetStringField(TEXT("searchTerm"), Term);
		Args->SetBoolField(TEXT("includeEngineClasses"), bIncludeEngine);
		Args->SetBoolField(TEXT("includePluginClasses"), bIncludePlugin);
		Args->SetNumberField(TEXT("maxResults"), 200);
		const TSharedPtr<FJsonObject> Json = N2CTest::ResultJson(N2CTest::RunTool(TEXT("search-blueprint-classes"), Args));
		const TArray<TSharedPtr<FJsonValue>>* Classes = nullptr;
		return Json.IsValid() && Json->TryGetArrayField(TEXT("classes"), Classes) ? *Classes : TArray<TSharedPtr<FJsonValue>>();
	}

	void WaitForAssetRegistry()
	{
		IAssetRegistry& AssetRegistry = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry").Get();
		if (AssetRegistry.IsLoadingAssets())
		{
			AssetRegistry.WaitForCompletion();
		}
	}

	FEdGraphPinType ParsedOrEmpty(const TCHAR* Text)
	{
		const TValueOrError<FEdGraphPinType, FText> Parsed = N2C::PinType::Parse(Text);
		return Parsed.HasValue() ? Parsed.GetValue() : FEdGraphPinType();
	}
}

BEGIN_DEFINE_SPEC(FN2CTypeScopeSpec, "NodeToCode.MCP.TypeScope", N2C_TEST_FLAGS)
	void ExpectScope(const FN2CTypeScope& Scopes, const FString& Path, EN2CTypeScope Expected)
	{
		const EN2CTypeScope Got = Scopes.Classify(Path);
		TestEqual(FString::Printf(TEXT("%s is %s"), *Path, FN2CTypeScope::ToString(Expected)),
			FString(FN2CTypeScope::ToString(Got)), FString(FN2CTypeScope::ToString(Expected)));
	}
END_DEFINE_SPEC(FN2CTypeScopeSpec)

void FN2CTypeScopeSpec::Define()
{
	using namespace N2CTypeScopeSpec;

	Describe(TEXT("FN2CTypeScope::Classify"), [this]()
	{
		It(TEXT("puts every engine module and /Engine content in the engine, /Game and the project's modules in the project"), [this]()
		{
			const FN2CTypeScope Scopes;
			ExpectScope(Scopes, TEXT("/Script/Engine.Actor"), EN2CTypeScope::Engine);
			ExpectScope(Scopes, TEXT("/Script/CoreUObject.Object"), EN2CTypeScope::Engine);
			ExpectScope(Scopes, TEXT("/Script/AIModule.AIController"), EN2CTypeScope::Engine);
			ExpectScope(Scopes, TEXT("/Script/Engine"), EN2CTypeScope::Engine);
			ExpectScope(Scopes, TEXT("/Engine/BasicShapes/Cube.Cube"), EN2CTypeScope::Engine);
			ExpectScope(Scopes, TEXT("/Game/BP_Door.BP_Door_C"), EN2CTypeScope::Project);
			ExpectScope(Scopes, TEXT("/Game/Doors"), EN2CTypeScope::Project);
			ExpectScope(Scopes, TEXT("/Script/Engine.BlueprintGeneratedClass'/Game/BP_Door.BP_Door_C'"), EN2CTypeScope::Project);
			ExpectScope(Scopes, TEXT("/Temp/N2CTest_0/BP_Temp.BP_Temp_C"), EN2CTypeScope::Project);

			const FProjectDescriptor* Project = IProjectManager::Get().GetCurrentProject();
			if (TestTrue(TEXT("the host project has a module"), Project && Project->Modules.Num() > 0))
			{
				// A project's native types: /Script/<ProjectModule>, which search-variable-types once took for engine types
				ExpectScope(Scopes, FString::Printf(TEXT("/Script/%s.MyNativeActor"), *Project->Modules[0].Name.ToString()), EN2CTypeScope::Project);
			}
		});

		It(TEXT("puts a plugin's modules and content in that plugin's scope"), [this]()
		{
			const FN2CTypeScope Scopes;
			const TSharedPtr<IPlugin> NodeToCode = IPluginManager::Get().FindPlugin(TEXT("NodeToCode"));
			if (TestTrue(TEXT("NodeToCode is a plugin"), NodeToCode.IsValid()))
			{
				const EN2CTypeScope Expected = NodeToCode->GetLoadedFrom() == EPluginLoadedFrom::Engine ? EN2CTypeScope::EnginePlugin : EN2CTypeScope::ProjectPlugin;
				ExpectScope(Scopes, TEXT("/Script/NodeToCode.N2CTagInfo"), Expected);
				ExpectScope(Scopes, TEXT("/NodeToCode/Somewhere/BP_InPlugin.BP_InPlugin_C"), Expected);
			}
			// The fixture plugin build.ps1 links into every host: content only, loaded from the project
			ExpectScope(Scopes, TEXT("/NodeToCodeTestContent/Dependencies/BP_N2CDep_A.BP_N2CDep_A_C"), EN2CTypeScope::ProjectPlugin);
			ExpectScope(Scopes, TEXT("/Script/Engine.BlueprintGeneratedClass'/NodeToCodeTestContent/Dependencies/BP_N2CDep_A.BP_N2CDep_A_C'"), EN2CTypeScope::ProjectPlugin);

			// An engine plugin with a module, and one with content
			FString EngineModule, EngineMount;
			for (const TSharedRef<IPlugin>& Plugin : IPluginManager::Get().GetEnabledPlugins())
			{
				if (Plugin->GetLoadedFrom() != EPluginLoadedFrom::Engine)
				{
					continue;
				}
				if (EngineModule.IsEmpty() && Plugin->GetDescriptor().Modules.Num() > 0)
				{
					EngineModule = Plugin->GetDescriptor().Modules[0].Name.ToString();
				}
				if (EngineMount.IsEmpty() && Plugin->CanContainContent())
				{
					EngineMount = Plugin->GetName();
				}
			}
			if (TestFalse(TEXT("an engine plugin with a module is enabled"), EngineModule.IsEmpty()))
			{
				ExpectScope(Scopes, FString::Printf(TEXT("/Script/%s.SomeType"), *EngineModule), EN2CTypeScope::EnginePlugin);
			}
			if (TestFalse(TEXT("an engine plugin with content is enabled"), EngineMount.IsEmpty()))
			{
				ExpectScope(Scopes, FString::Printf(TEXT("/%s/Folder/Asset.Asset"), *EngineMount), EN2CTypeScope::EnginePlugin);
			}
		});
	});

	Describe(TEXT("search-blueprint-classes"), [this]()
	{
		It(TEXT("leaves out every engine module with includeEngineClasses false, not only four"), [this]()
		{
			const TArray<TSharedPtr<FJsonValue>> All = SearchClasses(TEXT("AIController"), true, true);
			TestTrue(TEXT("AIController listed by default"), FindBy(&All, TEXT("classPath"), TEXT("/Script/AIModule.AIController")).IsValid());
			const TArray<TSharedPtr<FJsonValue>> NoEngine = SearchClasses(TEXT("AIController"), false, true);
			TestFalse(TEXT("includeEngineClasses false leaves /Script/AIModule.AIController out"),
				FindBy(&NoEngine, TEXT("classPath"), TEXT("/Script/AIModule.AIController")).IsValid());
			// AIModule is an engine module, not a plugin's
			const TArray<TSharedPtr<FJsonValue>> NoPlugin = SearchClasses(TEXT("AIController"), true, false);
			TestTrue(TEXT("includePluginClasses false keeps /Script/AIModule.AIController"),
				FindBy(&NoPlugin, TEXT("classPath"), TEXT("/Script/AIModule.AIController")).IsValid());
		});

		It(TEXT("leaves out a plugin's content Blueprints with includePluginClasses false"), [this]()
		{
			WaitForAssetRegistry();
			const FString Path = TEXT("/NodeToCodeTestContent/Dependencies/BP_N2CDep_A.BP_N2CDep_A_C");
			const TArray<TSharedPtr<FJsonValue>> All = SearchClasses(TEXT("N2CDep"), true, true);
			if (!TestTrue(TEXT("BP_N2CDep_A (plugin content) listed by default"), FindBy(&All, TEXT("classPath"), Path).IsValid()))
			{
				return;
			}
			const TArray<TSharedPtr<FJsonValue>> NoPlugin = SearchClasses(TEXT("N2CDep"), true, false);
			TestFalse(TEXT("includePluginClasses false leaves BP_N2CDep_A out"), FindBy(&NoPlugin, TEXT("classPath"), Path).IsValid());
			const TArray<TSharedPtr<FJsonValue>> NoEngine = SearchClasses(TEXT("N2CDep"), false, true);
			TestTrue(TEXT("includeEngineClasses false keeps it (a project plugin)"), FindBy(&NoEngine, TEXT("classPath"), Path).IsValid());
		});
	});

	Describe(TEXT("search-variable-types"), [this]()
	{
		It(TEXT("returns identifiers N2C::PinType::Parse reads, for the first 200 results of broad searches"), [this]()
		{
			WaitForAssetRegistry();
			// searchTerm cannot be empty: single letters match nearly every type of every kind
			int32 Checked = 0;
			TSet<FString> Seen;
			for (const TCHAR* Term : { TEXT("e"), TEXT("a"), TEXT("o") })
			{
				const TSharedPtr<FJsonObject> Args = MakeShared<FJsonObject>();
				Args->SetStringField(TEXT("searchTerm"), Term);
				Args->SetNumberField(TEXT("maxResults"), 200);
				const TSharedPtr<FJsonObject> Json = N2CTest::ResultJson(N2CTest::RunTool(TEXT("search-variable-types"), Args));
				const TArray<TSharedPtr<FJsonValue>>* Types = nullptr;
				if (!TestTrue(FString::Printf(TEXT("results for '%s'"), Term), Json.IsValid() && Json->TryGetArrayField(TEXT("types"), Types) && Types->Num() > 0))
				{
					continue;
				}
				for (const TSharedPtr<FJsonValue>& Value : *Types)
				{
					const FString Identifier = Str(Value->AsObject(), TEXT("typeIdentifier"));
					if (Seen.Contains(Identifier))
					{
						continue;
					}
					Seen.Add(Identifier);
					++Checked;
					const TValueOrError<FEdGraphPinType, FText> Parsed = N2C::PinType::Parse(Identifier);
					TestTrue(FString::Printf(TEXT("'%s' (%s) parses: %s"), *Identifier, *Str(Value->AsObject(), TEXT("category")),
						Parsed.HasError() ? *Parsed.GetError().ToString() : TEXT("")), Parsed.HasValue());
				}
			}
			TestTrue(FString::Printf(TEXT("checked many identifiers (%d)"), Checked), Checked >= 200);
		});
	});

	Describe(TEXT("N2C::PinType::Describe"), [this]()
	{
		It(TEXT("names the type each reference and container holds"), [this]()
		{
			const TSharedPtr<FJsonObject> Cls = N2C::PinType::Describe(ParsedOrEmpty(TEXT("class</Script/Engine.Actor>")));
			TestEqual(TEXT("class type"), Str(Cls, TEXT("type")), FString(TEXT("class")));
			TestEqual(TEXT("class objectPath is the meta class"), Str(Cls, TEXT("objectPath")), FString(TEXT("/Script/Engine.Actor")));
			const TSharedPtr<FJsonObject> Soft = N2C::PinType::Describe(ParsedOrEmpty(TEXT("softclass</Script/Engine.Actor>")));
			TestEqual(TEXT("softclass type"), Str(Soft, TEXT("type")), FString(TEXT("softclass")));
			TestEqual(TEXT("softclass objectType"), Str(Soft, TEXT("objectType")), FString(TEXT("Actor")));
			const TSharedPtr<FJsonObject> Real = N2C::PinType::Describe(ParsedOrEmpty(TEXT("double")));
			TestEqual(TEXT("double category"), Str(Real, TEXT("category")), FString(TEXT("real")));
			TestEqual(TEXT("double subCategory"), Str(Real, TEXT("subCategory")), FString(TEXT("double")));
			const TSharedPtr<FJsonObject> Map = N2C::PinType::Describe(ParsedOrEmpty(TEXT("map<name, /Script/Engine.HitResult>")));
			TestEqual(TEXT("map container"), Str(Map, TEXT("container")), FString(TEXT("map")));
			TestEqual(TEXT("map key"), Str(Obj(Map, TEXT("keyType")), TEXT("type")), FString(TEXT("name")));
			TestEqual(TEXT("map value"), Str(Obj(Map, TEXT("valueType")), TEXT("structPath")), FString(TEXT("/Script/Engine.HitResult")));
			const TSharedPtr<FJsonObject> Enum = N2C::PinType::Describe(ParsedOrEmpty(TEXT("array</Script/Engine.ECollisionChannel>")));
			TestEqual(TEXT("array innerType enum"), Str(Obj(Enum, TEXT("innerType")), TEXT("enumType")), FString(TEXT("ECollisionChannel")));
		});

		It(TEXT("is what BuildTypeInfo condenses, with the same fields as before"), [this]()
		{
			struct FRow { const TCHAR* Text; const TCHAR* Category; const TCHAR* NameField; const TCHAR* Name; };
			const FRow Rows[] = {
				{ TEXT("/Script/Engine.Actor"), TEXT("object"), TEXT("className"), TEXT("Actor") },
				{ TEXT("/Script/Engine.HitResult"), TEXT("struct"), TEXT("structName"), TEXT("HitResult") },
				{ TEXT("/Script/Engine.ECollisionChannel"), TEXT("enum"), TEXT("enumName"), TEXT("ECollisionChannel") },
				{ TEXT("array</Script/Engine.HitResult>"), TEXT("struct"), TEXT("structName"), TEXT("HitResult") },
				{ TEXT("map</Script/Engine.Actor, int>"), TEXT("object"), TEXT("className"), TEXT("Actor") },
				{ TEXT("int"), TEXT("primitive"), TEXT("typeName"), TEXT("int") },
				{ TEXT("class</Script/Engine.Actor>"), TEXT("primitive"), TEXT("typeName"), TEXT("class") },
			};
			for (const FRow& Row : Rows)
			{
				const TSharedPtr<FJsonObject> Info = FN2CMcpVariableUtils::BuildTypeInfo(ParsedOrEmpty(Row.Text));
				TestEqual(FString::Printf(TEXT("%s category"), Row.Text), Str(Info, TEXT("category")), FString(Row.Category));
				TestEqual(FString::Printf(TEXT("%s %s"), Row.Text, Row.NameField), Str(Info, Row.NameField), FString(Row.Name));
			}
		});

		It(TEXT("gives list-overridable-functions the meta class of a class parameter"), [this]()
		{
			N2CTest::FTestBlueprint Base(TEXT("BP_N2CTypeScopeBase"));
			UBlueprint* Blueprint = Base.GetBlueprint();
			UEdGraph* Function = Blueprint ? Base.AddFunction(TEXT("TakeTypes")) : nullptr;
			if (!TestNotNull(TEXT("base function"), Function))
			{
				return;
			}
			Base.AddFunctionInput(Function, TEXT("Cls"), ParsedOrEmpty(TEXT("class</Script/Engine.Actor>")));
			Base.AddFunctionInput(Function, TEXT("Soft"), ParsedOrEmpty(TEXT("softclass</Script/Engine.Actor>")));
			Base.AddFunctionInput(Function, TEXT("Ints"), ParsedOrEmpty(TEXT("array<int>")));
			N2CTest::ExpectCompilesClean(*this, TEXT("base"), Blueprint);
			UBlueprint* Child = FKismetEditorUtilities::CreateBlueprint(Blueprint->GeneratedClass, Base.GetPackage(), TEXT("BP_N2CTypeScopeChild"),
				BPTYPE_Normal, UBlueprint::StaticClass(), UBlueprintGeneratedClass::StaticClass());
			if (!TestNotNull(TEXT("child Blueprint"), Child))
			{
				return;
			}
			N2CTest::ExpectCompilesClean(*this, TEXT("child"), Child);

			const TSharedPtr<FJsonObject> Args = MakeShared<FJsonObject>();
			Args->SetStringField(TEXT("blueprintPath"), Child->GetPathName());
			const TSharedPtr<FJsonObject> Json = N2CTest::ResultJson(N2CTest::RunTool(TEXT("list-overridable-functions"), Args));
			const TArray<TSharedPtr<FJsonValue>>* Functions = nullptr;
			const TSharedPtr<FJsonObject> Take = Json.IsValid() && Json->TryGetArrayField(TEXT("functions"), Functions)
				? FindBy(Functions, TEXT("name"), TEXT("TakeTypes")) : nullptr;
			const TArray<TSharedPtr<FJsonValue>>* Inputs = nullptr;
			if (!TestTrue(TEXT("TakeTypes listed with inputs"), Take.IsValid() && Take->TryGetArrayField(TEXT("inputs"), Inputs)))
			{
				return;
			}
			const TSharedPtr<FJsonObject> Cls = Obj(FindBy(Inputs, TEXT("name"), TEXT("Cls")), TEXT("type"));
			TestEqual(TEXT("Cls type"), Str(Cls, TEXT("type")), FString(TEXT("class")));
			TestEqual(TEXT("Cls objectType is Actor, not Class"), Str(Cls, TEXT("objectType")), FString(TEXT("Actor")));
			const TSharedPtr<FJsonObject> Soft = Obj(FindBy(Inputs, TEXT("name"), TEXT("Soft")), TEXT("type"));
			TestEqual(TEXT("Soft type"), Str(Soft, TEXT("type")), FString(TEXT("softclass")));
			TestEqual(TEXT("Soft objectType"), Str(Soft, TEXT("objectType")), FString(TEXT("Actor")));
			const TSharedPtr<FJsonObject> Ints = Obj(FindBy(Inputs, TEXT("name"), TEXT("Ints")), TEXT("type"));
			TestEqual(TEXT("Ints container"), Str(Ints, TEXT("container")), FString(TEXT("array")));
			TestEqual(TEXT("Ints innerType"), Str(Obj(Ints, TEXT("innerType")), TEXT("type")), FString(TEXT("int")));
			TestEqual(TEXT("Ints innerType cppType"), Str(Obj(Ints, TEXT("innerType")), TEXT("cppType")), FString(TEXT("int32")));
		});
	});
}

#endif // WITH_DEV_AUTOMATION_TESTS
