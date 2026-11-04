// Copyright (c) 2026 Nick McClure (Protospatial)
// SPDX-License-Identifier: Apache-2.0

#include "Tests/Benchmark/N2CBenchmarkCorpus.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Blueprint/UserWidget.h"
#include "Blueprint/WidgetBlueprintGeneratedClass.h"
#include "Components/ActorComponent.h"
#include "Components/SceneComponent.h"
#include "Components/SphereComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Dom/JsonValue.h"
#include "EdGraph/EdGraph.h"
#include "EdGraphSchema_K2.h"
#include "Engine/Blueprint.h"
#include "Engine/BlueprintGeneratedClass.h"
#include "Engine/SCS_Node.h"
#include "Engine/SimpleConstructionScript.h"
#include "Engine/StaticMesh.h"
#include "Engine/TimelineTemplate.h"
#include "Engine/UserDefinedEnum.h"
#include "GameFramework/Actor.h"
#include "GameFramework/Pawn.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformMisc.h"
#include "Interfaces/IPluginManager.h"
#include "K2Node_CallFunction.h"
#include "K2Node_Composite.h"
#include "K2Node_CustomEvent.h"
#include "K2Node_DynamicCast.h"
#include "K2Node_Event.h"
#include "K2Node_ExecutionSequence.h"
#include "K2Node_FormatText.h"
#include "K2Node_FunctionEntry.h"
#include "K2Node_FunctionResult.h"
#include "K2Node_IfThenElse.h"
#include "K2Node_MacroInstance.h"
#include "K2Node_MakeStruct.h"
#include "K2Node_BreakStruct.h"
#include "K2Node_SwitchEnum.h"
#include "K2Node_Timeline.h"
#include "K2Node_Variable.h"
#include "K2Node_VariableGet.h"
#include "Kismet/GameplayStatics.h"
#include "Kismet/KismetMathLibrary.h"
#include "Kismet/KismetStringLibrary.h"
#include "Kismet/KismetSystemLibrary.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/CompilerResultsLog.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "MCP/Utils/N2CMcpNodeSpawnUtils.h"
#include "MCP/Utils/N2CMcpVariableUtils.h"
#include "Misc/CommandLine.h"
#include "Misc/FileHelper.h"
#include "Misc/PackageName.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "Models/N2CNode.h"
#include "Policies/PrettyJsonPrintPolicy.h"
#include "Runtime/Launch/Resources/Version.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "StructUtils/UserDefinedStruct.h"
#include "Tests/N2CTestBlueprintFactory.h"
#include "Tests/Perf/N2CPerfFixtures.h"
#include "UObject/Interface.h"
#include "UObject/Package.h"
#include "UObject/SavePackage.h"
#include "Utils/N2CStableNames.h"
#include "WidgetBlueprint.h"

#if PLATFORM_WINDOWS
#include "Windows/AllowWindowsPlatformTypes.h"
#include <windows.h>
#include "Windows/HideWindowsPlatformTypes.h"
#endif

namespace N2CTest::BenchmarkCorpus
{
	namespace
	{
		const FFixture GFixtures[] = {
			{ TEXT("BP_N2CBench_Pickup"), TEXT("an Actor with a root, a mesh and a trigger sphere; BeginPlay and Tick") },
			{ TEXT("BP_N2CBench_Math"), TEXT("functions with parameters and return values that call each other") },
			{ TEXT("BP_N2CBench_PureFunction"), TEXT("a pure function used by an impure one") },
			{ TEXT("BP_N2CBench_LocalVariables"), TEXT("a function with a local variable it reads and writes") },
			{ TEXT("BP_N2CBench_ForEach"), TEXT("ForEachLoop over an array parameter") },
			{ TEXT("BP_N2CBench_WhileLoop"), TEXT("WhileLoop on a boolean member") },
			{ TEXT("BP_N2CBench_BranchSwitch"), TEXT("Branch on a boolean and Switch on a user enum") },
			{ TEXT("BP_N2CBench_Delay"), TEXT("BeginPlay into a latent Delay, then a print") },
			{ TEXT("BP_N2CBench_Timeline"), TEXT("a timeline played from BeginPlay") },
			{ TEXT("BP_N2CBench_Dispatcher"), TEXT("an event dispatcher and a custom event") },
			{ TEXT("BP_N2CBench_Interactable"), TEXT("implements an interface function with a return value") },
			{ TEXT("BP_N2CBench_Cast"), TEXT("a cast of the player pawn to Pawn") },
			{ TEXT("BP_N2CBench_Struct"), TEXT("Make and Break of a user struct across two functions") },
			{ TEXT("BP_N2CBench_Macro"), TEXT("a macro of its own and an instance of it") },
			{ TEXT("BP_N2CBench_Collapsed"), TEXT("a collapsed graph in the event graph") },
			{ TEXT("BP_N2CBench_EnhancedInput"), TEXT("an Enhanced Input action event") },
			{ TEXT("BP_N2CBench_FormatText"), TEXT("Format Text with two arguments") },
			{ TEXT("BP_N2CBench_Sequence"), TEXT("a Sequence into two prints and a DoOnce") },
			{ TEXT("BP_N2CBench_Variables"), TEXT("member variables of many types, read by a function") },
			{ TEXT("BP_N2CBench_HealthComponent"), TEXT("an Actor Component Blueprint with a Health member and ApplyDamage") },
			{ TEXT("WBP_N2CBench_Widget"), TEXT("a Widget Blueprint with a custom event") },
			{ TEXT("BP_N2CBench_PickupChild"), TEXT("a child of BP_N2CBench_Pickup that wires its own BeginPlay") },
		};

		UPackage* NewAssetPackage(const TCHAR* AssetName, TArray<FString>& OutErrors)
		{
			const FString PackageName = FString::Printf(TEXT("%s/%s"), FolderPath, AssetName);
			if (FindPackage(nullptr, *PackageName))
			{
				OutErrors.Add(PackageName + TEXT(" is already loaded: regenerate in a fresh editor (only NodeToCode.Benchmark in the filter)"));
				return nullptr;
			}
			const FString File = FPaths::ConvertRelativePathToFull(
				FPackageName::LongPackageNameToFilename(PackageName, FPackageName::GetAssetPackageExtension()));
			if (IFileManager::Get().FileExists(*File) && !IFileManager::Get().Delete(*File, false, true))
			{
				OutErrors.Add(TEXT("could not delete the old ") + File);
				return nullptr;
			}
			UPackage* Package = CreatePackage(*PackageName);
			if (!Package)
			{
				OutErrors.Add(TEXT("could not create ") + PackageName);
			}
			return Package;
		}

		bool Compile(UBlueprint* Blueprint, TArray<FString>& OutErrors)
		{
			FCompilerResultsLog Results;
			Results.bSilentMode = true;
			FKismetEditorUtilities::CompileBlueprint(Blueprint, EBlueprintCompileOptions::SkipGarbageCollection, &Results);
			if (Results.NumErrors == 0 && Blueprint->Status != BS_Error)
			{
				return true;
			}
			for (const TSharedRef<FTokenizedMessage>& Message : Results.Messages)
			{
				if (Message->GetSeverity() == EMessageSeverity::Error)
				{
					OutErrors.Add(FString::Printf(TEXT("%s: %s"), *Blueprint->GetName(), *Message->ToText().ToString()));
				}
			}
			OutErrors.Add(FString::Printf(TEXT("%s: %d compile errors"), *Blueprint->GetName(), Results.NumErrors));
			return false;
		}

		bool Save(UObject* Asset, TArray<FString>& OutErrors, TArray<FString>& OutNotes)
		{
			UPackage* Package = Asset->GetPackage();
			const FString File = FPaths::ConvertRelativePathToFull(
				FPackageName::LongPackageNameToFilename(Package->GetName(), FPackageName::GetAssetPackageExtension()));
			Package->MarkPackageDirty();
			FSavePackageArgs Args;
			Args.TopLevelFlags = RF_Public | RF_Standalone;
			if (!UPackage::SavePackage(Package, Asset, *File, Args))
			{
				OutErrors.Add(TEXT("could not save ") + File);
				return false;
			}
			OutNotes.Add(TEXT("saved ") + File);
			return true;
		}

		UBlueprint* CreateBlueprint(UClass* Parent, UPackage* Package, const TCHAR* Name, EBlueprintType Type, TArray<FString>& OutErrors,
			TSubclassOf<UBlueprint> BlueprintClass = UBlueprint::StaticClass(),
			TSubclassOf<UBlueprintGeneratedClass> GeneratedClass = UBlueprintGeneratedClass::StaticClass())
		{
			UBlueprint* Blueprint = Package
				? FKismetEditorUtilities::CreateBlueprint(Parent, Package, FName(Name), Type, BlueprintClass, GeneratedClass)
				: nullptr;
			if (!Blueprint)
			{
				OutErrors.Add(FString::Printf(TEXT("could not create %s"), Name));
			}
			return Blueprint;
		}

		/** The Index-th visible pin of Node in Direction, exec or data */
		UEdGraphPin* NthPin(UEdGraphNode* Node, EEdGraphPinDirection Direction, bool bExec, int32 Index = 0)
		{
			if (!Node)
			{
				return nullptr;
			}
			for (UEdGraphPin* Pin : Node->Pins)
			{
				if (Pin && !Pin->bHidden && Pin->Direction == Direction
					&& (Pin->PinType.PinCategory == UEdGraphSchema_K2::PC_Exec) == bExec && Index-- == 0)
				{
					return Pin;
				}
			}
			return nullptr;
		}

		/** Links From's Index-th exec output to To's first exec input */
		bool Then(FTestBlueprint& Helper, UEdGraphNode* From, UEdGraphNode* To, int32 Index = 0)
		{
			return Helper.Link(NthPin(From, EGPD_Output, true, Index), NthPin(To, EGPD_Input, true));
		}

		UK2Node_CallFunction* Print(FTestBlueprint& Helper, UEdGraph* Graph, const TCHAR* Text)
		{
			UK2Node_CallFunction* Node = Helper.AddCall(Graph, UKismetSystemLibrary::StaticClass(),
				GET_FUNCTION_NAME_CHECKED(UKismetSystemLibrary, PrintString));
			if (UEdGraphPin* InString = Node ? Node->FindPin(TEXT("InString")) : nullptr)
			{
				InString->DefaultValue = Text;
			}
			return Node;
		}

		UK2Node_CallFunction* Math(FTestBlueprint& Helper, UEdGraph* Graph, const FName Function)
		{
			return Helper.AddCall(Graph, UKismetMathLibrary::StaticClass(), Function);
		}

		FEdGraphPinType Pin(const FName Category, UObject* SubObject = nullptr, const FName SubCategory = NAME_None)
		{
			FEdGraphPinType Type;
			Type.PinCategory = Category;
			Type.PinSubCategory = SubCategory;
			Type.PinSubCategoryObject = SubObject;
			return Type;
		}

		/** A new SCS node of Class under Parent (or a root node), as the Components panel adds one */
		USCS_Node* AddComponent(UBlueprint* Blueprint, UClass* Class, const FName Name, USCS_Node* Parent = nullptr)
		{
			USimpleConstructionScript* SCS = Blueprint ? Blueprint->SimpleConstructionScript.Get() : nullptr;
			USCS_Node* Node = SCS ? SCS->CreateNode(Class, Name) : nullptr;
			if (!Node)
			{
				return nullptr;
			}
			if (Parent)
			{
				Parent->AddChildNode(Node);
			}
			else
			{
				SCS->AddNode(Node);
			}
			SCS->ValidateSceneRootNodes();
			return Node;
		}

		/** An event dispatcher as the My Blueprint panel adds one: the multicast delegate variable and its signature graph */
		bool AddDispatcher(UBlueprint* Blueprint, const FName Name)
		{
			if (!FBlueprintEditorUtils::AddMemberVariable(Blueprint, Name, Pin(UEdGraphSchema_K2::PC_MCDelegate)))
			{
				return false;
			}
			const UEdGraphSchema_K2* Schema = GetDefault<UEdGraphSchema_K2>();
			UEdGraph* Signature = FBlueprintEditorUtils::CreateNewGraph(Blueprint, Name, UEdGraph::StaticClass(), UEdGraphSchema_K2::StaticClass());
			if (!Signature)
			{
				return false;
			}
			Signature->bEditable = false;
			Schema->CreateDefaultNodesForGraph(*Signature);
			Schema->CreateFunctionGraphTerminators(*Signature, static_cast<UClass*>(nullptr));
			Schema->AddExtraFunctionFlags(Signature, FUNC_BlueprintCallable | FUNC_BlueprintEvent | FUNC_Public);
			Schema->MarkFunctionEntryAsEditable(Signature, true);
			Blueprint->DelegateSignatureGraphs.Add(Signature);
			FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);
			return true;
		}

		UK2Node_FunctionResult* ResultOf(UEdGraph* Graph)
		{
			for (UEdGraphNode* Node : Graph ? Graph->Nodes : TArray<TObjectPtr<UEdGraphNode>>())
			{
				if (UK2Node_FunctionResult* Result = Cast<UK2Node_FunctionResult>(Node))
				{
					return Result;
				}
			}
			return nullptr;
		}

		FString PinTypeName(const FEdGraphPinType& Type)
		{
			return StaticEnum<EN2CPinType>()->GetNameStringByValue(static_cast<int64>(
				N2CStableNames::PinType(Type.PinCategory, Type.PinSubCategory, Type.PinSubCategoryObject.Get())));
		}

		TSharedRef<FJsonObject> TypedName(const FString& Name, const FEdGraphPinType& Type)
		{
			const TSharedRef<FJsonObject> Object = MakeShared<FJsonObject>();
			Object->SetStringField(TEXT("name"), Name);
			Object->SetStringField(TEXT("type"), PinTypeName(Type));
			const FString SubType = N2CStableNames::PinSubType(Type.PinSubCategory, Type.PinSubCategoryObject.Get());
			if (!SubType.IsEmpty())
			{
				Object->SetStringField(TEXT("sub_type"), SubType);
			}
			if (Type.ContainerType != EPinContainerType::None)
			{
				Object->SetStringField(TEXT("container"), Type.IsArray() ? TEXT("array") : Type.IsSet() ? TEXT("set") : TEXT("map"));
			}
			return Object;
		}

		TArray<TSharedPtr<FJsonValue>> Strings(TArray<FString> Values)
		{
			Values.Sort();
			TArray<TSharedPtr<FJsonValue>> Out;
			for (const FString& Value : Values)
			{
				Out.Add(MakeShared<FJsonValueString>(Value));
			}
			return Out;
		}

		TArray<TSharedPtr<FJsonValue>> Objects(TArray<TSharedRef<FJsonObject>> Values)
		{
			Values.Sort([](const TSharedRef<FJsonObject>& A, const TSharedRef<FJsonObject>& B)
			{
				return A->GetStringField(TEXT("name")) < B->GetStringField(TEXT("name"));
			});
			TArray<TSharedPtr<FJsonValue>> Out;
			for (const TSharedRef<FJsonObject>& Value : Values)
			{
				Out.Add(MakeShared<FJsonValueObject>(Value));
			}
			return Out;
		}

		/**
		 * A compiled function's parameters, inputs or outputs (out and return parameters), from the UFunction: an
		 * interface function's graph declares none of its own
		 */
		TArray<TSharedRef<FJsonObject>> Parameters(const UFunction* Function, bool bOutputs)
		{
			TArray<TSharedRef<FJsonObject>> Out;
			const UEdGraphSchema_K2* Schema = GetDefault<UEdGraphSchema_K2>();
			for (TFieldIterator<FProperty> It(Function); Function && It && It->HasAnyPropertyFlags(CPF_Parm); ++It)
			{
				const bool bOutput = It->HasAnyPropertyFlags(CPF_ReturnParm) || (It->HasAnyPropertyFlags(CPF_OutParm) && !It->HasAnyPropertyFlags(CPF_ReferenceParm));
				FEdGraphPinType Type;
				if (bOutput == bOutputs && Schema->ConvertPropertyToPinType(*It, Type))
				{
					Out.Add(TypedName(It->GetName(), Type));
				}
			}
			return Out;
		}

		/** What a graph calls: "<owner>.<function>" for each call, "macro:<name>" for each macro instance */
		TArray<FString> CallsOf(const UEdGraph* Graph)
		{
			TSet<FString> Calls;
			for (const UEdGraphNode* Node : Graph->Nodes)
			{
				// Disabled and automatically placed (ghost) nodes never run: a child's unwired Parent: calls, for one
				if (!Node || !Node->IsNodeEnabled() || Node->IsAutomaticallyPlacedGhostNode())
				{
					continue;
				}
				if (const UK2Node_CallFunction* Call = Cast<UK2Node_CallFunction>(Node))
				{
					const UFunction* Function = Call->GetTargetFunction();
					const FString Owner = Function ? N2CStableNames::ClassName(Function->GetOwnerClass())
						: N2CStableNames::ClassName(Call->FunctionReference.GetMemberParentClass());
					Calls.Add(FString::Printf(TEXT("%s.%s"), *Owner, *Call->FunctionReference.GetMemberName().ToString()));
				}
				else if (const UK2Node_MacroInstance* Macro = Cast<UK2Node_MacroInstance>(Node))
				{
					if (const UEdGraph* MacroGraph = Macro->GetMacroGraph())
					{
						Calls.Add(TEXT("macro:") + MacroGraph->GetName());
					}
				}
			}
			return Calls.Array();
		}

		FString CppName(const UClass* Class)
		{
			if (!Class)
			{
				return FString();
			}
			if (Cast<UBlueprintGeneratedClass>(Class))
			{
				return FString(Class->IsChildOf(AActor::StaticClass()) ? TEXT("A") : TEXT("U")) + N2CStableNames::ClassName(Class);
			}
			return FString(Class->GetPrefixCPP()) + Class->GetName();
		}
	}

	TConstArrayView<FFixture> All()
	{
		return GFixtures;
	}

	FString ObjectPath(const TCHAR* AssetName)
	{
		return FString::Printf(TEXT("%s/%s.%s"), FolderPath, AssetName, AssetName);
	}

	bool ShouldGenerate()
	{
		return FPlatformMisc::GetEnvironmentVariable(TEXT("N2C_GENERATE_BENCHMARK_CORPUS")) == TEXT("1")
			|| FParse::Param(FCommandLine::Get(), TEXT("N2CGenerateBenchmarkCorpus"));
	}

	FString RepoRoot()
	{
		const FString Override = FPlatformMisc::GetEnvironmentVariable(TEXT("N2C_REPO_ROOT"));
		if (!Override.IsEmpty())
		{
			return FPaths::ConvertRelativePathToFull(Override);
		}
		const TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(TEXT("NodeToCode"));
		if (!Plugin.IsValid())
		{
			return FString();
		}
		FString Source = FPaths::ConvertRelativePathToFull(Plugin->GetBaseDir() / TEXT("Source"));
#if PLATFORM_WINDOWS
		// build.ps1 junctions the host's plugin Source to the checkout's: follow it to the checkout
		const HANDLE Handle = ::CreateFileW(*Source, 0, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
			OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, nullptr);
		if (Handle != INVALID_HANDLE_VALUE)
		{
			WCHAR Buffer[4096];
			const DWORD Length = ::GetFinalPathNameByHandleW(Handle, Buffer, UE_ARRAY_COUNT(Buffer), FILE_NAME_NORMALIZED);
			::CloseHandle(Handle);
			if (Length > 0 && Length < UE_ARRAY_COUNT(Buffer))
			{
				Source = FString(Buffer);
				Source.RemoveFromStart(TEXT("\\\\?\\"));
				FPaths::NormalizeDirectoryName(Source);
			}
		}
#endif
		const FString Root = FPaths::GetPath(Source);
		return IFileManager::Get().DirectoryExists(*(Root / TEXT("Benchmarks"))) || IFileManager::Get().FileExists(*(Root / TEXT("build.ps1")))
			? Root : FString();
	}

	FString CorpusDir()
	{
		const FString Root = RepoRoot();
		return Root.IsEmpty() ? FString() : Root / TEXT("Benchmarks/corpus");
	}

	FString StableJson(const TSharedRef<FJsonObject>& Object)
	{
		// FJsonObject keeps insertion order: rebuild with sorted keys, recursively
		TFunction<TSharedPtr<FJsonValue>(const TSharedPtr<FJsonValue>&)> Sorted = [&Sorted](const TSharedPtr<FJsonValue>& Value) -> TSharedPtr<FJsonValue>
		{
			if (!Value.IsValid())
			{
				return Value;
			}
			if (Value->Type == EJson::Object)
			{
				const TSharedPtr<FJsonObject> In = Value->AsObject();
				// Iterated, not looked up by key: the key type of FJsonObject::Values differs from 5.8 on
				TArray<TPair<FString, TSharedPtr<FJsonValue>>> Fields;
				for (const auto& Pair : In->Values)
				{
					Fields.Emplace(FString(Pair.Key), Pair.Value);
				}
				Fields.Sort([](const TPair<FString, TSharedPtr<FJsonValue>>& A, const TPair<FString, TSharedPtr<FJsonValue>>& B) { return A.Key < B.Key; });
				const TSharedRef<FJsonObject> Out = MakeShared<FJsonObject>();
				for (const TPair<FString, TSharedPtr<FJsonValue>>& Field : Fields)
				{
					Out->SetField(Field.Key, Sorted(Field.Value));
				}
				return MakeShared<FJsonValueObject>(Out);
			}
			if (Value->Type == EJson::Array)
			{
				TArray<TSharedPtr<FJsonValue>> Out;
				for (const TSharedPtr<FJsonValue>& Item : Value->AsArray())
				{
					Out.Add(Sorted(Item));
				}
				return MakeShared<FJsonValueArray>(Out);
			}
			return Value;
		};
		const TSharedPtr<FJsonObject> Root = Sorted(MakeShared<FJsonValueObject>(Object))->AsObject();
		FString Text;
		const TSharedRef<TJsonWriter<TCHAR, TPrettyJsonPrintPolicy<TCHAR>>> Writer = TJsonWriterFactory<TCHAR, TPrettyJsonPrintPolicy<TCHAR>>::Create(&Text);
		FJsonSerializer::Serialize(Root.ToSharedRef(), Writer);
		Text.ReplaceInline(TEXT("\r\n"), TEXT("\n"));
		Text.ReplaceInline(TEXT("\t"), TEXT("  "));
		return Text + TEXT("\n");
	}

	TArray<FString> GraphNames(UBlueprint& Blueprint)
	{
		TArray<UEdGraph*> Graphs;
		Blueprint.GetAllGraphs(Graphs);
		TArray<FString> Names;
		for (const UEdGraph* Graph : Graphs)
		{
			// Delegate signatures are declarations, not graphs a translation reads
			if (Graph && !Blueprint.DelegateSignatureGraphs.Contains(Graph))
			{
				Names.AddUnique(Graph->GetName());
			}
		}
		Names.Sort();
		return Names;
	}

	TSharedRef<FJsonObject> ComputeShape(UBlueprint& Blueprint)
	{
		const TSharedRef<FJsonObject> Shape = MakeShared<FJsonObject>();
		Shape->SetStringField(TEXT("name"), Blueprint.GetName());
		Shape->SetStringField(TEXT("asset_class"), Blueprint.GetClass()->GetName());
		Shape->SetStringField(TEXT("parent"), N2CStableNames::ClassName(Blueprint.ParentClass));
		Shape->SetStringField(TEXT("cpp_class"), CppName(Blueprint.GeneratedClass));
		Shape->SetStringField(TEXT("cpp_parent"), CppName(Blueprint.ParentClass));

		TArray<FString> Interfaces;
		for (const FBPInterfaceDescription& Interface : Blueprint.ImplementedInterfaces)
		{
			Interfaces.Add(N2CStableNames::ClassName(Interface.Interface.Get()));
		}
		Shape->SetArrayField(TEXT("interfaces"), Strings(Interfaces));

		TArray<TSharedRef<FJsonObject>> Components;
		if (const USimpleConstructionScript* SCS = Blueprint.SimpleConstructionScript)
		{
			for (USCS_Node* Node : SCS->GetAllNodes())
			{
				if (!Node || Node == SCS->GetDefaultSceneRootNode())
				{
					continue;
				}
				const TSharedRef<FJsonObject> Component = MakeShared<FJsonObject>();
				Component->SetStringField(TEXT("name"), Node->GetVariableName().ToString());
				Component->SetStringField(TEXT("class"), N2CStableNames::ClassName(Node->ComponentClass));
				const USCS_Node* Parent = SCS->FindParentNode(Node);
				if (Parent || !Node->ParentComponentOrVariableName.IsNone())
				{
					Component->SetStringField(TEXT("parent"), Parent ? Parent->GetVariableName().ToString() : Node->ParentComponentOrVariableName.ToString());
				}
				Components.Add(Component);
			}
		}
		Shape->SetArrayField(TEXT("components"), Objects(Components));

		TArray<TSharedRef<FJsonObject>> Properties;
		TArray<FString> Dispatchers;
		for (const FBPVariableDescription& Variable : Blueprint.NewVariables)
		{
			if (Variable.VarType.PinCategory == UEdGraphSchema_K2::PC_MCDelegate)
			{
				Dispatchers.Add(Variable.VarName.ToString());
				continue;
			}
			Properties.Add(TypedName(Variable.VarName.ToString(), Variable.VarType));
		}
		Shape->SetArrayField(TEXT("properties"), Objects(Properties));
		Shape->SetArrayField(TEXT("dispatchers"), Strings(Dispatchers));

		const UClass* Generated = Blueprint.GeneratedClass;
		const auto DescribeFunction = [Generated](UEdGraph* Graph, const FString& Interface)
		{
			const TSharedRef<FJsonObject> Function = MakeShared<FJsonObject>();
			Function->SetStringField(TEXT("name"), Graph->GetName());
			UK2Node_FunctionEntry* Entry = nullptr;
			for (UEdGraphNode* Node : Graph->Nodes)
			{
				Entry = Entry ? Entry : Cast<UK2Node_FunctionEntry>(Node);
			}
			Function->SetBoolField(TEXT("pure"), Entry && (Entry->GetFunctionFlags() & FUNC_BlueprintPure) != 0);
			const UFunction* Compiled = Generated ? Generated->FindFunctionByName(Graph->GetFName()) : nullptr;
			Function->SetArrayField(TEXT("inputs"), Objects(Parameters(Compiled, false)));
			Function->SetArrayField(TEXT("outputs"), Objects(Parameters(Compiled, true)));
			if (!Interface.IsEmpty())
			{
				Function->SetStringField(TEXT("interface"), Interface);
			}
			return Function;
		};
		TArray<TSharedRef<FJsonObject>> Functions;
		for (UEdGraph* Graph : Blueprint.FunctionGraphs)
		{
			if (Graph && Graph->GetFName() != UEdGraphSchema_K2::FN_UserConstructionScript)
			{
				Functions.Add(DescribeFunction(Graph, FString()));
			}
		}
		for (const FBPInterfaceDescription& Interface : Blueprint.ImplementedInterfaces)
		{
			for (UEdGraph* Graph : Interface.Graphs)
			{
				if (Graph)
				{
					Functions.Add(DescribeFunction(Graph, N2CStableNames::ClassName(Interface.Interface.Get())));
				}
			}
		}
		Shape->SetArrayField(TEXT("functions"), Objects(Functions));

		TArray<FString> Macros;
		for (const UEdGraph* Graph : Blueprint.MacroGraphs)
		{
			if (Graph)
			{
				Macros.Add(Graph->GetName());
			}
		}
		Shape->SetArrayField(TEXT("macros"), Strings(Macros));

		TArray<FString> Timelines;
		for (const UTimelineTemplate* Timeline : Blueprint.Timelines)
		{
			if (Timeline)
			{
				Timelines.Add(Timeline->GetVariableName().ToString());
			}
		}
		Shape->SetArrayField(TEXT("timelines"), Strings(Timelines));

		// Events in the event graphs: engine events the Blueprint overrides (ghost nodes left out), custom events, input events
		TArray<TSharedRef<FJsonObject>> Events;
		for (const UEdGraph* Graph : Blueprint.UbergraphPages)
		{
			for (const UEdGraphNode* Node : Graph ? Graph->Nodes : TArray<TObjectPtr<UEdGraphNode>>())
			{
				const TSharedRef<FJsonObject> Event = MakeShared<FJsonObject>();
				if (const UK2Node_CustomEvent* Custom = Cast<UK2Node_CustomEvent>(Node))
				{
					Event->SetStringField(TEXT("name"), Custom->CustomFunctionName.ToString());
					Event->SetStringField(TEXT("kind"), TEXT("custom"));
				}
				else if (const UK2Node_Event* Engine = Cast<UK2Node_Event>(Node))
				{
					if (Engine->IsAutomaticallyPlacedGhostNode())
					{
						continue;
					}
					Event->SetStringField(TEXT("name"), Engine->EventReference.GetMemberName().ToString());
					Event->SetStringField(TEXT("kind"), Engine->bOverrideFunction ? TEXT("override") : TEXT("event"));
				}
				else if (Node && Node->GetClass()->GetName() == TEXT("K2Node_EnhancedInputAction"))
				{
					const FObjectPropertyBase* Action = FindFProperty<FObjectPropertyBase>(Node->GetClass(), TEXT("InputAction"));
					const UObject* Asset = Action ? Action->GetObjectPropertyValue_InContainer(Node) : nullptr;
					Event->SetStringField(TEXT("name"), Asset ? Asset->GetName() : FString());
					Event->SetStringField(TEXT("kind"), TEXT("input_action"));
				}
				else
				{
					continue;
				}
				Events.Add(Event);
			}
		}
		Shape->SetArrayField(TEXT("events"), Objects(Events));

		// What each graph calls (collapsed graphs included, by their own name)
		TArray<UEdGraph*> Graphs;
		Blueprint.GetAllGraphs(Graphs);
		const TSharedRef<FJsonObject> Calls = MakeShared<FJsonObject>();
		for (const UEdGraph* Graph : Graphs)
		{
			if (Graph && !Blueprint.DelegateSignatureGraphs.Contains(Graph))
			{
				const TArray<FString> GraphCalls = CallsOf(Graph);
				if (GraphCalls.Num() > 0)
				{
					Calls->SetArrayField(Graph->GetName(), Strings(GraphCalls));
				}
			}
		}
		Shape->SetObjectField(TEXT("calls"), Calls);
		return Shape;
	}

	TSharedRef<FJsonObject> BuildManifest(TArray<FString>& OutErrors)
	{
		const TSharedRef<FJsonObject> Manifest = MakeShared<FJsonObject>();
		Manifest->SetNumberField(TEXT("version"), 1);
		Manifest->SetStringField(TEXT("description"), TEXT("The NodeToCode benchmark corpus (card 0364): every graph the token, format and ")
			TEXT("compile-gated benchmarks run on. Written by N2CTest::BenchmarkCorpus::GenerateAll; checked by NodeToCode.Benchmark.Corpus ")
			TEXT("and Tests/python/test_benchmark_corpus.py against manifest.schema.json."));
		const TArray<TSharedPtr<FJsonValue>> Engines = { MakeShared<FJsonValueString>(TEXT("5.6")), MakeShared<FJsonValueString>(TEXT("5.7")),
			MakeShared<FJsonValueString>(TEXT("5.8")) };
		TArray<TSharedPtr<FJsonValue>> Entries;
		const auto AddEntry = [&](const FString& Id, const FString& Source, const FString& AssetPath, const TCHAR* Covers, bool bShape)
		{
			UBlueprint* Blueprint = LoadObject<UBlueprint>(nullptr, *AssetPath);
			if (!Blueprint)
			{
				OutErrors.Add(TEXT("manifest: cannot load ") + AssetPath);
				return;
			}
			const TSharedRef<FJsonObject> Entry = MakeShared<FJsonObject>();
			Entry->SetStringField(TEXT("id"), Id);
			Entry->SetStringField(TEXT("source"), Source);
			Entry->SetStringField(TEXT("asset_path"), AssetPath);
			Entry->SetStringField(TEXT("covers"), Covers);
			Entry->SetArrayField(TEXT("graphs"), Strings(GraphNames(*Blueprint)));
			Entry->SetArrayField(TEXT("engines"), Engines);
			if (bShape)
			{
				Entry->SetStringField(TEXT("shape"), FString::Printf(TEXT("shapes/%s.json"), *Id));
			}
			else
			{
				Entry->SetField(TEXT("shape"), MakeShared<FJsonValueNull>());
			}
			Entries.Add(MakeShared<FJsonValueObject>(Entry));
		};
		for (const FFixture& Fixture : All())
		{
			AddEntry(Fixture.Name, TEXT("corpus"), ObjectPath(Fixture.Name), Fixture.Covers, true);
		}
		for (const PerfFixtures::FFixture& Fixture : PerfFixtures::All())
		{
			AddEntry(Fixture.Name, TEXT("perf"), PerfFixtures::ObjectPath(Fixture.Name), TEXT("card 0359's generated performance fixture"), false);
		}
		Manifest->SetArrayField(TEXT("entries"), Entries);

		// Real-project samples named by the review (rec-agentic-landscape-12): under the Unreal Engine EULA, never committed
		TArray<TSharedPtr<FJsonValue>> External;
		for (const TCHAR* Sample : { TEXT("Lyra"), TEXT("Cropout") })
		{
			const TSharedRef<FJsonObject> Entry = MakeShared<FJsonObject>();
			Entry->SetStringField(TEXT("name"), Sample);
			Entry->SetStringField(TEXT("status"), TEXT("not_installed"));
			Entry->SetStringField(TEXT("note"), TEXT("Epic sample project under the Unreal Engine EULA: install it from the Epic launcher, list its ")
				TEXT("Blueprints here by asset path and capture them locally; never commit its assets."));
			Entry->SetArrayField(TEXT("entries"), {});
			External.Add(MakeShared<FJsonValueObject>(Entry));
		}
		Manifest->SetArrayField(TEXT("external"), External);
		return Manifest;
	}

	bool GenerateAll(TArray<FString>& OutErrors, TArray<FString>& OutNotes)
	{
#if ENGINE_MAJOR_VERSION != 5 || ENGINE_MINOR_VERSION != 6
		OutErrors.Add(TEXT("regenerate the benchmark corpus on UE 5.6 only: a package saved by a later engine does not load in 5.6"));
		return false;
#else
		const TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(TEXT("NodeToCodeTestContent"));
		if (!Plugin.IsValid() || !Plugin->IsEnabled() || !FPackageName::MountPointExists(TEXT("/NodeToCodeTestContent/")))
		{
			OutErrors.Add(TEXT("the NodeToCodeTestContent plugin is not mounted: build.ps1 junctions Tests/NodeToCodeTestContent into the host's Plugins folder"));
			return false;
		}
		const FString Corpus = CorpusDir();
		if (Corpus.IsEmpty())
		{
			OutErrors.Add(TEXT("the checkout root was not found (set N2C_REPO_ROOT)"));
			return false;
		}

		// Packages: the shared types, then one per Blueprint
		UPackage* EnumPackage = NewAssetPackage(EnumName, OutErrors);
		UPackage* StructPackage = NewAssetPackage(StructName, OutErrors);
		UPackage* InterfacePackage = NewAssetPackage(InterfaceName, OutErrors);
		TMap<FString, UPackage*> Packages;
		for (const FFixture& Fixture : All())
		{
			Packages.Add(Fixture.Name, NewAssetPackage(Fixture.Name, OutErrors));
		}
		if (OutErrors.Num() > 0)
		{
			return false;
		}

		// Actor Blueprints made through FTestBlueprint (its helpers also build the other kinds' graphs)
		TMap<FString, TUniquePtr<FTestBlueprint>> Actors;
		for (const FFixture& Fixture : All())
		{
			const FString Name = Fixture.Name;
			if (Name != TEXT("BP_N2CBench_HealthComponent") && Name != TEXT("WBP_N2CBench_Widget") && Name != TEXT("BP_N2CBench_PickupChild"))
			{
				Actors.Add(Name, FTestBlueprint::InPackage(Name, Packages[Name]));
			}
		}
		FTestBlueprint& Helper = *Actors[TEXT("BP_N2CBench_Pickup")];
		const auto Failed = [&]()
		{
			for (const TPair<FString, TUniquePtr<FTestBlueprint>>& Pair : Actors)
			{
				OutErrors.Append(Pair.Value->Errors());
			}
			return OutErrors.Num() > 0;
		};
		const auto Bp = [&](const TCHAR* Name) -> FTestBlueprint& { return *Actors[Name]; };

		// Shared types
		UUserDefinedEnum* Enum = Helper.AddUserEnum(EnumName, { TEXT("Idle"), TEXT("Moving"), TEXT("Dead") }, EnumPackage);
		UUserDefinedStruct* Struct = Enum ? Helper.AddUserStruct(StructName, {
			{ TEXT("Count"), FTestBlueprint::IntPinType() }, { TEXT("Alive"), FTestBlueprint::BoolPinType() },
			{ TEXT("State"), FTestBlueprint::EnumPinType(Enum) } }, StructPackage) : nullptr;
		UBlueprint* Interface = CreateBlueprint(UInterface::StaticClass(), InterfacePackage, InterfaceName, BPTYPE_Interface, OutErrors);
		if (Interface)
		{
			UEdGraph* Interact = FBlueprintEditorUtils::CreateNewGraph(Interface, TEXT("Interact"), UEdGraph::StaticClass(), UEdGraphSchema_K2::StaticClass());
			FBlueprintEditorUtils::AddFunctionGraph<UClass>(Interface, Interact, /*bIsUserCreated*/ true, nullptr);
			Helper.AddFunctionInput(Interact, TEXT("Instigator"), FTestBlueprint::ObjectPinType(AActor::StaticClass()));
			Helper.AddFunctionOutput(Interact, TEXT("Handled"), FTestBlueprint::BoolPinType());
		}
		if (!Struct || !Interface || Failed() || !Compile(Interface, OutErrors))
		{
			Failed();
			return false;
		}

		// Pickup: components, BeginPlay and Tick
		{
			FTestBlueprint& B = Bp(TEXT("BP_N2CBench_Pickup"));
			UBlueprint* Blueprint = B.GetBlueprint();
			USCS_Node* Root = AddComponent(Blueprint, USceneComponent::StaticClass(), TEXT("Root"));
			USCS_Node* Mesh = AddComponent(Blueprint, UStaticMeshComponent::StaticClass(), TEXT("Mesh"), Root);
			AddComponent(Blueprint, USphereComponent::StaticClass(), TEXT("Trigger"), Root);
			if (Mesh)
			{
				Cast<UStaticMeshComponent>(Mesh->ComponentTemplate)->SetStaticMesh(
					LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube")));
			}
			B.AddMemberVariable(TEXT("SpinSpeed"), Pin(UEdGraphSchema_K2::PC_Real, nullptr, UEdGraphSchema_K2::PC_Float));
			UEdGraph* Graph = B.EventGraph();
			Then(B, B.FindEvent(Graph, TEXT("ReceiveBeginPlay")), Print(B, Graph, TEXT("Pickup ready")));
			UK2Node_Event* Tick = B.FindEvent(Graph, TEXT("ReceiveTick"));
			UK2Node_CallFunction* Rotate = B.AddCall(Graph, AActor::StaticClass(), TEXT("K2_AddActorLocalRotation"));
			Then(B, Tick, Rotate);
		}
		// Math: Add and Scale, Scale calls Add
		{
			FTestBlueprint& B = Bp(TEXT("BP_N2CBench_Math"));
			UEdGraph* AddGraph = B.AddFunction(TEXT("AddNumbers"));
			B.AddFunctionInput(AddGraph, TEXT("A"), FTestBlueprint::IntPinType());
			B.AddFunctionInput(AddGraph, TEXT("B"), FTestBlueprint::IntPinType());
			UK2Node_FunctionResult* AddResult = B.AddFunctionOutput(AddGraph, TEXT("Sum"), FTestBlueprint::IntPinType());
			UK2Node_CallFunction* Plus = Math(B, AddGraph, GET_FUNCTION_NAME_CHECKED(UKismetMathLibrary, Add_IntInt));
			Then(B, B.FunctionEntry(AddGraph), AddResult);
			B.Link(B.FunctionEntry(AddGraph), TEXT("A"), Plus, TEXT("A"));
			B.Link(B.FunctionEntry(AddGraph), TEXT("B"), Plus, TEXT("B"));
			B.Link(Plus, UEdGraphSchema_K2::PN_ReturnValue, AddResult, TEXT("Sum"));
			UEdGraph* Scale = B.AddFunction(TEXT("ScaleSum"));
			B.AddFunctionInput(Scale, TEXT("Value"), FTestBlueprint::IntPinType());
			UK2Node_FunctionResult* ScaleResult = B.AddFunctionOutput(Scale, TEXT("Scaled"), FTestBlueprint::IntPinType());
			UK2Node_CallFunction* CallAdd = B.AddSelfCall(Scale, TEXT("AddNumbers"));
			UK2Node_CallFunction* Times = Math(B, Scale, GET_FUNCTION_NAME_CHECKED(UKismetMathLibrary, Multiply_IntInt));
			Then(B, B.FunctionEntry(Scale), CallAdd);
			Then(B, CallAdd, ScaleResult);
			B.Link(B.FunctionEntry(Scale), TEXT("Value"), CallAdd, TEXT("A"));
			B.Link(B.FunctionEntry(Scale), TEXT("Value"), CallAdd, TEXT("B"));
			B.Link(CallAdd, TEXT("Sum"), Times, TEXT("A"));
			B.Link(Times, UEdGraphSchema_K2::PN_ReturnValue, ScaleResult, TEXT("Scaled"));
		}
		// PureFunction: Doubled (pure) read by Report
		{
			FTestBlueprint& B = Bp(TEXT("BP_N2CBench_PureFunction"));
			UEdGraph* Doubled = B.AddFunction(TEXT("Doubled"));
			B.AddFunctionInput(Doubled, TEXT("Value"), FTestBlueprint::IntPinType());
			UK2Node_FunctionResult* Result = B.AddFunctionOutput(Doubled, TEXT("Result"), FTestBlueprint::IntPinType());
			if (UK2Node_FunctionEntry* Entry = B.FunctionEntry(Doubled))
			{
				Entry->AddExtraFlags(FUNC_BlueprintPure);
			}
			UK2Node_CallFunction* Times = Math(B, Doubled, GET_FUNCTION_NAME_CHECKED(UKismetMathLibrary, Multiply_IntInt));
			Then(B, B.FunctionEntry(Doubled), Result);
			B.Link(B.FunctionEntry(Doubled), TEXT("Value"), Times, TEXT("A"));
			if (UEdGraphPin* Two = Times ? Times->FindPin(TEXT("B")) : nullptr)
			{
				Two->DefaultValue = TEXT("2");
			}
			B.Link(Times, UEdGraphSchema_K2::PN_ReturnValue, Result, TEXT("Result"));
			UEdGraph* Report = B.AddFunction(TEXT("Report"));
			B.AddFunctionInput(Report, TEXT("Count"), FTestBlueprint::IntPinType());
			UK2Node_CallFunction* CallDoubled = B.AddSelfCall(Report, TEXT("Doubled"));
			UK2Node_CallFunction* ToString = B.AddCall(Report, UKismetStringLibrary::StaticClass(), TEXT("Conv_IntToString"));
			UK2Node_CallFunction* Printed = Print(B, Report, TEXT(""));
			Then(B, B.FunctionEntry(Report), Printed);
			B.Link(B.FunctionEntry(Report), TEXT("Count"), CallDoubled, TEXT("Value"));
			B.Link(CallDoubled, TEXT("Result"), ToString, TEXT("InInt"));
			B.Link(ToString, UEdGraphSchema_K2::PN_ReturnValue, Printed, TEXT("InString"));
		}
		// LocalVariables: CountUp with local Counter
		{
			FTestBlueprint& B = Bp(TEXT("BP_N2CBench_LocalVariables"));
			UBlueprint* Blueprint = B.GetBlueprint();
			UEdGraph* Graph = B.AddFunction(TEXT("CountUp"));
			UK2Node_FunctionResult* Result = B.AddFunctionOutput(Graph, TEXT("Total"), FTestBlueprint::IntPinType());
			FBlueprintEditorUtils::AddLocalVariable(Blueprint, Graph, TEXT("Counter"), FTestBlueprint::IntPinType(), TEXT("0"));
			const FBPVariableDescription* Counter = FN2CMcpVariableUtils::FindLocalVariable(B.FunctionEntry(Graph), TEXT("Counter"));
			FString Error;
			UK2Node_Variable* Get = Counter ? FN2CMcpNodeSpawn::SpawnLocalVariableNode(Blueprint, Graph, *Counter, false, FVector2D(100, 150), Error) : nullptr;
			UK2Node_Variable* Set = Counter ? FN2CMcpNodeSpawn::SpawnLocalVariableNode(Blueprint, Graph, *Counter, true, FVector2D(400, 0), Error) : nullptr;
			UK2Node_CallFunction* Plus = Math(B, Graph, GET_FUNCTION_NAME_CHECKED(UKismetMathLibrary, Add_IntInt));
			if (!Get || !Set || !Plus)
			{
				OutErrors.Add(TEXT("BP_N2CBench_LocalVariables: ") + Error);
				return false;
			}
			if (UEdGraphPin* One = Plus->FindPin(TEXT("B")))
			{
				One->DefaultValue = TEXT("1");
			}
			Then(B, B.FunctionEntry(Graph), Set);
			Then(B, Set, Result);
			B.Link(Get, TEXT("Counter"), Plus, TEXT("A"));
			B.Link(Plus, UEdGraphSchema_K2::PN_ReturnValue, Set, TEXT("Counter"));
			B.Link(NthPin(Set, EGPD_Output, false), Result->FindPin(TEXT("Total")));
		}
		// ForEach: over an int array parameter
		{
			FTestBlueprint& B = Bp(TEXT("BP_N2CBench_ForEach"));
			FEdGraphPinType Ints = FTestBlueprint::IntPinType();
			Ints.ContainerType = EPinContainerType::Array;
			UEdGraph* Graph = B.AddFunction(TEXT("PrintAll"));
			B.AddFunctionInput(Graph, TEXT("Values"), Ints);
			UK2Node_MacroInstance* Loop = B.AddStandardMacro(Graph, TEXT("ForEachLoop"));
			Then(B, B.FunctionEntry(Graph), Loop);
			B.Link(B.FunctionEntry(Graph), TEXT("Values"), Loop, TEXT("Array"));
			B.Link(Loop, TEXT("LoopBody"), Print(B, Graph, TEXT("one value")), UEdGraphSchema_K2::PN_Execute);
			B.Link(Loop, TEXT("Completed"), Print(B, Graph, TEXT("done")), UEdGraphSchema_K2::PN_Execute);
		}
		// WhileLoop: while bRunning
		{
			FTestBlueprint& B = Bp(TEXT("BP_N2CBench_WhileLoop"));
			B.AddMemberVariable(TEXT("bRunning"), FTestBlueprint::BoolPinType());
			UEdGraph* Graph = B.AddFunction(TEXT("RunWhile"));
			UK2Node_MacroInstance* Loop = B.AddStandardMacro(Graph, TEXT("WhileLoop"));
			UK2Node_VariableGet* Running = B.AddVariableGet(Graph, TEXT("bRunning"));
			Then(B, B.FunctionEntry(Graph), Loop);
			B.Link(NthPin(Running, EGPD_Output, false), NthPin(Loop, EGPD_Input, false));
			Then(B, Loop, Print(B, Graph, TEXT("running")), 0);
		}
		// BranchSwitch
		{
			FTestBlueprint& B = Bp(TEXT("BP_N2CBench_BranchSwitch"));
			B.AddMemberVariable(TEXT("bEnabled"), FTestBlueprint::BoolPinType());
			B.AddMemberVariable(TEXT("State"), FTestBlueprint::EnumPinType(Enum));
			UEdGraph* Graph = B.AddFunction(TEXT("Decide"));
			UK2Node_IfThenElse* Branch = B.AddBranch(Graph);
			UK2Node_SwitchEnum* Switch = B.AddSwitchOnEnum(Graph, Enum);
			Then(B, B.FunctionEntry(Graph), Branch);
			B.Link(NthPin(B.AddVariableGet(Graph, TEXT("bEnabled")), EGPD_Output, false), Branch->GetConditionPin());
			B.Link(Branch->GetThenPin(), NthPin(Switch, EGPD_Input, true));
			B.Link(NthPin(B.AddVariableGet(Graph, TEXT("State")), EGPD_Output, false), NthPin(Switch, EGPD_Input, false));
			Then(B, Switch, Print(B, Graph, TEXT("idle")), 0);
			Then(B, Switch, Print(B, Graph, TEXT("moving")), 1);
			Then(B, Switch, Print(B, Graph, TEXT("dead")), 2);
			B.Link(Branch->GetElsePin(), NthPin(Print(B, Graph, TEXT("disabled")), EGPD_Input, true));
		}
		// Delay
		{
			FTestBlueprint& B = Bp(TEXT("BP_N2CBench_Delay"));
			UEdGraph* Graph = B.EventGraph();
			UK2Node_CallFunction* Wait = B.AddCall(Graph, UKismetSystemLibrary::StaticClass(), GET_FUNCTION_NAME_CHECKED(UKismetSystemLibrary, Delay));
			if (UEdGraphPin* Duration = Wait ? Wait->FindPin(TEXT("Duration")) : nullptr)
			{
				Duration->DefaultValue = TEXT("2.0");
			}
			Then(B, B.FindEvent(Graph, TEXT("ReceiveBeginPlay")), Wait);
			Then(B, Wait, Print(B, Graph, TEXT("two seconds later")));
		}
		// Timeline
		{
			FTestBlueprint& B = Bp(TEXT("BP_N2CBench_Timeline"));
			UEdGraph* Graph = B.EventGraph();
			UK2Node_Timeline* Fade = B.AddTimeline(Graph, TEXT("Fade"));
			B.Link(NthPin(B.FindEvent(Graph, TEXT("ReceiveBeginPlay")), EGPD_Output, true), Fade ? Fade->GetPlayPin() : nullptr);
			B.Link(Fade ? Fade->GetFinishedPin() : nullptr, NthPin(Print(B, Graph, TEXT("faded")), EGPD_Input, true));
		}
		// Dispatcher
		{
			FTestBlueprint& B = Bp(TEXT("BP_N2CBench_Dispatcher"));
			if (!AddDispatcher(B.GetBlueprint(), TEXT("OnDone")))
			{
				OutErrors.Add(TEXT("BP_N2CBench_Dispatcher: could not add the OnDone dispatcher"));
				return false;
			}
			UEdGraph* Graph = B.EventGraph();
			Then(B, B.AddCustomEvent(Graph, TEXT("Finish")), Print(B, Graph, TEXT("finishing")));
		}
		// Cast
		{
			FTestBlueprint& B = Bp(TEXT("BP_N2CBench_Cast"));
			UEdGraph* Graph = B.AddFunction(TEXT("FindPawn"));
			UK2Node_CallFunction* GetPawn = B.AddCall(Graph, UGameplayStatics::StaticClass(), GET_FUNCTION_NAME_CHECKED(UGameplayStatics, GetPlayerPawn));
			FGraphNodeCreator<UK2Node_DynamicCast> Creator(*Graph);
			UK2Node_DynamicCast* CastNode = Creator.CreateNode();
			CastNode->TargetType = APawn::StaticClass();
			Creator.Finalize();
			Then(B, B.FunctionEntry(Graph), CastNode);
			B.Link(GetPawn, UEdGraphSchema_K2::PN_ReturnValue, CastNode, UEdGraphSchema_K2::PN_ObjectToCast);
			B.Link(CastNode->GetValidCastPin(), NthPin(Print(B, Graph, TEXT("a pawn")), EGPD_Input, true));
		}
		// Struct: MakeStats returns a struct, ReadStats breaks one
		{
			FTestBlueprint& B = Bp(TEXT("BP_N2CBench_Struct"));
			UEdGraph* MakeGraph = B.AddFunction(TEXT("MakeStats"));
			B.AddFunctionInput(MakeGraph, TEXT("Count"), FTestBlueprint::IntPinType());
			UK2Node_FunctionResult* MakeResult = B.AddFunctionOutput(MakeGraph, TEXT("Stats"), FTestBlueprint::StructPinType(Struct));
			UK2Node_MakeStruct* Make = B.AddMakeStruct(MakeGraph, Struct);
			Then(B, B.FunctionEntry(MakeGraph), MakeResult);
			B.Link(B.FunctionEntry(MakeGraph)->FindPin(TEXT("Count")), NthPin(Make, EGPD_Input, false));
			B.Link(NthPin(Make, EGPD_Output, false), MakeResult->FindPin(TEXT("Stats")));
			UEdGraph* ReadGraph = B.AddFunction(TEXT("ReadStats"));
			B.AddFunctionInput(ReadGraph, TEXT("Stats"), FTestBlueprint::StructPinType(Struct));
			UK2Node_FunctionResult* ReadResult = B.AddFunctionOutput(ReadGraph, TEXT("Alive"), FTestBlueprint::BoolPinType());
			UK2Node_BreakStruct* Break = B.AddBreakStruct(ReadGraph, Struct);
			Then(B, B.FunctionEntry(ReadGraph), ReadResult);
			B.Link(B.FunctionEntry(ReadGraph)->FindPin(TEXT("Stats")), NthPin(Break, EGPD_Input, false));
			B.Link(NthPin(Break, EGPD_Output, false, 1), ReadResult->FindPin(TEXT("Alive")));
		}
		// Macro of its own
		{
			FTestBlueprint& B = Bp(TEXT("BP_N2CBench_Macro"));
			UEdGraph* Macro = B.AddMacro(TEXT("PrintTwice"));
			UK2Node_Tunnel* In = B.TunnelEntry(Macro);
			UK2Node_Tunnel* Out = B.TunnelExit(Macro);
			B.AddTunnelPin(In, TEXT("Exec"), FTestBlueprint::ExecPinType());
			B.AddTunnelPin(Out, TEXT("Then"), FTestBlueprint::ExecPinType());
			UK2Node_CallFunction* First = Print(B, Macro, TEXT("once"));
			UK2Node_CallFunction* Second = Print(B, Macro, TEXT("twice"));
			Then(B, In, First);
			Then(B, First, Second);
			Then(B, Second, Out);
			UEdGraph* Graph = B.EventGraph();
			Then(B, B.AddCustomEvent(Graph, TEXT("Shout")), B.AddMacroInstance(Graph, Macro));
		}
		// Collapsed graph
		{
			FTestBlueprint& B = Bp(TEXT("BP_N2CBench_Collapsed"));
			UEdGraph* Graph = B.EventGraph();
			UK2Node_Composite* Collapsed = B.AddCollapsedGraph(Graph, TEXT("Greeting"));
			if (Collapsed)
			{
				UEdGraph* Inner = Collapsed->BoundGraph;
				UK2Node_Tunnel* In = B.TunnelEntry(Inner);
				UK2Node_Tunnel* Out = B.TunnelExit(Inner);
				B.AddTunnelPin(In, TEXT("Exec"), FTestBlueprint::ExecPinType());
				B.AddTunnelPin(Out, TEXT("Then"), FTestBlueprint::ExecPinType());
				UK2Node_CallFunction* Hello = Print(B, Inner, TEXT("hello"));
				Then(B, In, Hello);
				Then(B, Hello, Out);
				Then(B, B.AddCustomEvent(Graph, TEXT("Greet")), Collapsed);
			}
		}
		// Enhanced Input
		{
			FTestBlueprint& B = Bp(TEXT("BP_N2CBench_EnhancedInput"));
			UEdGraph* Graph = B.EventGraph();
			Then(B, B.AddEnhancedInputAction(Graph, TEXT("IA_N2CBench_Jump")), Print(B, Graph, TEXT("jump")));
		}
		// Format Text
		{
			FTestBlueprint& B = Bp(TEXT("BP_N2CBench_FormatText"));
			B.AddMemberVariable(TEXT("PlayerName"), FTestBlueprint::TextPinType());
			B.AddMemberVariable(TEXT("Coins"), FTestBlueprint::IntPinType());
			UEdGraph* Graph = B.AddFunction(TEXT("Describe"));
			UK2Node_FunctionResult* Result = B.AddFunctionOutput(Graph, TEXT("Line"), FTestBlueprint::TextPinType());
			UK2Node_FormatText* Format = B.AddFormatText(Graph, TEXT("{Name} has {Count} coins"));
			Then(B, B.FunctionEntry(Graph), Result);
			B.Link(NthPin(B.AddVariableGet(Graph, TEXT("PlayerName")), EGPD_Output, false), Format ? Format->FindPin(TEXT("Name")) : nullptr);
			B.Link(NthPin(B.AddVariableGet(Graph, TEXT("Coins")), EGPD_Output, false), Format ? Format->FindPin(TEXT("Count")) : nullptr);
			B.Link(NthPin(Format, EGPD_Output, false), Result->FindPin(TEXT("Line")));
		}
		// Sequence and DoOnce
		{
			FTestBlueprint& B = Bp(TEXT("BP_N2CBench_Sequence"));
			UEdGraph* Graph = B.EventGraph();
			UK2Node_ExecutionSequence* Sequence = B.AddSequence(Graph);
			UK2Node_MacroInstance* Once = B.AddStandardMacro(Graph, TEXT("DoOnce"));
			Then(B, B.AddCustomEvent(Graph, TEXT("Start")), Sequence);
			Then(B, Sequence, Print(B, Graph, TEXT("first")), 0);
			Then(B, Sequence, Once, 1);
			Then(B, Once, Print(B, Graph, TEXT("only once")), 0);
		}
		// Variables of many types
		{
			FTestBlueprint& B = Bp(TEXT("BP_N2CBench_Variables"));
			FEdGraphPinType Names = Pin(UEdGraphSchema_K2::PC_Name);
			Names.ContainerType = EPinContainerType::Array;
			FEdGraphPinType Scores = Pin(UEdGraphSchema_K2::PC_Name);
			Scores.ContainerType = EPinContainerType::Map;
			Scores.PinValueType = FEdGraphTerminalType::FromPinType(FTestBlueprint::IntPinType());
			B.AddMemberVariable(TEXT("Speed"), Pin(UEdGraphSchema_K2::PC_Real, nullptr, UEdGraphSchema_K2::PC_Float));
			B.AddMemberVariable(TEXT("Lives"), FTestBlueprint::IntPinType());
			B.AddMemberVariable(TEXT("Title"), Pin(UEdGraphSchema_K2::PC_String));
			B.AddMemberVariable(TEXT("Target"), FTestBlueprint::ObjectPinType(AActor::StaticClass()));
			B.AddMemberVariable(TEXT("Labels"), Names);
			B.AddMemberVariable(TEXT("Scores"), Scores);
			B.AddMemberVariable(TEXT("Stats"), FTestBlueprint::StructPinType(Struct));
			B.AddMemberVariable(TEXT("Spot"), FTestBlueprint::StructPinType(TBaseStructure<FVector>::Get()));
			UEdGraph* Graph = B.AddFunction(TEXT("IsAlive"));
			UK2Node_FunctionResult* Result = B.AddFunctionOutput(Graph, TEXT("Alive"), FTestBlueprint::BoolPinType());
			UK2Node_CallFunction* Greater = Math(B, Graph, GET_FUNCTION_NAME_CHECKED(UKismetMathLibrary, Greater_IntInt));
			Then(B, B.FunctionEntry(Graph), Result);
			B.Link(NthPin(B.AddVariableGet(Graph, TEXT("Lives")), EGPD_Output, false), Greater ? Greater->FindPin(TEXT("A")) : nullptr);
			B.Link(Greater, UEdGraphSchema_K2::PN_ReturnValue, Result, TEXT("Alive"));
		}
		if (Failed())
		{
			return false;
		}

		// Interactable: the interface's Interact, returning true
		{
			FTestBlueprint& B = Bp(TEXT("BP_N2CBench_Interactable"));
			if (!FBlueprintEditorUtils::ImplementNewInterface(B.GetBlueprint(), Interface->GeneratedClass->GetClassPathName()))
			{
				OutErrors.Add(TEXT("BP_N2CBench_Interactable could not implement ") + FString(InterfaceName));
				return false;
			}
			UEdGraph* Interact = B.GetBlueprint()->ImplementedInterfaces.Num() > 0 && B.GetBlueprint()->ImplementedInterfaces[0].Graphs.Num() > 0
				? B.GetBlueprint()->ImplementedInterfaces[0].Graphs[0].Get() : nullptr;
			UK2Node_FunctionResult* Result = ResultOf(Interact);
			UK2Node_FunctionEntry* Entry = nullptr;
			for (UEdGraphNode* Node : Interact ? Interact->Nodes : TArray<TObjectPtr<UEdGraphNode>>())
			{
				Entry = Entry ? Entry : Cast<UK2Node_FunctionEntry>(Node);
			}
			UK2Node_CallFunction* Printed = Interact ? Print(B, Interact, TEXT("interacted")) : nullptr;
			if (!Result || !Entry || !Printed || !Then(B, Entry, Printed) || !Then(B, Printed, Result))
			{
				OutErrors.Add(TEXT("BP_N2CBench_Interactable: the Interact graph"));
				Failed();
				return false;
			}
			if (UEdGraphPin* Handled = Result->FindPin(TEXT("Handled")))
			{
				Handled->DefaultValue = TEXT("true");
			}
		}

		// Compile the Actor Blueprints made so far
		for (TPair<FString, TUniquePtr<FTestBlueprint>>& Pair : Actors)
		{
			if (!Compile(Pair.Value->GetBlueprint(), OutErrors))
			{
				return false;
			}
		}

		// HealthComponent: an Actor Component Blueprint
		UBlueprint* Health = CreateBlueprint(UActorComponent::StaticClass(), Packages[TEXT("BP_N2CBench_HealthComponent")],
			TEXT("BP_N2CBench_HealthComponent"), BPTYPE_Normal, OutErrors);
		if (!Health)
		{
			return false;
		}
		FBlueprintEditorUtils::AddMemberVariable(Health, TEXT("Health"), FTestBlueprint::IntPinType(), TEXT("100"));
		if (!Compile(Health, OutErrors))
		{
			return false;
		}
		{
			UEdGraph* Graph = FBlueprintEditorUtils::CreateNewGraph(Health, TEXT("ApplyDamage"), UEdGraph::StaticClass(), UEdGraphSchema_K2::StaticClass());
			FBlueprintEditorUtils::AddFunctionGraph<UClass>(Health, Graph, /*bIsUserCreated*/ true, nullptr);
			Helper.AddFunctionInput(Graph, TEXT("Amount"), FTestBlueprint::IntPinType());
			FString Error;
			const FProperty* Get = FN2CMcpVariableUtils::FindMemberVariableProperty(Health, TEXT("Health"), false, Error);
			const FProperty* SetProperty = FN2CMcpVariableUtils::FindMemberVariableProperty(Health, TEXT("Health"), true, Error);
			UK2Node_Variable* GetNode = Get ? FN2CMcpNodeSpawn::SpawnMemberVariableNode(Health, Graph, Get, false, FVector2D(0, 200), Error) : nullptr;
			UK2Node_Variable* SetNode = SetProperty ? FN2CMcpNodeSpawn::SpawnMemberVariableNode(Health, Graph, SetProperty, true, FVector2D(400, 0), Error) : nullptr;
			UK2Node_CallFunction* Minus = Math(Helper, Graph, GET_FUNCTION_NAME_CHECKED(UKismetMathLibrary, Subtract_IntInt));
			UK2Node_FunctionEntry* Entry = nullptr;
			for (UEdGraphNode* Node : Graph->Nodes)
			{
				Entry = Entry ? Entry : Cast<UK2Node_FunctionEntry>(Node);
			}
			if (!GetNode || !SetNode || !Minus || !Entry)
			{
				OutErrors.Add(TEXT("BP_N2CBench_HealthComponent: ") + Error);
				return false;
			}
			Then(Helper, Entry, SetNode);
			Helper.Link(NthPin(GetNode, EGPD_Output, false), Minus->FindPin(TEXT("A")));
			Helper.Link(Entry->FindPin(TEXT("Amount")), Minus->FindPin(TEXT("B")));
			Helper.Link(Minus->GetReturnValuePin(), NthPin(SetNode, EGPD_Input, false));
		}

		// Widget: a custom event that prints
		UBlueprint* Widget = CreateBlueprint(UUserWidget::StaticClass(), Packages[TEXT("WBP_N2CBench_Widget")], TEXT("WBP_N2CBench_Widget"),
			BPTYPE_Normal, OutErrors, UWidgetBlueprint::StaticClass(), UWidgetBlueprintGeneratedClass::StaticClass());
		if (!Widget)
		{
			return false;
		}
		{
			UEdGraph* Graph = FBlueprintEditorUtils::FindEventGraph(Widget);
			Then(Helper, Helper.AddCustomEvent(Graph, TEXT("ShowScore")), Print(Helper, Graph, TEXT("score shown")));
		}

		// PickupChild: a child of Pickup with its own BeginPlay
		UBlueprint* Child = CreateBlueprint(Bp(TEXT("BP_N2CBench_Pickup")).GetBlueprint()->GeneratedClass, Packages[TEXT("BP_N2CBench_PickupChild")],
			TEXT("BP_N2CBench_PickupChild"), BPTYPE_Normal, OutErrors);
		if (!Child)
		{
			return false;
		}
		{
			UEdGraph* Graph = FBlueprintEditorUtils::FindEventGraph(Child);
			Then(Helper, Helper.FindEvent(Graph, TEXT("ReceiveBeginPlay")), Print(Helper, Graph, TEXT("child ready")));
		}
		if (Failed() || !Compile(Health, OutErrors) || !Compile(Widget, OutErrors) || !Compile(Child, OutErrors))
		{
			Failed();
			return false;
		}

		// Save the assets, then write the shapes and the manifest from them
		TArray<UObject*> Assets = { Enum, Struct, Interface };
		for (const FFixture& Fixture : All())
		{
			const FString Name = Fixture.Name;
			Assets.Add(Name == TEXT("BP_N2CBench_HealthComponent") ? Health : Name == TEXT("WBP_N2CBench_Widget") ? Widget
				: Name == TEXT("BP_N2CBench_PickupChild") ? Child : Actors[Name]->GetBlueprint());
		}
		for (UObject* Asset : Assets)
		{
			if (!Save(Asset, OutErrors, OutNotes))
			{
				return false;
			}
		}
		IFileManager::Get().MakeDirectory(*(Corpus / TEXT("shapes")), /*Tree*/ true);
		for (int32 Index = 0; Index < All().Num(); ++Index)
		{
			UBlueprint* Blueprint = Cast<UBlueprint>(Assets[Index + 3]);
			const FString File = Corpus / TEXT("shapes") / Blueprint->GetName() + TEXT(".json");
			if (!FFileHelper::SaveStringToFile(StableJson(ComputeShape(*Blueprint)), *File, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM))
			{
				OutErrors.Add(TEXT("could not write ") + File);
				return false;
			}
			OutNotes.Add(TEXT("wrote ") + File);
		}
		const TSharedRef<FJsonObject> Manifest = BuildManifest(OutErrors);
		const FString ManifestFile = Corpus / TEXT("manifest.json");
		if (OutErrors.Num() > 0 || !FFileHelper::SaveStringToFile(StableJson(Manifest), *ManifestFile, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM))
		{
			OutErrors.Add(TEXT("could not write ") + ManifestFile);
			return false;
		}
		OutNotes.Add(TEXT("wrote ") + ManifestFile);
		return true;
#endif
	}
}

#endif // WITH_DEV_AUTOMATION_TESTS
