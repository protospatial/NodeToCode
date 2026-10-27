// Copyright (c) 2026 Nick McClure (Protospatial)
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "N2CSyntaxDefinition.h"

/**
 * JSON, for the translation viewer's JSON tab (the N2C JSON a translation was made from). JSON is not a translation
 * target, so it has no EN2CCodeLanguage value: FN2CSyntaxDefinitionFactory::CreateJsonDefinition makes it and
 * SN2CCodeEditor::SetJsonSyntax shows it. It colours with the JavaScript styles.
 */
class FN2CJsonSyntaxDefinition : public FN2CSyntaxDefinition
{
public:
    FN2CJsonSyntaxDefinition();

    virtual const TArray<FString>& GetKeywords() const override { return Keywords; }
    virtual const TArray<FString>& GetOperators() const override { return Operators; }
    virtual void GetCommentDelimiters(FString& OutLineComment, FString& OutBlockCommentStart, FString& OutBlockCommentEnd) const override;
    virtual const TArray<TCHAR>& GetStringDelimiters() const override { return StringDelimiters; }
    virtual const TArray<FString>& GetParentheses() const override { return Parentheses; }
    virtual const TArray<FString>& GetCurlyBraces() const override { return CurlyBraces; }
    virtual const TArray<FString>& GetSquareBrackets() const override { return SquareBrackets; }

    /** The closest translation language, for callers that need an EN2CCodeLanguage; GetSyntaxName says JSON */
    virtual EN2CCodeLanguage GetLanguage() const override { return EN2CCodeLanguage::JavaScript; }
    virtual FString GetSyntaxName() const override { return TEXT("JSON"); }
};
