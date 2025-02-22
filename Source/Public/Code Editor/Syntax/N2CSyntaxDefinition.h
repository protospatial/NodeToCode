// Copyright (c) 2025 Nick McClure (Protospatial). All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Code Editor/Models/N2CCodeLanguage.h"

/**
 * Base class for language-specific syntax definitions
 */
class FN2CSyntaxDefinition
{
public:
    virtual ~FN2CSyntaxDefinition() = default;

    /** Get all keywords for this language */
    virtual const TArray<FString>& GetKeywords() const = 0;

    /** Get all operators for this language */
    virtual const TArray<FString>& GetOperators() const = 0;

    /** Get comment delimiters */
    virtual void GetCommentDelimiters(FString& OutLineComment, FString& OutBlockCommentStart, FString& OutBlockCommentEnd) const = 0;

    /** Get string delimiters */
    virtual const TArray<TCHAR>& GetStringDelimiters() const = 0;

    /** Get parentheses operators */
    virtual const TArray<FString>& GetParentheses() const = 0;

    /** Get curly brace operators */
    virtual const TArray<FString>& GetCurlyBraces() const = 0;

    /** Get square bracket operators */
    virtual const TArray<FString>& GetSquareBrackets() const = 0;

    /** Get the language this definition is for */
    virtual EN2CCodeLanguage GetLanguage() const = 0;

protected:
    TArray<FString> Keywords;
    TArray<FString> Operators;
    TArray<TCHAR> StringDelimiters;
    TArray<FString> Parentheses;
    TArray<FString> CurlyBraces;
    TArray<FString> SquareBrackets;
    
    FString LineCommentDelimiter;
    FString BlockCommentStart;
    FString BlockCommentEnd;
};
