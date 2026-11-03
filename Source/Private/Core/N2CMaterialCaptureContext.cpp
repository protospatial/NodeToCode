// Copyright (c) 2026 Nick McClure (Protospatial)
// SPDX-License-Identifier: Apache-2.0

#include "Core/N2CMaterialCaptureContext.h"

#include "Core/N2CSettings.h"
#include "Engine/EngineTypes.h"
#include "MaterialDomain.h"
#include "Materials/Material.h"
#include "Materials/MaterialAttributeDefinitionMap.h"
#include "Materials/MaterialExpression.h"
#include "Materials/MaterialExpressionComment.h"
#include "Materials/MaterialExpressionFunctionInput.h"
#include "Materials/MaterialExpressionFunctionOutput.h"
#include "Materials/MaterialExpressionMaterialFunctionCall.h"
#include "Materials/MaterialExpressionNamedReroute.h"
#include "Materials/MaterialExpressionReroute.h"
#include "Materials/MaterialFunction.h"
#include "Materials/MaterialFunctionInterface.h"
#include "Models/N2CJsonContract.h"
#include "UObject/Package.h"
#include "UObject/UnrealType.h"

namespace N2CMaterialCapture
{
	/** True for FExpressionInput and the structs derived from it (the material's root inputs) */
	bool IsExpressionInputStruct(const UStruct* Struct)
	{
		for (const UStruct* Each = Struct; Each; Each = Each->GetSuperStruct())
		{
			if (Each->GetFName() == TEXT("ExpressionInput"))
			{
				return true;
			}
		}
		return false;
	}

	/** True for a struct that is, or holds a member that is, an expression input or output (or an array of them) */
	bool HoldsExpressionPins(const UStruct* Struct, int32 Depth = 0)
	{
		if (!Struct || Depth > 3)
		{
			return false;
		}
		const FName Name = Struct->GetFName();
		if (IsExpressionInputStruct(Struct) || Name == TEXT("ExpressionOutput"))
		{
			return true;
		}
		for (TFieldIterator<FProperty> It(Struct); It; ++It)
		{
			const FProperty* Inner = *It;
			if (const FArrayProperty* Array = CastField<FArrayProperty>(Inner))
			{
				Inner = Array->Inner;
			}
			const FStructProperty* StructProperty = CastField<FStructProperty>(Inner);
			if (StructProperty && HoldsExpressionPins(StructProperty->Struct, Depth + 1))
			{
				return true;
			}
		}
		return false;
	}

	/** The struct a property holds (directly or as an array's elements), or null */
	const UScriptStruct* HeldStruct(const FProperty* Property)
	{
		if (const FArrayProperty* Array = CastField<FArrayProperty>(Property))
		{
			Property = Array->Inner;
		}
		const FStructProperty* StructProperty = CastField<FStructProperty>(Property);
		return StructProperty ? StructProperty->Struct : nullptr;
	}

	/**
	 * Every FExpressionInput of the expression by its address, named by its property: "A", an array element "Inputs[2]",
	 * an input inside an array of structs "FunctionInputs[0]"
	 */
	TMap<const FExpressionInput*, FString> InputPropertyNames(UMaterialExpression& Expression)
	{
		TMap<const FExpressionInput*, FString> Names;
		for (TFieldIterator<FProperty> It(Expression.GetClass()); It; ++It)
		{
			const FProperty* Property = *It;
			if (const FStructProperty* StructProperty = CastField<FStructProperty>(Property))
			{
				if (IsExpressionInputStruct(StructProperty->Struct))
				{
					for (int32 Index = 0; Index < StructProperty->ArrayDim; ++Index)
					{
						Names.Add(StructProperty->ContainerPtrToValuePtr<FExpressionInput>(&Expression, Index),
							StructProperty->ArrayDim > 1 ? FString::Printf(TEXT("%s[%d]"), *Property->GetName(), Index) : Property->GetName());
					}
				}
				continue;
			}
			const FArrayProperty* Array = CastField<FArrayProperty>(Property);
			const FStructProperty* Inner = Array ? CastField<FStructProperty>(Array->Inner) : nullptr;
			if (!Inner)
			{
				continue;
			}
			// An array of inputs, or of structs that each hold one input (a function call's FunctionInputs)
			const FStructProperty* Member = nullptr;
			if (!IsExpressionInputStruct(Inner->Struct))
			{
				for (TFieldIterator<FProperty> MemberIt(Inner->Struct); MemberIt; ++MemberIt)
				{
					const FStructProperty* Candidate = CastField<FStructProperty>(*MemberIt);
					if (Candidate && IsExpressionInputStruct(Candidate->Struct))
					{
						Member = Candidate;
						break;
					}
				}
				if (!Member)
				{
					continue;
				}
			}
			FScriptArrayHelper Helper(Array, Array->ContainerPtrToValuePtr<void>(&Expression));
			for (int32 Index = 0; Index < Helper.Num(); ++Index)
			{
				uint8* Element = Helper.GetRawPtr(Index);
				const FExpressionInput* Input = Member ? Member->ContainerPtrToValuePtr<FExpressionInput>(Element)
					: reinterpret_cast<const FExpressionInput*>(Element);
				Names.Add(Input, FString::Printf(TEXT("%s[%d]"), *Property->GetName(), Index));
			}
		}
		return Names;
	}

	/** An enum value's name without its prefix ("MD_Surface" -> "Surface") */
	FString EnumValueName(const UEnum* Enum, int64 Value, const TCHAR* Prefix)
	{
		FString Name = Enum ? Enum->GetNameStringByValue(Value) : FString();
		Name.RemoveFromStart(Prefix, ESearchCase::CaseSensitive);
		return Name;
	}

	/** True when the class is, or derives from, the class named ClassName (for classes NodeToCode does not link) */
	bool IsA(const UObject& Object, const TCHAR* ClassName)
	{
		for (const UClass* Class = Object.GetClass(); Class; Class = Class->GetSuperClass())
		{
			if (Class->GetFName() == ClassName)
			{
				return true;
			}
		}
		return false;
	}

	bool IsReroute(const UMaterialExpression& Expression)
	{
		return Expression.IsA<UMaterialExpressionReroute>() || Expression.IsA<UMaterialExpressionNamedRerouteDeclaration>()
			|| Expression.IsA<UMaterialExpressionNamedRerouteUsage>();
	}

	/** A liveness boundary: what the material or function outputs */
	bool IsOutput(const UMaterialExpression& Expression)
	{
		return Expression.IsA<UMaterialExpressionFunctionOutput>() || IsA(Expression, TEXT("MaterialExpressionCustomOutput"));
	}

	FString PinRef(const FString& NodeID, const FString& PinID)
	{
		return NodeID + N2CJsonContract::PinRefSeparator + PinID;
	}
}

FN2CMaterialCaptureContext::FN2CMaterialCaptureContext(const FN2CCaptureOptions& InOptions)
	: Options(InOptions)
{
	if (Options.Depth == INDEX_NONE)
	{
		const UN2CSettings* Settings = GetDefault<UN2CSettings>();
		Options.Depth = Settings ? Settings->TranslationDepth : 0;
	}
	Capture.Blueprint.Version.Value = N2CJsonContract::Version;
	Capture.Blueprint.Metadata.AssetKind = EN2CAssetKind::Material;
}

void FN2CMaterialCaptureContext::CaptureMaterial(UMaterial& Material)
{
	using namespace N2CMaterialCapture;
	FN2CMetadata& Metadata = Capture.Blueprint.Metadata;
	Metadata.Name = Material.GetName();
	Metadata.AssetClass = Material.GetClass()->GetName();
	Metadata.Material.bSet = true;
	Metadata.Material.Domain = EnumValueName(StaticEnum<EMaterialDomain>(), Material.MaterialDomain, TEXT("MD_"));
	Metadata.Material.BlendMode = EnumValueName(StaticEnum<EBlendMode>(), Material.GetBlendMode(), TEXT("BLEND_"));
	const FMaterialShadingModelField ShadingModels = Material.GetShadingModels();
	for (int32 Model = 0; Model < MSM_NUM; ++Model)
	{
		if (ShadingModels.HasShadingModel(static_cast<EMaterialShadingModel>(Model)))
		{
			Metadata.Material.ShadingModels.Add(EnumValueName(StaticEnum<EMaterialShadingModel>(), Model, TEXT("MSM_")));
		}
	}
	Metadata.Material.bTwoSided = Material.IsTwoSided();

	CaptureGraph(Material.GetName(), EN2CGraphType::Material, Material.GetExpressions(), Material.GetEditorComments(), &Material, 0);
	while (PendingFunctions.Num() > 0)
	{
		const TPair<UMaterialFunction*, int32> Next = PendingFunctions[0];
		PendingFunctions.RemoveAt(0);
		CaptureGraph(Next.Key->GetName(), EN2CGraphType::MaterialFunction, Next.Key->GetExpressions(), Next.Key->GetEditorComments(),
			nullptr, Next.Value);
	}
}

void FN2CMaterialCaptureContext::CaptureFunction(UMaterialFunction& Function)
{
	FN2CMetadata& Metadata = Capture.Blueprint.Metadata;
	Metadata.Name = Function.GetName();
	Metadata.AssetClass = Function.GetClass()->GetName();
	SeenFunctions.Add(&Function);
	PendingFunctions.Add({ &Function, 0 });
	while (PendingFunctions.Num() > 0)
	{
		const TPair<UMaterialFunction*, int32> Next = PendingFunctions[0];
		PendingFunctions.RemoveAt(0);
		CaptureGraph(Next.Key->GetName(), EN2CGraphType::MaterialFunction, Next.Key->GetExpressions(), Next.Key->GetEditorComments(),
			nullptr, Next.Value);
	}
}

FN2CCapture FN2CMaterialCaptureContext::TakeCapture()
{
	for (const UMaterialFunction* Function : DepthLimitedFunctions)
	{
		if (!SeenFunctions.Contains(Function))
		{
			AddWarning(N2CJsonContract::Diagnostic::DepthLimit, TEXT("graphs"),
				FString::Printf(TEXT("Material function %s is used but not captured: it is beyond Max Translation Depth %d"),
					*Function->GetPathName(), Options.Depth));
		}
	}
	return MoveTemp(Capture);
}

void FN2CMaterialCaptureContext::AddWarning(const TCHAR* Code, const FString& Path, const FString& Message)
{
	FN2CDiagnostic& Diagnostic = Capture.Diagnostics.AddDefaulted_GetRef();
	Diagnostic.Severity = EN2CDiagnosticSeverity::Warning;
	Diagnostic.Code = Code;
	Diagnostic.Path = Path;
	Diagnostic.Message = Message;
}

FGuid FN2CMaterialCaptureContext::UniqueGuid(const UMaterialExpression& Expression, int32 Ordinal, const FString& Path)
{
	FGuid Guid = Expression.MaterialExpressionGuid;
	if (!Guid.IsValid() || UsedGuids.Contains(Guid))
	{
		const FGuid Original = Guid;
		uint64 Seed = static_cast<uint64>(Ordinal);
		do
		{
			Guid = FGuid::NewDeterministicGuid(FString::Printf(TEXT("%s#%s"), *Original.ToString(), *Expression.GetClass()->GetName()), Seed++);
		}
		while (UsedGuids.Contains(Guid));
		AddWarning(N2CJsonContract::Diagnostic::DuplicateExpressionGuid, Path, FString::Printf(
			TEXT("%s %s (expression %d) has %s MaterialExpressionGuid; the capture derives %s from it and its position"),
			*Expression.GetClass()->GetName(), *Expression.GetName(), Ordinal,
			Original.IsValid() ? TEXT("the same") : TEXT("no"), *Guid.ToString()));
	}
	UsedGuids.Add(Guid);
	return Guid;
}

UMaterialExpression* FN2CMaterialCaptureContext::TraceSource(const FExpressionInput& Input, int32& OutOutputIndex)
{
	UMaterialExpression* Source = Input.Expression;
	OutOutputIndex = Input.OutputIndex;
	for (int32 Hop = 0; Source && Hop < 64; ++Hop)
	{
		const FExpressionInput* Next = nullptr;
		if (const UMaterialExpressionReroute* Reroute = Cast<UMaterialExpressionReroute>(Source))
		{
			Next = &Reroute->Input;
		}
		else if (const UMaterialExpressionNamedRerouteDeclaration* Declaration = Cast<UMaterialExpressionNamedRerouteDeclaration>(Source))
		{
			Next = &Declaration->Input;
		}
		else if (const UMaterialExpressionNamedRerouteUsage* Usage = Cast<UMaterialExpressionNamedRerouteUsage>(Source))
		{
			Next = Usage->Declaration ? &Usage->Declaration->Input : nullptr;
		}
		else
		{
			return Source;
		}
		if (!Next)
		{
			return nullptr;
		}
		Source = Next->Expression;
		OutOutputIndex = Next->OutputIndex;
	}
	return nullptr;
}

void FN2CMaterialCaptureContext::CaptureGraph(const FString& Name, EN2CGraphType GraphType,
	TConstArrayView<TObjectPtr<UMaterialExpression>> Expressions, TConstArrayView<TObjectPtr<UMaterialExpressionComment>> Comments,
	UMaterial* OutputMaterial, int32 Depth)
{
	using namespace N2CMaterialCapture;
	const int32 GraphIndex = Capture.Blueprint.Graphs.Num();
	FN2CGraph& Graph = Capture.Blueprint.Graphs.AddDefaulted_GetRef();
	Graph.Name = Name;
	Graph.GraphType = GraphType;
	Capture.GraphGuids.Add(FGuid());
	const FString GraphPath = FString::Printf(TEXT("graphs[%d]"), GraphIndex);

	// Pass 1: one node per expression (reroutes are traced, not captured), then the material's output node; each
	// node's pins, inputs first, keep the FExpressionInput they read
	TMap<const UMaterialExpression*, FCapturedExpression> Captured;
	TArray<TArray<const FExpressionInput*>> NodeInputs;
	TArray<const UMaterialExpression*> NodeExpressions;
	for (int32 Ordinal = 0; Ordinal < Expressions.Num(); ++Ordinal)
	{
		UMaterialExpression* Expression = Expressions[Ordinal];
		if (!Expression || IsReroute(*Expression) || Expression->IsA<UMaterialExpressionComment>())
		{
			continue;
		}
		const int32 NodeIndex = Graph.Nodes.Num();
		FN2CNodeDefinition& Node = Graph.Nodes.AddDefaulted_GetRef();
		Node.ID = FString::Printf(TEXT("N%d"), NextNodeNumber++);
		Capture.NodeGuids.Add(Node.ID, UniqueGuid(*Expression, Ordinal, FString::Printf(TEXT("%s.nodes[%d]"), *GraphPath, NodeIndex)));
		DescribeExpression(*Expression, Node, Depth);

		// Inputs, named by their property (a function call's by its function input's name)
		const TMap<const FExpressionInput*, FString> PropertyNames = InputPropertyNames(*Expression);
		const UMaterialExpressionMaterialFunctionCall* Call = Cast<UMaterialExpressionMaterialFunctionCall>(Expression);
		TArray<const FExpressionInput*>& Inputs = NodeInputs.AddDefaulted_GetRef();
		TSet<FString> UsedNames;
		// GetInput, not GetInputsView (deprecated on UE 5.8); a function call overrides both with its function's inputs
		for (int32 Index = 0; const FExpressionInput* Input = Expression->GetInput(Index); ++Index)
		{
			FString PinName;
			if (Call && Call->FunctionInputs.IsValidIndex(Index) && !Call->FunctionInputs[Index].Input.InputName.IsNone())
			{
				PinName = Call->FunctionInputs[Index].Input.InputName.ToString();
			}
			else if (const FString* Found = PropertyNames.Find(Input))
			{
				PinName = *Found;
			}
			else
			{
				PinName = FString::Printf(TEXT("Input%d"), Index);
			}
			if (UsedNames.Contains(PinName))
			{
				PinName = FString::Printf(TEXT("%s_%d"), *PinName, Index);
			}
			UsedNames.Add(PinName);

			FN2CPinDefinition& Pin = Node.InputPins.AddDefaulted_GetRef();
			Pin.ID = NewPinID();
			Pin.Name = PinName;
			Pin.Type = EN2CPinType::Wildcard;
			// An unwired input's value: the expression's Const<Name> property (Add's ConstA, Lerp's ConstAlpha)
			if (!Input->Expression)
			{
				if (const FProperty* Const = Expression->GetClass()->FindPropertyByName(FName(*(TEXT("Const") + PinName))))
				{
					Const->ExportText_InContainer(0, Pin.DefaultValue, Expression, nullptr, nullptr, PPF_None);
				}
			}
			Inputs.Add(Input);
		}

		// Settings, after the inputs: an input's Const<Name> value is its default_value, not a property
		CaptureProperties(*Expression, Node);

		// Outputs, named by their OutputName or "Output"
		FCapturedExpression& Entry = Captured.Add(Expression);
		Entry.NodeIndex = NodeIndex;
		// An unnamed output is named by the channels its mask selects ("RGB", "R", ...), as the editor colours its pin,
		// or "Output"; a repeated name gets the output's index
		const TArray<FExpressionOutput>& Outputs = Expression->GetOutputs();
		TSet<FString> UsedOutputNames;
		for (int32 Index = 0; Index < Outputs.Num(); ++Index)
		{
			const FExpressionOutput& Output = Outputs[Index];
			FString OutputName;
			if (!Output.OutputName.IsNone())
			{
				OutputName = Output.OutputName.ToString();
			}
			else if (Output.Mask)
			{
				OutputName = FString(Output.MaskR ? TEXT("R") : TEXT("")) + (Output.MaskG ? TEXT("G") : TEXT(""))
					+ (Output.MaskB ? TEXT("B") : TEXT("")) + (Output.MaskA ? TEXT("A") : TEXT(""));
			}
			if (OutputName.IsEmpty())
			{
				OutputName = TEXT("Output");
			}
			if (UsedOutputNames.Contains(OutputName))
			{
				OutputName = FString::Printf(TEXT("%s_%d"), *OutputName, Index);
			}
			UsedOutputNames.Add(OutputName);

			FN2CPinDefinition& Pin = Node.OutputPins.AddDefaulted_GetRef();
			Pin.ID = NewPinID();
			Pin.Name = OutputName;
			Pin.Type = EN2CPinType::Wildcard;
			Entry.OutputPinIDs.Add(Pin.ID);
		}
		NodeExpressions.Add(Expression);
	}

	int32 OutputNodeIndex = INDEX_NONE;
	if (OutputMaterial)
	{
		OutputNodeIndex = Graph.Nodes.Num();
		FN2CNodeDefinition& Node = Graph.Nodes.AddDefaulted_GetRef();
		Node.ID = FString::Printf(TEXT("N%d"), NextNodeNumber++);
		Node.NodeType = EN2CNodeType::MaterialOutput;
		Node.NodeClass = OutputMaterial->GetClass()->GetName();
		TArray<const FExpressionInput*>& Inputs = NodeInputs.AddDefaulted_GetRef();
		for (int32 Property = 0; Property < MP_MAX; ++Property)
		{
			const FExpressionInput* Input = OutputMaterial->GetExpressionInputForProperty(static_cast<EMaterialProperty>(Property));
			if (!Input || !Input->Expression)
			{
				continue;
			}
			FN2CPinDefinition& Pin = Node.InputPins.AddDefaulted_GetRef();
			Pin.ID = NewPinID();
			Pin.Name = FMaterialAttributeDefinitionMap::GetAttributeName(static_cast<EMaterialProperty>(Property));
			Pin.Type = EN2CPinType::Wildcard;
			Inputs.Add(Input);
		}
		NodeExpressions.Add(nullptr);
	}

	// Pass 2: each wired input's source, through reroutes
	TArray<TArray<int32>> Sources;
	Sources.SetNum(Graph.Nodes.Num());
	for (int32 NodeIndex = 0; NodeIndex < Graph.Nodes.Num(); ++NodeIndex)
	{
		FN2CNodeDefinition& Node = Graph.Nodes[NodeIndex];
		for (int32 PinIndex = 0; PinIndex < Node.InputPins.Num(); ++PinIndex)
		{
			const FExpressionInput* Input = NodeInputs[NodeIndex][PinIndex];
			if (!Input->Expression)
			{
				continue;
			}
			int32 OutputIndex = 0;
			const UMaterialExpression* Source = TraceSource(*Input, OutputIndex);
			if (!Source)
			{
				continue; // a reroute chain that ends unwired
			}
			const FCapturedExpression* From = Captured.Find(Source);
			const FString PinPath = FString::Printf(TEXT("%s.nodes[%d].input_pins[%d]"), *GraphPath, NodeIndex, PinIndex);
			if (!From || From->OutputPinIDs.Num() == 0)
			{
				AddWarning(N2CJsonContract::Diagnostic::DanglingInput, PinPath, FString::Printf(
					TEXT("Input %s of node %s reads %s, which is not an expression of %s"),
					*Node.InputPins[PinIndex].Name, *Node.ID, *Source->GetName(), *Name));
				continue;
			}
			const int32 FromOutput = From->OutputPinIDs.IsValidIndex(OutputIndex) ? OutputIndex : 0;
			FN2CNodeDefinition& FromNode = Graph.Nodes[From->NodeIndex];
			Node.InputPins[PinIndex].bConnected = true;
			FromNode.OutputPins[FromOutput].bConnected = true;
			Graph.Flows.Data.FindOrAdd(PinRef(FromNode.ID, From->OutputPinIDs[FromOutput])).Targets.Add(
				PinRef(Node.ID, Node.InputPins[PinIndex].ID));
			Sources[NodeIndex].AddUnique(From->NodeIndex);
		}
	}

	// Liveness: what the output node, FunctionOutput and custom outputs read, directly or through others
	TArray<bool> Live;
	Live.Init(false, Graph.Nodes.Num());
	TArray<int32> Stack;
	for (int32 NodeIndex = 0; NodeIndex < Graph.Nodes.Num(); ++NodeIndex)
	{
		if (NodeIndex == OutputNodeIndex || (NodeExpressions[NodeIndex] && IsOutput(*NodeExpressions[NodeIndex])))
		{
			Live[NodeIndex] = true;
			Stack.Add(NodeIndex);
		}
	}
	while (Stack.Num() > 0)
	{
		const int32 NodeIndex = Stack.Pop(EAllowShrinking::No);
		for (const int32 Source : Sources[NodeIndex])
		{
			if (!Live[Source])
			{
				Live[Source] = true;
				Stack.Add(Source);
			}
		}
	}
	for (int32 NodeIndex = 0; NodeIndex < Graph.Nodes.Num(); ++NodeIndex)
	{
		Graph.Nodes[NodeIndex].bUnused = !Live[NodeIndex];
	}

	// Comment boxes, with the nodes whose top-left corner lies inside each
	for (const UMaterialExpressionComment* Comment : Comments)
	{
		if (!Comment)
		{
			continue;
		}
		FN2CCommentBox& Box = Graph.Comments.AddDefaulted_GetRef();
		Box.Text = Comment->Text;
		const int32 Left = Comment->MaterialExpressionEditorX;
		const int32 Top = Comment->MaterialExpressionEditorY;
		for (int32 NodeIndex = 0; NodeIndex < Graph.Nodes.Num(); ++NodeIndex)
		{
			const UMaterialExpression* Expression = NodeExpressions[NodeIndex];
			if (Expression && Expression->MaterialExpressionEditorX >= Left && Expression->MaterialExpressionEditorX <= Left + Comment->SizeX
				&& Expression->MaterialExpressionEditorY >= Top && Expression->MaterialExpressionEditorY <= Top + Comment->SizeY)
			{
				Box.NodeIDs.Add(Graph.Nodes[NodeIndex].ID);
			}
		}
	}
}

void FN2CMaterialCaptureContext::DescribeExpression(UMaterialExpression& Expression, FN2CNodeDefinition& Node, int32 Depth)
{
	Node.NodeType = EN2CNodeType::MaterialExpression;
	Node.NodeClass = Expression.GetClass()->GetName();
	Node.Comment = Expression.Desc;

	if (const UMaterialExpressionMaterialFunctionCall* Call = Cast<UMaterialExpressionMaterialFunctionCall>(&Expression))
	{
		Node.NodeType = EN2CNodeType::MaterialFunctionCall;
		if (UMaterialFunctionInterface* Called = Call->MaterialFunction)
		{
			Node.MemberParent = Called->GetOutermost()->GetName();
			Node.MemberName = Called->GetName();
			// A material function (exact class: layers, blends and function instances are not followed) is captured
			// once, when its call depth is within the limit (decision 0009 item 4)
			UMaterialFunction* Function = Cast<UMaterialFunction>(Called);
			if (Function && Function->GetClass() == UMaterialFunction::StaticClass() && !SeenFunctions.Contains(Function))
			{
				if (Depth + 1 <= Options.Depth)
				{
					SeenFunctions.Add(Function);
					PendingFunctions.Add({ Function, Depth + 1 });
				}
				else
				{
					DepthLimitedFunctions.AddUnique(Function);
				}
			}
		}
	}
	else if (const UMaterialExpressionFunctionInput* FunctionInput = Cast<UMaterialExpressionFunctionInput>(&Expression))
	{
		Node.MemberName = FunctionInput->InputName.ToString();
	}
	else if (const UMaterialExpressionFunctionOutput* FunctionOutput = Cast<UMaterialExpressionFunctionOutput>(&Expression))
	{
		Node.MemberName = FunctionOutput->OutputName.ToString();
	}
	else if (!Expression.GetParameterName().IsNone())
	{
		Node.MemberName = Expression.GetParameterName().ToString();
	}
}

void FN2CMaterialCaptureContext::CaptureProperties(const UMaterialExpression& Expression, FN2CNodeDefinition& Node) const
{
	using namespace N2CMaterialCapture;
	const UObject* Defaults = Expression.GetClass()->GetDefaultObject();
	for (TFieldIterator<FProperty> It(Expression.GetClass()); It; ++It)
	{
		const FProperty* Property = *It;
		const UClass* Owner = Property->GetOwnerClass();
		// The expression class's own settings: not UMaterialExpression's (position, GUID, description, ...)
		if (!Owner || !Owner->IsChildOf(UMaterialExpression::StaticClass()) || Owner == UMaterialExpression::StaticClass())
		{
			continue;
		}
		if (!Property->HasAnyPropertyFlags(CPF_Edit) || Property->HasAnyPropertyFlags(CPF_Transient | CPF_Deprecated | CPF_EditConst))
		{
			continue;
		}
		// Pins (inputs and outputs), GUIDs and what member_parent/member_name already say
		const UScriptStruct* Struct = HeldStruct(Property);
		if ((Struct && (HoldsExpressionPins(Struct) || Struct == TBaseStructure<FGuid>::Get()))
			|| Property->GetFName() == TEXT("ParameterName") || Property->GetFName() == TEXT("MaterialFunction")
			|| (Expression.IsA<UMaterialExpressionFunctionInput>() && Property->GetFName() == TEXT("InputName"))
			|| (Expression.IsA<UMaterialExpressionFunctionOutput>() && Property->GetFName() == TEXT("OutputName")))
		{
			continue;
		}
		if (Defaults && Property->Identical_InContainer(&Expression, Defaults))
		{
			continue;
		}
		const FString PropertyName = Property->GetName();
		if (PropertyName.StartsWith(TEXT("Const")) && Node.InputPins.ContainsByPredicate([&PropertyName](const FN2CPinDefinition& Pin)
		{
			return PropertyName.Equals(TEXT("Const") + Pin.Name, ESearchCase::CaseSensitive);
		}))
		{
			continue;
		}
		FString Value;
		Property->ExportText_InContainer(0, Value, &Expression, nullptr, nullptr, PPF_None);
		Node.Properties.Add(Property->GetName(), Value);
	}
}
