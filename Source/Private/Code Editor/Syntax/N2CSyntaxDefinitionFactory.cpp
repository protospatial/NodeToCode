// Copyright (c) 2025 Nick McClure (Protospatial). All Rights Reserved.

#include "Code Editor/Syntax/N2CSyntaxDefinitionFactory.h"
#include "Code Editor/Syntax/N2CCPPSyntaxDefinition.h"
#include "Code Editor/Syntax/N2CPythonSyntaxDefinition.h"
#include "Code Editor/Syntax/N2CJavaScriptSyntaxDefinition.h"
#include "Code Editor/Syntax/N2CCSharpSyntaxDefinition.h"
#include "Code Editor/Syntax/N2CSwiftSyntaxDefinition.h"
#include "Code Editor/Syntax/N2CPseudocodeSyntaxDefinition.h"

FN2CSyntaxDefinitionFactory& FN2CSyntaxDefinitionFactory::Get()
{
    static FN2CSyntaxDefinitionFactory Instance;
    return Instance;
}

TSharedPtr<FN2CSyntaxDefinition> FN2CSyntaxDefinitionFactory::CreateDefinition(EN2CCodeLanguage Language)
{
    switch (Language)
    {
        case EN2CCodeLanguage::Cpp:
            return MakeShared<FN2CCPPSyntaxDefinition>();
        case EN2CCodeLanguage::Python:
            return MakeShared<FN2CPythonSyntaxDefinition>();
        case EN2CCodeLanguage::JavaScript:
            return MakeShared<FN2CJavaScriptSyntaxDefinition>();
        case EN2CCodeLanguage::CSharp:
            return MakeShared<FN2CCSharpSyntaxDefinition>();
        case EN2CCodeLanguage::Swift:
            return MakeShared<FN2CSwiftSyntaxDefinition>();
        case EN2CCodeLanguage::Pseudocode:
            return MakeShared<FN2CPseudocodeSyntaxDefinition>();
        default:
            return nullptr;
    }
}
