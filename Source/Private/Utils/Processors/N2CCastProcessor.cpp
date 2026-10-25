// Copyright (c) 2025 Nick McClure (Protospatial). All Rights Reserved.

#include "Utils/Processors/N2CCastProcessor.h"
#include "Utils/N2CStableNames.h"

void FN2CCastProcessor::ExtractNodeProperties(UK2Node* Node, FN2CNodeDefinition& OutNodeDef)
{
    // Handle dynamic cast nodes
    if (UK2Node_DynamicCast* CastNode = Cast<UK2Node_DynamicCast>(Node))
    {
        if (UClass* TargetType = CastNode->TargetType)
        {
            OutNodeDef.MemberName = N2CStableNames::ClassName(TargetType);
            
            // Log cast details
            FString CastInfo = FString::Printf(TEXT("Dynamic Cast: %s, Target Type: %s"),
                *OutNodeDef.Title,
                *OutNodeDef.MemberName);
            FN2CLogger::Get().Log(CastInfo, EN2CLogSeverity::Debug);
        }
        return;
    }
    
    // Handle class dynamic cast nodes
    if (UK2Node_ClassDynamicCast* ClassCastNode = Cast<UK2Node_ClassDynamicCast>(Node))
    {
        if (UClass* TargetType = ClassCastNode->TargetType)
        {
            OutNodeDef.MemberName = N2CStableNames::ClassName(TargetType);
            
            // Log class cast details
            FString CastInfo = FString::Printf(TEXT("Class Dynamic Cast: %s, Target Type: %s"),
                *OutNodeDef.Title,
                *OutNodeDef.MemberName);
            FN2CLogger::Get().Log(CastInfo, EN2CLogSeverity::Debug);
        }
        return;
    }
    
    // Handle byte to enum cast nodes
    if (UK2Node_CastByteToEnum* ByteToEnumNode = Cast<UK2Node_CastByteToEnum>(Node))
    {
        if (UEnum* Enum = ByteToEnumNode->Enum)
        {
            OutNodeDef.MemberName = Enum->GetName();
            
            // Log byte to enum cast details
            FString CastInfo = FString::Printf(TEXT("Byte To Enum Cast: %s, Enum Type: %s"),
                *OutNodeDef.Title,
                *OutNodeDef.MemberName);
            FN2CLogger::Get().Log(CastInfo, EN2CLogSeverity::Debug);
        }
        return;
    }
}
