// Copyright (c) 2025 Nick McClure (Protospatial). All Rights Reserved.

#include "Utils/Processors/N2CStructProcessor.h"

void FN2CStructProcessor::ExtractNodeProperties(UK2Node* Node, FN2CNodeDefinition& OutNodeDef)
{
    // Handle make struct nodes
    if (UK2Node_MakeStruct* MakeStructNode = Cast<UK2Node_MakeStruct>(Node))
    {
        if (UScriptStruct* Struct = MakeStructNode->StructType)
        {
            OutNodeDef.Name = FString::Printf(TEXT("Make %s"), *Struct->GetName());
            OutNodeDef.MemberName = GetCleanClassName(Struct->GetName());

            // Get the path name and extract just the base name before the period
            const FString FullPath = Struct->GetStructPathName().ToString();
            int32 DotIndex;                                                                                                                                                                                                                
            if (FullPath.FindChar('.', DotIndex))                                                                                                                                                                                          
            {                                                                                                                                                                                                                              
                OutNodeDef.MemberParent = GetCleanClassName(FullPath.Left(DotIndex));                                                                                                                                                                         
            }                                                                                                                                                                                                                              
            else                                                                                                                                                                                                                           
            {                                                                                                                                                                                                                              
                OutNodeDef.MemberParent = FullPath;                                                                                                                                                                                        
            }
            
            // Log make struct details
            FString StructInfo = FString::Printf(TEXT("Make Struct: %s, Type: %s"),
                *OutNodeDef.Name,
                *OutNodeDef.MemberName);
            FN2CLogger::Get().Log(StructInfo, EN2CLogSeverity::Debug);
            return;
        }
    }
    
    // Handle break struct nodes
    if (UK2Node_BreakStruct* BreakStructNode = Cast<UK2Node_BreakStruct>(Node))
    {
        if (UScriptStruct* Struct = BreakStructNode->StructType)
        {
            OutNodeDef.Name = FString::Printf(TEXT("Break %s"), *Struct->GetName());
            OutNodeDef.MemberName = GetCleanClassName(Struct->GetName());

            // Get the path name and extract just the base name before the period
            const FString FullPath = Struct->GetStructPathName().ToString();
            int32 DotIndex;
            if (FullPath.FindChar('.', DotIndex))
            {
                OutNodeDef.MemberParent = GetCleanClassName(FullPath.Left(DotIndex));
            }
            else
            {
                OutNodeDef.MemberParent = FullPath;
            }
            
            // Log break struct details
            FString StructInfo = FString::Printf(TEXT("Break Struct: %s, Type: %s"),
                *OutNodeDef.Name,
                *OutNodeDef.MemberName);
            FN2CLogger::Get().Log(StructInfo, EN2CLogSeverity::Debug);
            return;
        }
    }
    
    // Handle struct operation nodes
    if (UK2Node_StructOperation* StructNode = Cast<UK2Node_StructOperation>(Node))
    {
        if (UScriptStruct* Struct = StructNode->StructType)
        {
            OutNodeDef.MemberParent = GetCleanClassName(Struct->GetName());
            
            // Log struct operation details
            FString StructInfo = FString::Printf(TEXT("Struct Operation: %s, Type: %s"),
                *OutNodeDef.Name,
                *OutNodeDef.MemberParent);
            FN2CLogger::Get().Log(StructInfo, EN2CLogSeverity::Debug);
            return;
        }
    }
    
    // Handle set fields in struct nodes
    if (UK2Node_SetFieldsInStruct* SetFieldsNode = Cast<UK2Node_SetFieldsInStruct>(Node))
    {
        if (UScriptStruct* Struct = SetFieldsNode->StructType)
        {
            OutNodeDef.Name = FString::Printf(TEXT("Set Fields In %s"), *Struct->GetName());
            OutNodeDef.MemberName = GetCleanClassName(Struct->GetName());

            // Get the path name and extract just the base name before the period
            const FString FullPath = Struct->GetStructPathName().ToString();
            int32 DotIndex;
            if (FullPath.FindChar('.', DotIndex))
            {
                OutNodeDef.MemberParent = GetCleanClassName(FullPath.Left(DotIndex));
            }
            else
            {
                OutNodeDef.MemberParent = FullPath;
            }
            
            // Log set fields details
            FString StructInfo = FString::Printf(TEXT("Set Fields In Struct: %s, Type: %s"),
                *OutNodeDef.Name,
                *OutNodeDef.MemberName);
            FN2CLogger::Get().Log(StructInfo, EN2CLogSeverity::Debug);
            return;
        }
    }
}
