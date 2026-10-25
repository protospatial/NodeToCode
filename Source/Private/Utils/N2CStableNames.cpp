// Copyright (c) 2026 Nick McClure (Protospatial)
// SPDX-License-Identifier: Apache-2.0

#include "Utils/N2CStableNames.h"

#include "EdGraph/EdGraphNode.h"
#include "EdGraph/EdGraphPin.h"
#include "Engine/Blueprint.h"
#include "Engine/BlueprintGeneratedClass.h"
#include "K2Node_CallFunction.h"
#include "K2Node_Variable.h"
#include "Models/N2CJsonContract.h"

namespace N2CStableNames
{
	FString ClassName(const UClass* Class)
	{
		if (!Class)
		{
			return FString();
		}
#if WITH_EDITORONLY_DATA
		if (const UBlueprint* Blueprint = Cast<UBlueprint>(Class->ClassGeneratedBy))
		{
			return Blueprint->GetName();
		}
#endif
		FString Name = Class->GetName();
		if (Cast<UBlueprintGeneratedClass>(Class))
		{
			// A generated class without its Blueprint (a cooked or orphaned class): BP_Enemy_C, SKEL_BP_Enemy_C
			Name.RemoveFromStart(TEXT("SKEL_"), ESearchCase::CaseSensitive);
			Name.RemoveFromEnd(TEXT("_C"), ESearchCase::CaseSensitive);
		}
		return Name;
	}

	FString TypeObjectName(const UObject* Object)
	{
		if (const UClass* Class = Cast<UClass>(Object))
		{
			return ClassName(Class);
		}
		return Object ? Object->GetName() : FString();
	}

	FString SourceText(const FText& Text)
	{
		return Text.BuildSourceString();
	}

	FString NodeTitle(const UEdGraphNode* Node)
	{
		return Node ? SourceText(Node->GetNodeTitle(ENodeTitleType::MenuTitle)) : FString();
	}

	FString PinLabel(const UEdGraphPin* Pin)
	{
		if (!Pin)
		{
			return FString();
		}
		FString Label;
		const UEdGraphNode* Node = Pin->GetOwningNodeUnchecked();
		if (Node && Node->ShouldOverridePinNames())
		{
			Label = SourceText(Node->GetPinNameOverride(*Pin));
		}
		else if (!Pin->PinFriendlyName.IsEmpty())
		{
			Label = SourceText(Pin->PinFriendlyName);
		}
		Label.TrimStartAndEndInline();
		return N2CJsonContract::IsRedundantHint(Label, Pin->PinName.ToString()) ? FString() : Label;
	}

	FString NodeSearchText(const UEdGraphNode* Node)
	{
		if (!Node)
		{
			return FString();
		}
		const FText FullTitle = Node->GetNodeTitle(ENodeTitleType::FullTitle);
		FString MemberName;
		if (const UK2Node_CallFunction* CallNode = Cast<UK2Node_CallFunction>(Node))
		{
			MemberName = CallNode->FunctionReference.GetMemberName().ToString();
		}
		else if (const UK2Node_Variable* VariableNode = Cast<UK2Node_Variable>(Node))
		{
			MemberName = VariableNode->VariableReference.GetMemberName().ToString();
		}
		return FString::Join(TArray<FString>{ FullTitle.ToString(), SourceText(FullTitle), Node->GetClass()->GetName(), MemberName }, TEXT(" "));
	}
}
