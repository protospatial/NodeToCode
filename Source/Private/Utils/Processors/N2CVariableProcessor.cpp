// Copyright (c) 2025 Nick McClure (Protospatial). All Rights Reserved.

#include "Utils/Processors/N2CVariableProcessor.h"
#include "Utils/N2CStableNames.h"

void FN2CVariableProcessor::ExtractNodeProperties(UK2Node* Node, FN2CNodeDefinition& OutNodeDef)
{
    UK2Node_Variable* VarNode = Cast<UK2Node_Variable>(Node);
    if (!VarNode)
    {
        return;
    }

    // Get variable reference info
    const FMemberReference& VarRef = VarNode->VariableReference;

    // A node without a variable keeps the base processor's title (the editor's) rather than "None"
    if (!VarNode->GetVarName().IsNone())
    {
        OutNodeDef.Title = VarNode->GetVarNameString();
    }

    // The variable's name as declared: never shortened, so Temperature_C stays Temperature_C (translation-core-28).
    // A node without a variable reference has no member_name: no name is made up (it used to be Var_<graph>_<UObject
    // index>, which changed every session; translation-core-4).
    FName MemberName = VarRef.GetMemberName();
    if (!MemberName.IsNone())
    {
        OutNodeDef.MemberName = MemberName.ToString();
    }

    // member_parent is the class that declares the variable, as for a function call (translation-core-22): Health of
    // a BP_Enemy reference is BP_Enemy's, Target of self is the Blueprint's (or an inherited native class's). The
    // variable's type is on its pins. A local variable has no owning class; a member reference that resolves to no
    // class is "self" when it reads the node's own Blueprint.
    if (!MemberName.IsNone() && !VarRef.IsLocalScope())
    {
        UClass* OwnerClass = nullptr;
        if (const FProperty* Property = VarNode->GetPropertyForVariable())
        {
            OwnerClass = Property->GetOwnerClass();
        }
        if (!OwnerClass)
        {
            OwnerClass = VarRef.GetMemberParentClass(VarNode->GetBlueprintClassFromNode());
        }
        if (OwnerClass)
        {
            OutNodeDef.MemberParent = N2CStableNames::ClassName(OwnerClass);
        }
        else if (VarRef.IsSelfContext())
        {
            OutNodeDef.MemberParent = TEXT("self");
        }
    }

    // Log variable details
    FString VarInfo = FString::Printf(TEXT("Variable: %s, Owner: %s"),
        *OutNodeDef.MemberName,
        *OutNodeDef.MemberParent);
    FN2CLogger::Get().Log(VarInfo, EN2CLogSeverity::Debug);
}
