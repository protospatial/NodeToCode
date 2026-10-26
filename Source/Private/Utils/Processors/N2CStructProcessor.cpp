// Copyright (c) 2025 Nick McClure (Protospatial). All Rights Reserved.

#include "Utils/Processors/N2CStructProcessor.h"

namespace
{
    /** member_name is the struct's name and member_parent the path of the package that declares it */
    void SetStructMember(const UScriptStruct* Struct, FN2CNodeDefinition& OutNodeDef)
    {
        OutNodeDef.MemberName = Struct->GetName();
        const FString FullPath = Struct->GetStructPathName().ToString();
        int32 DotIndex;
        OutNodeDef.MemberParent = FullPath.FindChar('.', DotIndex) ? FullPath.Left(DotIndex) : FullPath;
    }
}

void FN2CStructProcessor::ExtractNodeProperties(UK2Node* Node, FN2CNodeDefinition& OutNodeDef)
{
    // Every struct node is a struct operation; the most-derived class decides the title (translation-core-27):
    // SetFieldsInStruct derives from MakeStruct, and BreakStruct from StructMemberGet
    const UK2Node_StructOperation* StructNode = Cast<UK2Node_StructOperation>(Node);
    const UScriptStruct* Struct = StructNode ? StructNode->StructType.Get() : nullptr;
    if (!Struct)
    {
        return;
    }
    SetStructMember(Struct, OutNodeDef);

    if (Node->IsA<UK2Node_SetFieldsInStruct>())
    {
        // Keeps the editor's title ("Set members in S_Item"), from the base processor
    }
    else if (Node->IsA<UK2Node_MakeStruct>())
    {
        OutNodeDef.Title = FString::Printf(TEXT("Make %s"), *Struct->GetName());
    }
    else if (Node->IsA<UK2Node_BreakStruct>())
    {
        OutNodeDef.Title = FString::Printf(TEXT("Break %s"), *Struct->GetName());
    }

    FN2CLogger::Get().Log(FString::Printf(TEXT("Struct node %s: %s, struct %s"), *Node->GetClass()->GetName(),
        *OutNodeDef.Title, *OutNodeDef.MemberName), EN2CLogSeverity::Debug);
}
