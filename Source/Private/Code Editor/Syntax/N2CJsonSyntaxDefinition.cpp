// Copyright (c) 2026 Nick McClure (Protospatial)
// SPDX-License-Identifier: Apache-2.0

#include "Code Editor/Syntax/N2CJsonSyntaxDefinition.h"

FN2CJsonSyntaxDefinition::FN2CJsonSyntaxDefinition()
{
    // The three literal names
    Keywords = { TEXT("true"), TEXT("false"), TEXT("null") };

    CurlyBraces = { TEXT("{"), TEXT("}") };
    SquareBrackets = { TEXT("["), TEXT("]") };

    Operators = { TEXT(":"), TEXT(","), TEXT("-") };

    // Only double-quoted strings, single-line, with backslash escapes; no comments
    StringDelimiters = { TEXT('\"') };
}

void FN2CJsonSyntaxDefinition::GetCommentDelimiters(FString& OutLineComment, FString& OutBlockCommentStart, FString& OutBlockCommentEnd) const
{
    OutLineComment.Reset();
    OutBlockCommentStart.Reset();
    OutBlockCommentEnd.Reset();
}
