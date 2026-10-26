// Copyright (c) 2025 Nick McClure (Protospatial). All Rights Reserved.

#include "Models/N2CLogging.h"
#include "Code Editor/Syntax/N2CPseudocodeSyntaxDefinition.h"

FN2CPseudocodeSyntaxDefinition::FN2CPseudocodeSyntaxDefinition()
{
    N2C_LOG_FUNCTION_ENTRY();
    // Minimal set of keywords for pseudocode - we'll keep this empty for simplicity
    Keywords = {};

    // Bracket operators
    Parentheses = { TEXT("("), TEXT(")") };
    CurlyBraces = { TEXT("{"), TEXT("}") };
    SquareBrackets = { TEXT("["), TEXT("]") };

    // Minimal operators
    Operators = {};

    // String delimiters
    StringDelimiters = { TEXT('\"'), TEXT('\'') };

    // Comment delimiters
    LineCommentDelimiter = TEXT("//");
    BlockCommentStart = TEXT("/*");
    BlockCommentEnd = TEXT("*/");
}

void FN2CPseudocodeSyntaxDefinition::GetCommentDelimiters(FString& OutLineComment, FString& OutBlockCommentStart, FString& OutBlockCommentEnd) const
{
    N2C_LOG_FUNCTION_ENTRY();
    OutLineComment = LineCommentDelimiter;
    OutBlockCommentStart = BlockCommentStart;
    OutBlockCommentEnd = BlockCommentEnd;
}
