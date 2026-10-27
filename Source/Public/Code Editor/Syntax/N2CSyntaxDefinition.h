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

    /** The syntax's name: the language id (Cpp, Python, ...), or JSON for the JSON definition */
    virtual FString GetSyntaxName() const;

    /** Python and Swift: """ (and in Python ''') opens a string that runs to the same triple quote, across lines */
    bool SupportsTripleQuotedStrings() const { return bTripleQuotedStrings; }

    /** True for a delimiter whose string may span lines (the JavaScript template literal's backtick) */
    bool IsMultiLineStringDelimiter(TCHAR Delimiter) const { return MultiLineStringDelimiters.Contains(Delimiter); }

    /** C++ raw strings: R"delim( ... )delim", with u8, u, U and L prefixes; no escapes, may span lines */
    bool SupportsRawStrings() const { return bRawStrings; }

    /** C# verbatim strings: @"..." (and $@, @$); a doubled quote is the only escape, may span lines */
    bool SupportsVerbatimStrings() const { return bVerbatimStrings; }

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

    TArray<TCHAR> MultiLineStringDelimiters;
    bool bTripleQuotedStrings = false;
    bool bRawStrings = false;
    bool bVerbatimStrings = false;
};
