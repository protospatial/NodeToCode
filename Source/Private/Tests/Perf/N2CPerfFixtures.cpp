// Copyright (c) 2026 Nick McClure (Protospatial)
// SPDX-License-Identifier: Apache-2.0

#include "Tests/Perf/N2CPerfFixtures.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Tests/N2CTestBlueprintFactory.h"
#include "EdGraph/EdGraph.h"
#include "EdGraphSchema_K2.h"
#include "Engine/Blueprint.h"
#include "Engine/UserDefinedEnum.h"
#include "GameFramework/Pawn.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformMisc.h"
#include "Interfaces/IPluginManager.h"
#include "K2Node.h"
#include "K2Node_CallFunction.h"
#include "K2Node_Composite.h"
#include "K2Node_CustomEvent.h"
#include "K2Node_DynamicCast.h"
#include "K2Node_Event.h"
#include "K2Node_ExecutionSequence.h"
#include "K2Node_FunctionEntry.h"
#include "K2Node_FunctionResult.h"
#include "K2Node_IfThenElse.h"
#include "K2Node_MacroInstance.h"
#include "K2Node_MakeStruct.h"
#include "K2Node_BreakStruct.h"
#include "K2Node_SwitchEnum.h"
#include "K2Node_Tunnel.h"
#include "K2Node_VariableGet.h"
#include "K2Node_VariableSet.h"
#include "Kismet/KismetMathLibrary.h"
#include "Kismet/KismetStringLibrary.h"
#include "Kismet/KismetSystemLibrary.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/CompilerResultsLog.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Math/RandomStream.h"
#include "Misc/CommandLine.h"
#include "Misc/PackageName.h"
#include "Misc/Parse.h"
#include "Runtime/Launch/Resources/Version.h"
#include "StructUtils/UserDefinedStruct.h"
#include "UObject/Package.h"
#include "UObject/SavePackage.h"

namespace N2CTest::PerfFixtures
{
	TConstArrayView<FFixture> All()
	{
		static const FFixture Fixtures[] = {
			{ TEXT("BP_N2CPerf_100"), 100, 0, 0, 0, 0 },
			{ TEXT("BP_N2CPerf_1k"), 1000, 10, 2, 0, 0 },
			{ TEXT("BP_N2CPerf_5k"), 5000, 30, 5, 1, 5 },
		};
		return Fixtures;
	}

	FString ObjectPath(const TCHAR* AssetName)
	{
		return FString::Printf(TEXT("%s/%s.%s"), FolderPath, AssetName, AssetName);
	}

	int32 CountK2Nodes(const UBlueprint& Blueprint)
	{
		TArray<UEdGraph*> Graphs;
		Blueprint.GetAllGraphs(Graphs);
		int32 Count = 0;
		for (const UEdGraph* Graph : Graphs)
		{
			if (!Graph)
			{
				continue;
			}
			for (const UEdGraphNode* Node : Graph->Nodes)
			{
				Count += Node && Node->IsA<UK2Node>() ? 1 : 0;
			}
		}
		return Count;
	}

	bool ShouldGenerate()
	{
		return FPlatformMisc::GetEnvironmentVariable(TEXT("N2C_GENERATE_PERF_FIXTURES")) == TEXT("1")
			|| FParse::Param(FCommandLine::Get(), TEXT("N2CGeneratePerfFixtures"));
	}

	namespace
	{
		const FName ScoreVar(TEXT("Score"));
		const FName ValuesVar(TEXT("Values"));
		const FName ModeVar(TEXT("Mode"));
		const FName DataVar(TEXT("Data"));
		const FName ReadyVar(TEXT("bReady"));
		const FName ValueParam(TEXT("Value"));
		const FName ResultParam(TEXT("Result"));

		/** Where the next node of an exec chain goes */
		struct FCursor
		{
			UEdGraph* Graph = nullptr;
			/** The exec output the next impure node is linked to */
			UEdGraphPin* Exec = nullptr;
			/** The function's Value input, when the chain is in a function */
			UEdGraphPin* ValueInput = nullptr;
			int32 X = 0;
			int32 Y = 0;
			/** Blocks since the chain's event, in an event graph (a new custom event starts a new chain) */
			int32 BlocksInChain = 0;
			bool bEventGraph = false;
			/** Calls to the Blueprint's own functions and instances of its macros are allowed here */
			bool bCalls = true;
			bool bMacros = true;
		};

		/**
		 * Builds one fixture Blueprint. Every random choice comes from one FRandomStream seeded per fixture, and graphs
		 * are filled in a fixed order, so the same seed gives the same graph structure.
		 */
		class FBuilder
		{
		public:
			FBuilder(FTestBlueprint& InBP, const FFixture& InFixture, UUserDefinedStruct* InStruct, UUserDefinedEnum* InEnum)
				: BP(InBP), Fixture(InFixture), Rand(359 + InFixture.TargetNodes), Struct(InStruct), Enum(InEnum)
			{
			}

			bool Build()
			{
				UBlueprint* Blueprint = BP.GetBlueprint();
				if (!Blueprint || !Struct || !Enum)
				{
					return false;
				}
				FEdGraphPinType IntArray = FTestBlueprint::IntPinType();
				IntArray.ContainerType = EPinContainerType::Array;
				if (!BP.AddMemberVariable(ScoreVar, FTestBlueprint::IntPinType())
					|| !BP.AddMemberVariable(ValuesVar, IntArray)
					|| !BP.AddMemberVariable(ModeVar, FTestBlueprint::EnumPinType(Enum))
					|| !BP.AddMemberVariable(DataVar, FTestBlueprint::StructPinType(Struct))
					|| !BP.AddMemberVariable(ReadyVar, FTestBlueprint::BoolPinType()))
				{
					return false;
				}

				// Signatures first, then one skeleton regeneration, so calls resolve without one per call
				const int32 ChainFunctions = Fixture.ChainDepth > 0 ? Fixture.ChainDepth + 1 : 0;
				const int32 WorkFunctions = Fixture.Functions - ChainFunctions;
				for (int32 Index = 0; Index < WorkFunctions; ++Index)
				{
					WorkGraphs.Add(AddSignature(FName(*FString::Printf(TEXT("F_Work_%02d"), Index))));
				}
				for (int32 Index = 0; Index < ChainFunctions; ++Index)
				{
					ChainGraphs.Add(AddSignature(FName(*FString::Printf(TEXT("%s%d"), ChainPrefix, Index))));
				}
				for (int32 Index = 0; Index < Fixture.Macros; ++Index)
				{
					UEdGraph* Macro = BP.AddMacro(FName(*FString::Printf(TEXT("M_Perf_%02d"), Index)));
					BP.AddTunnelPin(BP.TunnelEntry(Macro), TEXT("In"), FTestBlueprint::ExecPinType());
					BP.AddTunnelPin(BP.TunnelExit(Macro), TEXT("Out"), FTestBlueprint::ExecPinType());
					MacroGraphs.Add(Macro);
				}
				FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);
				if (BP.Errors().Num() > 0)
				{
					return false;
				}

				// Budgets: the event graph gets what the other graphs leave (about 2000 of 5000 nodes in the 5k fixture)
				const int32 MacroBudget = Fixture.TargetNodes >= 5000 ? 40 : 30;
				const int32 WorkBudget = Fixture.TargetNodes >= 5000 ? 100 : 45;
				const int32 ChainBudget = 20;
				const int32 CollapsedBudget = 250;
				int32 Goal = CountK2Nodes(*Blueprint);

				for (int32 Index = 0; Index < WorkGraphs.Num(); ++Index)
				{
					// A work function calls only lower-numbered ones, so calls never loop
					CallableWork = Index;
					FCursor Cursor = FunctionCursor(WorkGraphs[Index]);
					Goal += WorkBudget;
					Fill(Cursor, Goal - 3);
					FinishFunction(Cursor);
				}
				CallableWork = WorkGraphs.Num();
				for (UEdGraph* Macro : MacroGraphs)
				{
					FCursor Cursor;
					Cursor.Graph = Macro;
					Cursor.Exec = BP.Pin(BP.TunnelEntry(Macro), TEXT("In"), EGPD_Output);
					Cursor.bMacros = false;
					Goal += MacroBudget;
					Fill(Cursor, Goal);
					BP.Link(Cursor.Exec, BP.Pin(BP.TunnelExit(Macro), TEXT("Out"), EGPD_Input));
				}
				for (int32 Index = 0; Index < ChainGraphs.Num(); ++Index)
				{
					// Only the next link of the chain is called from a chain function, so its depth is exact
					FCursor Cursor = FunctionCursor(ChainGraphs[Index]);
					Cursor.bCalls = false;
					Cursor.bMacros = false;
					Goal += ChainBudget;
					Fill(Cursor, Goal - 5);
					if (ChainGraphs.IsValidIndex(Index + 1))
					{
						AddCallTo(Cursor, ChainGraphs[Index + 1]->GetFName());
					}
					FinishFunction(Cursor);
				}

				UEdGraph* EventGraph = BP.EventGraph();
				FCursor Cursor;
				Cursor.Graph = EventGraph;
				Cursor.bEventGraph = true;
				UK2Node_Event* BeginPlay = BP.FindEvent(EventGraph, TEXT("ReceiveBeginPlay"));
				Cursor.Exec = BP.Pin(BeginPlay, UEdGraphSchema_K2::PN_Then, EGPD_Output);
				if (ChainGraphs.Num() > 0)
				{
					AddCallTo(Cursor, ChainGraphs[0]->GetFName());
				}
				for (int32 Index = 0; Index < Fixture.CollapsedGraphs; ++Index)
				{
					AddCollapsed(Cursor, FName(*FString::Printf(TEXT("Perf_Collapsed_%d"), Index)), CollapsedBudget);
				}
				Fill(Cursor, Fixture.TargetNodes);
				return BP.Errors().Num() == 0 && Errors.Num() == 0;
			}

			/** What failed outside FTestBlueprint's own helpers */
			TArray<FString> Errors;

		private:
			FTestBlueprint& BP;
			const FFixture& Fixture;
			FRandomStream Rand;
			UUserDefinedStruct* Struct;
			UUserDefinedEnum* Enum;
			TArray<UEdGraph*> WorkGraphs;
			TArray<UEdGraph*> ChainGraphs;
			TArray<UEdGraph*> MacroGraphs;
			/** Work functions [0, CallableWork) may be called from the graph being filled */
			int32 CallableWork = 0;
			int32 EventCount = 0;
			int32 PrintCount = 0;

			UEdGraph* AddSignature(const FName Name)
			{
				UEdGraph* Graph = BP.AddFunction(Name);
				BP.AddFunctionInput(Graph, ValueParam, FTestBlueprint::IntPinType());
				BP.AddFunctionOutput(Graph, ResultParam, FTestBlueprint::IntPinType());
				return Graph;
			}

			FCursor FunctionCursor(UEdGraph* Graph)
			{
				UK2Node_FunctionEntry* Entry = BP.FunctionEntry(Graph);
				FCursor Cursor;
				Cursor.Graph = Graph;
				Cursor.Exec = BP.Pin(Entry, UEdGraphSchema_K2::PN_Then, EGPD_Output);
				Cursor.ValueInput = BP.Pin(Entry, ValueParam, EGPD_Output);
				return Cursor;
			}

			/** Result = Value + Score, then the return node (3 nodes counted against the function's budget) */
			void FinishFunction(FCursor& Cursor)
			{
				UK2Node_FunctionResult* Result = nullptr;
				for (UEdGraphNode* Node : Cursor.Graph->Nodes)
				{
					Result = Result ? Result : Cast<UK2Node_FunctionResult>(Node);
				}
				if (!Result)
				{
					Errors.Add(Cursor.Graph->GetName() + TEXT(" has no return node"));
					return;
				}
				UK2Node_CallFunction* Add = Math(Cursor, GET_FUNCTION_NAME_CHECKED(UKismetMathLibrary, Add_IntInt), 150);
				BP.Link(Cursor.ValueInput, BP.Pin(Add, TEXT("A"), EGPD_Input));
				BP.Link(Out(GetVar(Cursor, ScoreVar, 250), ScoreVar), BP.Pin(Add, TEXT("B"), EGPD_Input));
				BP.Link(Out(Add, UEdGraphSchema_K2::PN_ReturnValue), BP.Pin(Result, ResultParam, EGPD_Input));
				BP.Link(Cursor.Exec, BP.Pin(Result, UEdGraphSchema_K2::PN_Execute, EGPD_Input));
				Place(Result, Cursor, 0);
			}

			/** Adds blocks until the Blueprint holds Goal K2 nodes; the last few are single Print String nodes */
			void Fill(FCursor& Cursor, const int32 Goal)
			{
				UBlueprint* Blueprint = BP.GetBlueprint();
				for (int32 Count = CountK2Nodes(*Blueprint); Count < Goal && BP.Errors().Num() == 0 && Errors.Num() == 0; Count = CountK2Nodes(*Blueprint))
				{
					if (Goal - Count <= 6)
					{
						Print(Cursor, nullptr);
						continue;
					}
					if (Cursor.bEventGraph && Cursor.BlocksInChain >= 12)
					{
						StartChain(Cursor);
						continue;
					}
					AddBlock(Cursor);
					++Cursor.BlocksInChain;
				}
			}

			/** One block of at most six nodes, by weighted random choice */
			void AddBlock(FCursor& Cursor)
			{
				const bool bCanCall = Cursor.bCalls && CallableWork > 0;
				const bool bCanMacro = Cursor.bMacros && MacroGraphs.Num() > 0;
				const int32 Roll = Rand.RandRange(0, 99);
				if (Roll < 10) { PrintValue(Cursor); }
				else if (Roll < 22) { SetScore(Cursor); }
				else if (Roll < 34) { Branch(Cursor); }
				else if (Roll < 42) { Sequence(Cursor); }
				else if (Roll < 50) { ForEach(Cursor); }
				else if (Roll < 57) { CastToPawn(Cursor); }
				else if (Roll < 64) { MakeData(Cursor); }
				else if (Roll < 71) { BreakData(Cursor); }
				else if (Roll < 78) { SwitchMode(Cursor); }
				else if (Roll < 84) { ToggleReady(Cursor); }
				else if (Roll < 94 && bCanCall) { AddCallTo(Cursor, WorkGraphs[Rand.RandRange(0, CallableWork - 1)]->GetFName()); }
				else if (bCanMacro) { MacroInstance(Cursor); }
				else { PrintValue(Cursor); }
			}

			// Nodes and pins

			void Place(UEdGraphNode* Node, FCursor& Cursor, const int32 OffsetY)
			{
				if (Node)
				{
					Node->NodePosX = Cursor.X;
					Node->NodePosY = Cursor.Y + OffsetY;
				}
			}

			/** Places an impure node at the cursor, links the chain's exec into it and moves the cursor past it */
			void Chain(UEdGraphNode* Node, FCursor& Cursor, const FName NextExec = UEdGraphSchema_K2::PN_Then)
			{
				if (!Node)
				{
					return;
				}
				Place(Node, Cursor, 0);
				BP.Link(Cursor.Exec, BP.Pin(Node, UEdGraphSchema_K2::PN_Execute, EGPD_Input));
				Cursor.Exec = BP.Pin(Node, NextExec, EGPD_Output);
				Cursor.X += 320;
			}

			UEdGraphPin* Out(UEdGraphNode* Node, const FName Name)
			{
				return BP.Pin(Node, Name, EGPD_Output);
			}

			UEdGraphPin* In(UEdGraphNode* Node, const FName Name)
			{
				return BP.Pin(Node, Name, EGPD_Input);
			}

			UK2Node_VariableGet* GetVar(FCursor& Cursor, const FName Name, const int32 OffsetY)
			{
				UK2Node_VariableGet* Node = BP.AddVariableGet(Cursor.Graph, Name);
				Place(Node, Cursor, OffsetY);
				return Node;
			}

			UK2Node_VariableSet* SetVar(FCursor& Cursor, const FName Name)
			{
				const FGuid Guid = FBlueprintEditorUtils::FindMemberVariableGuidByName(BP.GetBlueprint(), Name);
				FGraphNodeCreator<UK2Node_VariableSet> Creator(*Cursor.Graph);
				UK2Node_VariableSet* Node = Creator.CreateNode();
				Node->VariableReference.SetSelfMember(Name, Guid);
				Creator.Finalize();
				return Node;
			}

			UK2Node_CallFunction* Math(FCursor& Cursor, const FName Function, const int32 OffsetY)
			{
				UK2Node_CallFunction* Node = BP.AddCall(Cursor.Graph, UKismetMathLibrary::StaticClass(), Function);
				Place(Node, Cursor, OffsetY);
				return Node;
			}

			/** An int to read: the function's Value input half the time in a function, else a Get Score node */
			UEdGraphPin* IntSource(FCursor& Cursor, const int32 OffsetY)
			{
				if (Cursor.ValueInput && Rand.RandRange(0, 1) == 0)
				{
					return Cursor.ValueInput;
				}
				return Out(GetVar(Cursor, ScoreVar, OffsetY), ScoreVar);
			}

			/** Print String: of Text when given (a data pin), else of a fixed literal. 1 node. */
			UK2Node_CallFunction* Print(FCursor& Cursor, UEdGraphPin* Text)
			{
				UK2Node_CallFunction* Node = BP.AddCall(Cursor.Graph, UKismetSystemLibrary::StaticClass(),
					GET_FUNCTION_NAME_CHECKED(UKismetSystemLibrary, PrintString));
				if (UEdGraphPin* InString = In(Node, TEXT("InString")))
				{
					if (Text)
					{
						BP.Link(Text, InString);
					}
					else
					{
						InString->DefaultValue = FString::Printf(TEXT("Perf %d"), ++PrintCount);
					}
				}
				Chain(Node, Cursor);
				return Node;
			}

			/** An int to string, printed: up to 3 nodes */
			void PrintValue(FCursor& Cursor)
			{
				UEdGraphPin* Source = IntSource(Cursor, 260);
				UK2Node_CallFunction* Convert = BP.AddCall(Cursor.Graph, UKismetStringLibrary::StaticClass(),
					GET_FUNCTION_NAME_CHECKED(UKismetStringLibrary, Conv_IntToString));
				Place(Convert, Cursor, 160);
				BP.Link(Source, In(Convert, TEXT("InInt")));
				Print(Cursor, Out(Convert, UEdGraphSchema_K2::PN_ReturnValue));
			}

			/** Score = Score * 3 + (Value or Score): up to 5 nodes */
			void SetScore(FCursor& Cursor)
			{
				UK2Node_CallFunction* Multiply = Math(Cursor, GET_FUNCTION_NAME_CHECKED(UKismetMathLibrary, Multiply_IntInt), 160);
				BP.Link(Out(GetVar(Cursor, ScoreVar, 260), ScoreVar), In(Multiply, TEXT("A")));
				if (UEdGraphPin* B = In(Multiply, TEXT("B")))
				{
					B->DefaultValue = FString::FromInt(Rand.RandRange(2, 9));
				}
				UK2Node_CallFunction* Add = Math(Cursor, GET_FUNCTION_NAME_CHECKED(UKismetMathLibrary, Add_IntInt), 360);
				BP.Link(Out(Multiply, UEdGraphSchema_K2::PN_ReturnValue), In(Add, TEXT("A")));
				BP.Link(IntSource(Cursor, 460), In(Add, TEXT("B")));
				UK2Node_VariableSet* Set = SetVar(Cursor, ScoreVar);
				BP.Link(Out(Add, UEdGraphSchema_K2::PN_ReturnValue), In(Set, ScoreVar));
				Chain(Set, Cursor);
			}

			/** if (Score > n) Print else go on: up to 4 nodes */
			void Branch(FCursor& Cursor)
			{
				UK2Node_CallFunction* Greater = Math(Cursor, GET_FUNCTION_NAME_CHECKED(UKismetMathLibrary, Greater_IntInt), 160);
				BP.Link(IntSource(Cursor, 260), In(Greater, TEXT("A")));
				if (UEdGraphPin* B = In(Greater, TEXT("B")))
				{
					B->DefaultValue = FString::FromInt(Rand.RandRange(0, 100));
				}
				UK2Node_IfThenElse* If = BP.AddBranch(Cursor.Graph);
				BP.Link(Out(Greater, UEdGraphSchema_K2::PN_ReturnValue), In(If, UEdGraphSchema_K2::PN_Condition));
				Chain(If, Cursor, UEdGraphSchema_K2::PN_Else);
				FCursor Arm = Cursor;
				Arm.Exec = Out(If, UEdGraphSchema_K2::PN_Then);
				Arm.Y -= 300;
				Print(Arm, nullptr);
			}

			/** Sequence: then_0 prints a value, then_1 goes on. Up to 4 nodes */
			void Sequence(FCursor& Cursor)
			{
				UK2Node_ExecutionSequence* Node = BP.AddSequence(Cursor.Graph);
				Chain(Node, Cursor, TEXT("then_1"));
				FCursor Arm = Cursor;
				Arm.Exec = Out(Node, TEXT("then_0"));
				Arm.Y -= 300;
				PrintValue(Arm);
			}

			/** ForEachLoop (StandardMacros) over Values: Loop Body prints the element, Completed goes on. 4 nodes */
			void ForEach(FCursor& Cursor)
			{
				UK2Node_MacroInstance* Loop = BP.AddStandardMacro(Cursor.Graph, TEXT("ForEachLoop"));
				BP.Link(Out(GetVar(Cursor, ValuesVar, 200), ValuesVar), In(Loop, TEXT("Array")));
				Place(Loop, Cursor, 0);
				BP.Link(Cursor.Exec, In(Loop, TEXT("Exec")));
				Cursor.Exec = Out(Loop, TEXT("Completed"));
				Cursor.X += 320;
				FCursor Body = Cursor;
				Body.Exec = Out(Loop, TEXT("LoopBody"));
				Body.Y -= 300;
				UK2Node_CallFunction* Convert = BP.AddCall(Cursor.Graph, UKismetStringLibrary::StaticClass(),
					GET_FUNCTION_NAME_CHECKED(UKismetStringLibrary, Conv_IntToString));
				Place(Convert, Body, 160);
				BP.Link(Out(Loop, TEXT("Array Element")), In(Convert, TEXT("InInt")));
				Print(Body, Out(Convert, UEdGraphSchema_K2::PN_ReturnValue));
			}

			/** Cast (Get Owner) to Pawn; Cast Failed prints, the cast goes on. 3 nodes */
			void CastToPawn(FCursor& Cursor)
			{
				UK2Node_CallFunction* Owner = BP.AddCall(Cursor.Graph, AActor::StaticClass(), FName(TEXT("GetOwner")));
				Place(Owner, Cursor, 200);
				FGraphNodeCreator<UK2Node_DynamicCast> CastCreator(*Cursor.Graph);
				UK2Node_DynamicCast* CastNode = CastCreator.CreateNode();
				CastNode->TargetType = APawn::StaticClass();
				CastCreator.Finalize();
				BP.Link(Out(Owner, UEdGraphSchema_K2::PN_ReturnValue), CastNode->GetCastSourcePin());
				Place(CastNode, Cursor, 0);
				BP.Link(Cursor.Exec, CastNode->GetExecPin());
				Cursor.Exec = CastNode->GetValidCastPin();
				Cursor.X += 320;
				FCursor Failed = Cursor;
				Failed.Exec = CastNode->GetInvalidCastPin();
				Failed.Y -= 300;
				Print(Failed, nullptr);
			}

			/** Data = Make S_N2CPerf(Count: Score, Mode: Mode): 4 nodes */
			void MakeData(FCursor& Cursor)
			{
				UK2Node_MakeStruct* Make = BP.AddMakeStruct(Cursor.Graph, Struct);
				Place(Make, Cursor, 160);
				for (UEdGraphPin* Pin : Make ? Make->Pins : TArray<UEdGraphPin*>())
				{
					if (Pin->Direction != EGPD_Input || Pin->bHidden)
					{
						continue;
					}
					if (Pin->PinType.PinCategory == UEdGraphSchema_K2::PC_Int)
					{
						BP.Link(IntSource(Cursor, 300), Pin);
					}
					else if (Pin->PinType.PinCategory == UEdGraphSchema_K2::PC_Byte)
					{
						BP.Link(Out(GetVar(Cursor, ModeVar, 400), ModeVar), Pin);
					}
				}
				UK2Node_VariableSet* Set = SetVar(Cursor, DataVar);
				BP.Link(Make ? FindOutput(Make, UEdGraphSchema_K2::PC_Struct) : nullptr, In(Set, DataVar));
				Chain(Set, Cursor);
			}

			/** Print (Break Data).Count: 4 nodes */
			void BreakData(FCursor& Cursor)
			{
				UK2Node_BreakStruct* Break = BP.AddBreakStruct(Cursor.Graph, Struct);
				Place(Break, Cursor, 260);
				BP.Link(Out(GetVar(Cursor, DataVar, 400), DataVar), Break ? FindInput(Break, UEdGraphSchema_K2::PC_Struct) : nullptr);
				UK2Node_CallFunction* Convert = BP.AddCall(Cursor.Graph, UKismetStringLibrary::StaticClass(),
					GET_FUNCTION_NAME_CHECKED(UKismetStringLibrary, Conv_IntToString));
				Place(Convert, Cursor, 160);
				BP.Link(Break ? FindOutput(Break, UEdGraphSchema_K2::PC_Int) : nullptr, In(Convert, TEXT("InInt")));
				Print(Cursor, Out(Convert, UEdGraphSchema_K2::PN_ReturnValue));
			}

			/** Switch on Mode: the first case prints, the last goes on. 3 nodes */
			void SwitchMode(FCursor& Cursor)
			{
				UK2Node_SwitchEnum* Switch = BP.AddSwitchOnEnum(Cursor.Graph, Enum);
				BP.Link(Out(GetVar(Cursor, ModeVar, 200), ModeVar), Switch ? Switch->GetSelectionPin() : nullptr);
				TArray<UEdGraphPin*> Cases;
				for (UEdGraphPin* Pin : Switch ? Switch->Pins : TArray<UEdGraphPin*>())
				{
					if (Pin->Direction == EGPD_Output && Pin->PinType.PinCategory == UEdGraphSchema_K2::PC_Exec)
					{
						Cases.Add(Pin);
					}
				}
				if (!Switch || Cases.Num() < 2)
				{
					Errors.Add(TEXT("Switch on E_N2CPerf has fewer than two cases"));
					return;
				}
				Place(Switch, Cursor, 0);
				BP.Link(Cursor.Exec, Switch->GetExecPin());
				Cursor.Exec = Cases.Last();
				Cursor.X += 320;
				FCursor Arm = Cursor;
				Arm.Exec = Cases[0];
				Arm.Y -= 300;
				Print(Arm, nullptr);
			}

			/** bReady = !bReady: 3 nodes */
			void ToggleReady(FCursor& Cursor)
			{
				UK2Node_CallFunction* Not = Math(Cursor, GET_FUNCTION_NAME_CHECKED(UKismetMathLibrary, Not_PreBool), 160);
				BP.Link(Out(GetVar(Cursor, ReadyVar, 260), ReadyVar), In(Not, TEXT("A")));
				UK2Node_VariableSet* Set = SetVar(Cursor, ReadyVar);
				BP.Link(Out(Not, UEdGraphSchema_K2::PN_ReturnValue), In(Set, ReadyVar));
				Chain(Set, Cursor);
			}

			/** A call to one of the Blueprint's own functions with an int: up to 2 nodes */
			void AddCallTo(FCursor& Cursor, const FName Function)
			{
				UK2Node_CallFunction* Call = BP.AddCall(Cursor.Graph, BP.GetBlueprint()->SkeletonGeneratedClass, Function);
				BP.Link(IntSource(Cursor, 200), In(Call, ValueParam));
				Chain(Call, Cursor);
			}

			/** An instance of one of the Blueprint's own macros: 1 node */
			void MacroInstance(FCursor& Cursor)
			{
				UK2Node_MacroInstance* Node = BP.AddMacroInstance(Cursor.Graph, MacroGraphs[Rand.RandRange(0, MacroGraphs.Num() - 1)]);
				Place(Node, Cursor, 0);
				BP.Link(Cursor.Exec, In(Node, TEXT("In")));
				Cursor.Exec = Out(Node, TEXT("Out"));
				Cursor.X += 320;
			}

			/** A new custom event and an exec chain from it, below the last one: 1 node */
			void StartChain(FCursor& Cursor)
			{
				UK2Node_CustomEvent* Event = BP.AddCustomEvent(Cursor.Graph, FName(*FString::Printf(TEXT("Perf_Event_%03d"), ++EventCount)));
				Cursor.X = 0;
				Cursor.Y += 800;
				Cursor.BlocksInChain = 0;
				Place(Event, Cursor, 0);
				Cursor.Exec = Out(Event, UEdGraphSchema_K2::PN_Then);
				Cursor.X += 320;
			}

			/** A collapsed graph in the chain, filled with about Budget nodes */
			void AddCollapsed(FCursor& Cursor, const FName Name, const int32 Budget)
			{
				UK2Node_Composite* Collapsed = BP.AddCollapsedGraph(Cursor.Graph, Name);
				UEdGraph* Bound = Collapsed ? Collapsed->BoundGraph.Get() : nullptr;
				UK2Node_Tunnel* Entry = BP.TunnelEntry(Bound);
				UK2Node_Tunnel* Exit = BP.TunnelExit(Bound);
				BP.AddTunnelPin(Entry, TEXT("In"), FTestBlueprint::ExecPinType());
				BP.AddTunnelPin(Exit, TEXT("Out"), FTestBlueprint::ExecPinType());
				Place(Collapsed, Cursor, 0);
				BP.Link(Cursor.Exec, In(Collapsed, TEXT("In")));
				Cursor.Exec = Out(Collapsed, TEXT("Out"));
				Cursor.X += 320;
				if (!Bound)
				{
					return;
				}
				FCursor Inner;
				Inner.Graph = Bound;
				Inner.Exec = Out(Entry, TEXT("In"));
				Inner.bCalls = Cursor.bCalls;
				Inner.bMacros = Cursor.bMacros;
				Fill(Inner, CountK2Nodes(*BP.GetBlueprint()) + Budget);
				BP.Link(Inner.Exec, In(Exit, TEXT("Out")));
			}

			static UEdGraphPin* FindPin(UEdGraphNode* Node, const EEdGraphPinDirection Direction, const FName Category)
			{
				for (UEdGraphPin* Pin : Node->Pins)
				{
					if (Pin && Pin->Direction == Direction && !Pin->bHidden && Pin->PinType.PinCategory == Category)
					{
						return Pin;
					}
				}
				return nullptr;
			}
			static UEdGraphPin* FindOutput(UEdGraphNode* Node, const FName Category) { return FindPin(Node, EGPD_Output, Category); }
			static UEdGraphPin* FindInput(UEdGraphNode* Node, const FName Category) { return FindPin(Node, EGPD_Input, Category); }
		};

		UPackage* NewAssetPackage(const TCHAR* AssetName, TArray<FString>& OutErrors)
		{
			const FString PackageName = FString::Printf(TEXT("%s/%s"), FolderPath, AssetName);
			if (FindPackage(nullptr, *PackageName))
			{
				OutErrors.Add(PackageName + TEXT(" is already loaded: regenerate in a fresh editor (only NodeToCode.Perf in the filter)"));
				return nullptr;
			}
			UPackage* Package = CreatePackage(*PackageName);
			if (!Package)
			{
				OutErrors.Add(TEXT("could not create ") + PackageName);
			}
			return Package;
		}

		bool Save(UObject* Asset, TArray<FString>& OutErrors, TArray<FString>& OutNotes)
		{
			UPackage* Package = Asset->GetPackage();
			const FString File = FPackageName::LongPackageNameToFilename(Package->GetName(), FPackageName::GetAssetPackageExtension());
			Package->MarkPackageDirty();
			FSavePackageArgs Args;
			Args.TopLevelFlags = RF_Public | RF_Standalone;
			if (!UPackage::SavePackage(Package, Asset, *FPaths::ConvertRelativePathToFull(File), Args))
			{
				OutErrors.Add(TEXT("could not save ") + File);
				return false;
			}
			OutNotes.Add(FString::Printf(TEXT("saved %s"), *FPaths::ConvertRelativePathToFull(File)));
			return true;
		}
	}

	bool GenerateAll(TArray<FString>& OutErrors, TArray<FString>& OutNotes)
	{
#if ENGINE_MAJOR_VERSION != 5 || ENGINE_MINOR_VERSION != 6
		OutErrors.Add(TEXT("regenerate the perf fixtures on UE 5.6 only: a package saved by a later engine does not load in 5.6"));
		return false;
#else
		const TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(PluginName);
		if (!Plugin.IsValid() || !Plugin->IsEnabled() || !FPackageName::MountPointExists(FString(TEXT("/")) + PluginName + TEXT("/")))
		{
			OutErrors.Add(TEXT("the NodeToCodeTestContent plugin is not mounted: build.ps1 junctions Tests/NodeToCodeTestContent into the host's Plugins folder"));
			return false;
		}

		UPackage* EnumPackage = NewAssetPackage(EnumName, OutErrors);
		UPackage* StructPackage = NewAssetPackage(StructName, OutErrors);
		TArray<UPackage*> BlueprintPackages;
		for (const FFixture& Fixture : All())
		{
			BlueprintPackages.Add(NewAssetPackage(Fixture.Name, OutErrors));
		}
		if (OutErrors.Num() > 0)
		{
			return false;
		}
		// The new packages replace the files: delete the old ones first (saving over them fails)
		TArray<UPackage*> AllPackages = BlueprintPackages;
		AllPackages.Append({ EnumPackage, StructPackage });
		for (const UPackage* Package : AllPackages)
		{
			const FString File = FPaths::ConvertRelativePathToFull(
				FPackageName::LongPackageNameToFilename(Package->GetName(), FPackageName::GetAssetPackageExtension()));
			if (IFileManager::Get().FileExists(*File) && !IFileManager::Get().Delete(*File, false, true))
			{
				OutErrors.Add(TEXT("could not delete the old ") + File);
			}
		}
		if (OutErrors.Num() > 0)
		{
			return false;
		}

		UUserDefinedEnum* Enum = nullptr;
		UUserDefinedStruct* Struct = nullptr;
		for (int32 Index = 0; Index < All().Num(); ++Index)
		{
			const FFixture& Fixture = All()[Index];
			TUniquePtr<FTestBlueprint> BP = FTestBlueprint::InPackage(Fixture.Name, BlueprintPackages[Index]);
			if (!Enum)
			{
				// The types every fixture uses, made once beside the first Blueprint's helper
				Enum = BP->AddUserEnum(EnumName, { TEXT("Idle"), TEXT("Walk"), TEXT("Run") }, EnumPackage);
				Struct = Enum ? BP->AddUserStruct(StructName, {
					{ TEXT("Count"), FTestBlueprint::IntPinType() },
					{ TEXT("Mode"), FTestBlueprint::EnumPinType(Enum) },
					{ TEXT("Weight"), FTestBlueprint::DoublePinType() },
					{ TEXT("Ready"), FTestBlueprint::BoolPinType() } }, StructPackage) : nullptr;
				if (!Struct || !Save(Enum, OutErrors, OutNotes) || !Save(Struct, OutErrors, OutNotes))
				{
					OutErrors.Append(BP->Errors());
					return false;
				}
			}
			FBuilder Builder(*BP, Fixture, Struct, Enum);
			const bool bBuilt = Builder.Build();
			TArray<FString> BuildErrors = BP->Errors();
			BuildErrors.Append(Builder.Errors);
			for (const FString& Error : BuildErrors)
			{
				OutErrors.Add(FString::Printf(TEXT("%s: %s"), Fixture.Name, *Error));
			}
			if (!bBuilt || OutErrors.Num() > 0)
			{
				OutErrors.Add(FString::Printf(TEXT("%s: could not be built"), Fixture.Name));
				return false;
			}

			UBlueprint* Blueprint = BP->GetBlueprint();
			FCompilerResultsLog Results;
			Results.bSilentMode = true;
			FKismetEditorUtilities::CompileBlueprint(Blueprint, EBlueprintCompileOptions::SkipGarbageCollection, &Results);
			if (Results.NumErrors > 0 || Blueprint->Status == BS_Error)
			{
				for (const TSharedRef<FTokenizedMessage>& Message : Results.Messages)
				{
					if (Message->GetSeverity() == EMessageSeverity::Error)
					{
						OutErrors.Add(FString::Printf(TEXT("%s: %s"), Fixture.Name, *Message->ToText().ToString()));
					}
				}
				OutErrors.Add(FString::Printf(TEXT("%s: %d compile errors"), Fixture.Name, Results.NumErrors));
				return false;
			}
			OutNotes.Add(FString::Printf(TEXT("%s: %d K2 nodes, %d compile warnings"), Fixture.Name, CountK2Nodes(*Blueprint), Results.NumWarnings));
			TSet<FString> Warnings;
			for (const TSharedRef<FTokenizedMessage>& Message : Results.Messages)
			{
				if (Message->GetSeverity() == EMessageSeverity::Warning && Warnings.Num() < 5)
				{
					bool bAlreadyIn = false;
					Warnings.Add(Message->ToText().ToString(), &bAlreadyIn);
					if (!bAlreadyIn)
					{
						OutNotes.Add(FString::Printf(TEXT("%s: warning: %s"), Fixture.Name, *Message->ToText().ToString()));
					}
				}
			}
			if (!Save(Blueprint, OutErrors, OutNotes))
			{
				return false;
			}
		}
		return true;
#endif
	}
}

#endif // WITH_DEV_AUTOMATION_TESTS
