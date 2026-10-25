// Copyright (c) 2025 Nick McClure (Protospatial). All Rights Reserved.

#include "N2CMcpFunctionPinUtils.h"
#include "K2Node_FunctionEntry.h"
#include "K2Node_FunctionResult.h"
#include "K2Node_EditablePinBase.h"
#include "Engine/Blueprint.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphPin.h"
#include "EdGraphSchema_K2.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Dom/JsonObject.h"
#include "Utils/N2CLogger.h"
#include "MCP/Utils/N2CMcpBlueprintUtils.h"
#include "ScopedTransaction.h"

UK2Node_FunctionEntry* FN2CMcpFunctionPinUtils::FindFunctionEntryNode(UEdGraph* Graph)
{
	if (!Graph)
	{
		return nullptr;
	}

	// Find the function entry node in the graph
	for (UEdGraphNode* Node : Graph->Nodes)
	{
		if (UK2Node_FunctionEntry* EntryNode = Cast<UK2Node_FunctionEntry>(Node))
		{
			return EntryNode;
		}
	}

	return nullptr;
}

UK2Node_FunctionResult* FN2CMcpFunctionPinUtils::FindFunctionResultNode(UEdGraph* Graph)
{
	if (!Graph)
	{
		return nullptr;
	}

	// Find the function result node in the graph
	for (UEdGraphNode* Node : Graph->Nodes)
	{
		if (UK2Node_FunctionResult* ResultNode = Cast<UK2Node_FunctionResult>(Node))
		{
			return ResultNode;
		}
	}

	return nullptr;
}

TArray<UK2Node_FunctionResult*> FN2CMcpFunctionPinUtils::GetAllResultNodes(UEdGraph* Graph)
{
	TArray<UK2Node_FunctionResult*> ResultNodes;
	if (Graph)
	{
		Graph->GetNodesOfClass(ResultNodes);
	}
	return ResultNodes;
}

void FN2CMcpFunctionPinUtils::FinishSignatureEdit(UBlueprint* Blueprint, const TArray<UK2Node_EditablePinBase*>& EditedNodes)
{
	// As the Blueprint editor's OnParamsChanged: rebuild each edited entry/result node from its UserDefinedPins (no
	// orphan pins for a removed parameter), then HandleParameterDefaultValueChanged regenerates the skeleton class
	// (MarkBlueprintAsStructurallyModified) and only then rebuilds the call sites, delegates and macro instances of the
	// function in every loaded Blueprint (FParamsChangedHelper, which Modifies each call node first), so they get the
	// new signature. Rebuilding call sites before the skeleton (card 0156) left them with the old pins.
	for (UK2Node_EditablePinBase* Node : EditedNodes)
	{
		const bool bOldDisableOrphanSaving = Node->bDisableOrphanPinSaving;
		Node->bDisableOrphanPinSaving = true;
		Node->ReconstructNode();
		Node->bDisableOrphanPinSaving = bOldDisableOrphanSaving;
	}
	if (EditedNodes.Num() > 0)
	{
		GetDefault<UEdGraphSchema_K2>()->HandleParameterDefaultValueChanged(EditedNodes[0]);
	}

	// Package dirty and the compile queued for the end of the tool call, after its transaction closes (card 0151)
	FN2CMcpBlueprintUtils::MarkModified(Blueprint, /*bStructural*/ true);
}

bool FN2CMcpFunctionPinUtils::AddFunctionPin(UBlueprint* Blueprint, UEdGraph* FunctionGraph, bool bReturnValue,
	const FString& DesiredName, const FEdGraphPinType& PinType, const FString& DefaultValue, const FString& Tooltip,
	const FText& TransactionTitle, FN2CFunctionPinEdit& Out)
{
	Out = FN2CFunctionPinEdit();

	// Validate everything before the transaction opens, so a refused edit changes nothing and leaves the package clean
	if (!Blueprint || !FunctionGraph)
	{
		Out.Error = TEXT("Invalid Blueprint or function graph");
		return false;
	}
	if (DesiredName.TrimStartAndEnd().IsEmpty())
	{
		Out.Error = TEXT("The pin name is empty");
		return false;
	}
	UK2Node_FunctionEntry* FunctionEntry = FindFunctionEntryNode(FunctionGraph);
	if (!FunctionEntry)
	{
		Out.Error = TEXT("Not in a function graph. Please focus on a Blueprint function.");
		return false;
	}
	TArray<UK2Node_FunctionResult*> ResultNodes = GetAllResultNodes(FunctionGraph);
	FText CannotCreate;
	if (!bReturnValue)
	{
		if (!FunctionEntry->CanCreateUserDefinedPin(PinType, EGPD_Output, CannotCreate))
		{
			Out.Error = FString::Printf(TEXT("Cannot create pin: %s"), *CannotCreate.ToString());
			return false;
		}
	}
	else if (ResultNodes.Num() > 0)
	{
		for (UK2Node_FunctionResult* ResultNode : ResultNodes)
		{
			if (!ResultNode->CanCreateUserDefinedPin(PinType, EGPD_Input, CannotCreate))
			{
				Out.Error = FString::Printf(TEXT("Cannot create return pin: %s"), *CannotCreate.ToString());
				return false;
			}
		}
	}
	else if (!FunctionEntry->IsEditable() || PinType.PinCategory == UEdGraphSchema_K2::PC_Exec)
	{
		// No result node yet: the one FindOrCreateFunctionResultNode makes is editable and takes no exec pins
		Out.Error = FunctionEntry->IsEditable() ? TEXT("Cannot create return pin: an exec pin cannot be a return value")
			: TEXT("Cannot create return pin: the function cannot be edited");
		return false;
	}

	const FScopedTransaction Transaction(TransactionTitle);
	Blueprint->Modify();
	FunctionGraph->Modify();
	FunctionEntry->Modify(); // the tooltip lives in the entry's metadata, and the editor records the entry for outputs too

	TArray<UK2Node_EditablePinBase*> EditedNodes;
	if (!bReturnValue)
	{
		UEdGraphPin* NewPin = FunctionEntry->CreateUserDefinedPin(FName(*DesiredName), PinType, EGPD_Output, /*bUseUniqueName*/ true);
		if (!NewPin)
		{
			Out.Error = TEXT("Failed to create input pin");
			return false;
		}
		if (!DefaultValue.IsEmpty())
		{
			TSharedPtr<FUserPinInfo>* PinInfo = FunctionEntry->UserDefinedPins.FindByPredicate(
				[NewPin](const TSharedPtr<FUserPinInfo>& Info) { return Info.IsValid() && Info->PinName == NewPin->PinName; });
			if (PinInfo && PinInfo->IsValid())
			{
				FunctionEntry->ModifyUserDefinedPinDefaultValue(*PinInfo, DefaultValue);
			}
		}
		Out.PinName = NewPin->PinName;
		EditedNodes.Add(FunctionEntry);
	}
	else
	{
		if (ResultNodes.Num() == 0)
		{
			// The engine's own result node (FGraphNodeCreator: RF_Transactional, a NodeGuid, editable), placed right of
			// the entry and wired from its exec output when that is free
			UK2Node_FunctionResult* Created = FBlueprintEditorUtils::FindOrCreateFunctionResultNode(FunctionEntry);
			if (!Created)
			{
				Out.Error = TEXT("Failed to find or create function result node");
				return false;
			}
			Out.bCreatedResultNode = true;
			ResultNodes = Created->GetAllResultNodes();
		}

		// One name for every Return node (the editor's OnAddNewOutputClicked): unique against the function's
		// parameters and the first node's pins
		const FName PinName = ResultNodes[0]->CreateUniquePinName(FName(*DesiredName));
		for (UK2Node_FunctionResult* ResultNode : ResultNodes)
		{
			ResultNode->Modify();
			UEdGraphPin* NewPin = ResultNode->CreateUserDefinedPin(PinName, PinType, EGPD_Input, /*bUseUniqueName*/ false);
			if (!NewPin)
			{
				// Nothing reports this for a validated type; undo what this call did rather than leave the nodes apart
				Out.Error = FString::Printf(TEXT("Failed to create return pin '%s' on %s"), *PinName.ToString(), *ResultNode->GetName());
				for (UK2Node_EditablePinBase* Done : EditedNodes)
				{
					Done->RemoveUserDefinedPinByName(PinName);
				}
				return false;
			}
			Out.Pin = Out.Pin ? Out.Pin : NewPin;
			EditedNodes.Add(ResultNode);
		}
		Out.PinName = PinName;
	}

	if (!Tooltip.IsEmpty())
	{
		SetParameterTooltip(FunctionEntry, Out.PinName, Tooltip);
	}

	FinishSignatureEdit(Blueprint, EditedNodes);

	// The pin objects are recreated by ReconstructNode: hand back the live one
	Out.Pin = EditedNodes[0]->FindPin(Out.PinName, bReturnValue ? EGPD_Input : EGPD_Output);
	Out.EditedNodeCount = EditedNodes.Num();
	return true;
}

bool FN2CMcpFunctionPinUtils::RemoveFunctionPin(UBlueprint* Blueprint, UEdGraph* FunctionGraph, bool bReturnValue,
	const FString& PinName, const FText& TransactionTitle, FN2CFunctionPinEdit& Out)
{
	Out = FN2CFunctionPinEdit();

	// Validate before the transaction opens: a misspelled name changes nothing and leaves the package clean
	if (!Blueprint || !FunctionGraph)
	{
		Out.Error = TEXT("Invalid Blueprint or function graph");
		return false;
	}
	UK2Node_FunctionEntry* FunctionEntry = FindFunctionEntryNode(FunctionGraph);
	if (!FunctionEntry)
	{
		Out.Error = TEXT("No function entry node found in the graph");
		return false;
	}
	TArray<UK2Node_EditablePinBase*> TargetNodes;
	if (bReturnValue)
	{
		for (UK2Node_FunctionResult* ResultNode : GetAllResultNodes(FunctionGraph))
		{
			TargetNodes.Add(ResultNode);
		}
		if (TargetNodes.Num() == 0)
		{
			Out.Error = TEXT("No function result node found. This function has no return values.");
			return false;
		}
	}
	else
	{
		TargetNodes.Add(FunctionEntry);
	}
	const UEdGraphPin* Found = FindPinForRemoval(TargetNodes[0], PinName, Out.Error);
	if (!Found)
	{
		return false;
	}
	const FName RemovedName = Found->PinName;

	const FScopedTransaction Transaction(TransactionTitle);
	Blueprint->Modify();
	FunctionGraph->Modify();
	FunctionEntry->Modify();
	for (UK2Node_EditablePinBase* Node : TargetNodes)
	{
		Node->Modify();
		if (UEdGraphPin* Pin = Node->FindPin(RemovedName))
		{
			// Inside the function: wires to the parameter go with it (BreakLinkTo Modifies both ends)
			Pin->BreakAllPinLinks(/*bNotifyNodes*/ true);
		}
		Node->RemoveUserDefinedPinByName(RemovedName);
	}
	SetParameterTooltip(FunctionEntry, RemovedName, FString());

	FinishSignatureEdit(Blueprint, TargetNodes);

	Out.PinName = RemovedName;
	Out.EditedNodeCount = TargetNodes.Num();
	return true;
}

void FN2CMcpFunctionPinUtils::SetParameterTooltip(UK2Node_FunctionEntry* FunctionEntry, FName PinName, const FString& Tooltip)
{
	if (!FunctionEntry || PinName.IsNone())
	{
		return;
	}

	// UEdGraphPin::PinToolTip is rebuilt on every ReconstructNode (FUserPinInfo has no tooltip), so the text goes where
	// the engine reads parameter tooltips from: an "@param <name> <text>" line of the function's tooltip, which
	// UK2Node_CallFunction::GeneratePinTooltipFromFunction shows on the call node's pin and
	// ObjectTools::GetDefaultTooltipForFunction leaves out of the function's own tooltip
	const FString Tag = FString::Printf(TEXT("@param %s"), *PinName.ToString());
	TArray<FString> Lines;
	FunctionEntry->MetaData.ToolTip.ToString().ParseIntoArrayLines(Lines, /*bCullEmpty*/ false);
	const int32 Before = Lines.Num();
	Lines.RemoveAll([&Tag](const FString& Line)
	{
		const FString Trimmed = Line.TrimStart();
		return Trimmed.StartsWith(Tag, ESearchCase::CaseSensitive)
			&& (Trimmed.Len() == Tag.Len() || FChar::IsWhitespace(Trimmed[Tag.Len()]));
	});
	const FString Text = Tooltip.Replace(TEXT("\r"), TEXT(" ")).Replace(TEXT("\n"), TEXT(" ")).TrimStartAndEnd();
	if (Text.IsEmpty() && Lines.Num() == Before)
	{
		return;
	}
	if (!Text.IsEmpty())
	{
		Lines.Add(FString::Printf(TEXT("%s %s"), *Tag, *Text));
	}
	while (Lines.Num() > 0 && Lines.Last().TrimStartAndEnd().IsEmpty())
	{
		Lines.Pop();
	}
	FunctionEntry->MetaData.ToolTip = FText::FromString(FString::Join(Lines, TEXT("\n")));
}

TSharedPtr<FJsonObject> FN2CMcpFunctionPinUtils::BuildPinCreationSuccessResult(
	UEdGraph* FunctionGraph,
	const FString& RequestedName,
	UEdGraphPin* CreatedPin,
	const FEdGraphPinType& PinType,
	bool bIsReturnPin)
{
	TSharedPtr<FJsonObject> Result = MakeShareable(new FJsonObject);
	
	Result->SetBoolField(TEXT("success"), true);
	Result->SetStringField(TEXT("pinName"), RequestedName);
	
	// The actual name might be different if it was made unique
	if (CreatedPin)
	{
		Result->SetStringField(TEXT("actualName"), CreatedPin->PinName.ToString());
		Result->SetStringField(TEXT("displayName"), CreatedPin->GetDisplayName().ToString());
		Result->SetStringField(TEXT("pinId"), CreatedPin->PinId.ToString());
	}
	
	// Add type info
	TSharedPtr<FJsonObject> TypeInfo = MakeShareable(new FJsonObject);
	TypeInfo->SetStringField(TEXT("category"), PinType.PinCategory.ToString());
	
	if (PinType.PinSubCategoryObject.IsValid())
	{
		TypeInfo->SetStringField(TEXT("className"), PinType.PinSubCategoryObject->GetName());
		TypeInfo->SetStringField(TEXT("path"), PinType.PinSubCategoryObject->GetPathName());
	}
	
	// Add a type name for clarity
	FString TypeName = PinType.PinCategory.ToString();
	if (PinType.PinSubCategoryObject.IsValid())
	{
		TypeName = PinType.PinSubCategoryObject->GetName();
	}
	TypeInfo->SetStringField(TEXT("typeName"), TypeName);
	
	Result->SetObjectField(TEXT("typeInfo"), TypeInfo);
	
	// Add function and blueprint info
	if (FunctionGraph)
	{
		Result->SetStringField(TEXT("functionName"), FunctionGraph->GetName());
		
		UBlueprint* Blueprint = FBlueprintEditorUtils::FindBlueprintForGraph(FunctionGraph);
		if (Blueprint)
		{
			Result->SetStringField(TEXT("blueprintName"), Blueprint->GetName());
		}
	}
	
	// Build appropriate message
	FString PinTypeDescription = bIsReturnPin ? TEXT("Return pin") : TEXT("Input pin");
	Result->SetStringField(TEXT("message"), FString::Printf(
		TEXT("%s '%s' added successfully to function '%s'"), 
		*PinTypeDescription,
		*RequestedName, 
		FunctionGraph ? *FunctionGraph->GetName() : TEXT("Unknown")));
	
	return Result;
}

bool FN2CMcpFunctionPinUtils::ValidatePinForRemoval(UK2Node_EditablePinBase* Node, UEdGraphPin* Pin, FString& OutError)
{
	if (!Node || !Pin)
	{
		OutError = TEXT("Invalid node or pin");
		return false;
	}

	UE_LOG(LogNodeToCode, Verbose, TEXT("ValidatePinForRemoval: Validating pin '%s' for removal"), 
		*Pin->PinName.ToString());

	// Don't allow removal of execution pins
	if (Pin->PinType.PinCategory == UEdGraphSchema_K2::PC_Exec)
	{
		OutError = TEXT("Cannot remove execution pin");
		UE_LOG(LogNodeToCode, Verbose, TEXT("ValidatePinForRemoval: Pin is execution pin, cannot remove"));
		return false;
	}

	// Log user-defined pins for debugging
	UE_LOG(LogNodeToCode, Verbose, TEXT("ValidatePinForRemoval: Node has %d user-defined pins"), 
		Node->UserDefinedPins.Num());
	for (const TSharedPtr<FUserPinInfo>& UserPin : Node->UserDefinedPins)
	{
		if (UserPin.IsValid())
		{
			UE_LOG(LogNodeToCode, Verbose, TEXT("  UserDefinedPin: Name='%s', FName='%s'"), 
				*UserPin->PinName.ToString(), 
				*UserPin->PinName.ToString());
		}
	}

	// Check if it's a user-defined pin by searching the UserDefinedPins array
	bool bIsUserDefined = false;
	for (const TSharedPtr<FUserPinInfo>& UserPin : Node->UserDefinedPins)
	{
		if (UserPin.IsValid() && UserPin->PinName == Pin->GetFName())
		{
			bIsUserDefined = true;
			UE_LOG(LogNodeToCode, Verbose, TEXT("ValidatePinForRemoval: Found matching user-defined pin"));
			break;
		}
	}
	
	if (!bIsUserDefined)
	{
		OutError = TEXT("Cannot remove non-user-defined pin. Only pins added by users can be removed.");
		UE_LOG(LogNodeToCode, Verbose, TEXT("ValidatePinForRemoval: Pin '%s' is not user-defined, cannot remove"), 
			*Pin->PinName.ToString());
		return false;
	}

	UE_LOG(LogNodeToCode, Verbose, TEXT("ValidatePinForRemoval: Pin '%s' validated successfully"), 
		*Pin->PinName.ToString());
	return true;
}

UEdGraphPin* FN2CMcpFunctionPinUtils::FindPinForRemoval(UK2Node_EditablePinBase* Node, const FString& PinName, FString& OutError)
{
	if (!Node)
	{
		OutError = TEXT("Invalid node");
		return nullptr;
	}

	// The exact pin name first, then the pin name or the display name, ignoring case
	UEdGraphPin* PinToRemove = Node->FindPin(FName(*PinName));
	if (!PinToRemove)
	{
		for (UEdGraphPin* Pin : Node->Pins)
		{
			if (Pin && (Pin->PinName.ToString().Equals(PinName, ESearchCase::IgnoreCase)
				|| Pin->GetDisplayName().ToString().Equals(PinName, ESearchCase::IgnoreCase)))
			{
				PinToRemove = Pin;
				break;
			}
		}
	}

	if (!PinToRemove)
	{
		TArray<FString> AvailablePins;
		for (UEdGraphPin* Pin : Node->Pins)
		{
			if (Pin)
			{
				const FString DisplayName = Pin->GetDisplayName().ToString();
				AvailablePins.Add(DisplayName != Pin->PinName.ToString()
					? FString::Printf(TEXT("%s (display: %s)"), *Pin->PinName.ToString(), *DisplayName)
					: Pin->PinName.ToString());
			}
		}
		OutError = FString::Printf(TEXT("Pin '%s' not found. Available pins: %s"),
			*PinName, *FString::Join(AvailablePins, TEXT(", ")));
		return nullptr;
	}

	if (!ValidatePinForRemoval(Node, PinToRemove, OutError))
	{
		return nullptr;
	}
	return PinToRemove;
}

TSharedPtr<FJsonObject> FN2CMcpFunctionPinUtils::BuildPinRemovalSuccessResult(
	UEdGraph* FunctionGraph,
	const FString& RemovedPinName,
	bool bIsReturnPin)
{
	TSharedPtr<FJsonObject> Result = MakeShareable(new FJsonObject);
	
	Result->SetBoolField(TEXT("success"), true);
	Result->SetStringField(TEXT("removedPinName"), RemovedPinName);
	
	// Add function and blueprint info
	if (FunctionGraph)
	{
		Result->SetStringField(TEXT("functionName"), FunctionGraph->GetName());
		
		UBlueprint* Blueprint = FBlueprintEditorUtils::FindBlueprintForGraph(FunctionGraph);
		if (Blueprint)
		{
			Result->SetStringField(TEXT("blueprintName"), Blueprint->GetName());
		}
	}
	
	// Build appropriate message
	FString PinTypeDescription = bIsReturnPin ? TEXT("Return pin") : TEXT("Input pin");
	Result->SetStringField(TEXT("message"), FString::Printf(
		TEXT("%s '%s' removed successfully from function '%s'"), 
		*PinTypeDescription,
		*RemovedPinName, 
		FunctionGraph ? *FunctionGraph->GetName() : TEXT("Unknown")));
	
	return Result;
}