// Copyright (c) 2025 Nick McClure (Protospatial). All Rights Reserved.

#include "Code Editor/Syntax/N2CPythonSyntaxDefinition.h"

FN2CPythonSyntaxDefinition::FN2CPythonSyntaxDefinition()
{
    // Python Keywords
    Keywords = {
        TEXT("False"), TEXT("None"), TEXT("True"), TEXT("and"), TEXT("as"),
        TEXT("assert"), TEXT("async"), TEXT("await"), TEXT("break"), TEXT("class"),
        TEXT("continue"), TEXT("def"), TEXT("del"), TEXT("elif"), TEXT("else"),
        TEXT("except"), TEXT("finally"), TEXT("for"), TEXT("from"), TEXT("global"),
        TEXT("if"), TEXT("import"), TEXT("in"), TEXT("is"), TEXT("lambda"),
        TEXT("nonlocal"), TEXT("not"), TEXT("or"), TEXT("pass"), TEXT("raise"),
        TEXT("return"), TEXT("try"), TEXT("while"), TEXT("with"), TEXT("yield")
    };

    // Bracket operators
    Parentheses = { TEXT("("), TEXT(")") };
    CurlyBraces = { TEXT("{"), TEXT("}") };
    SquareBrackets = { TEXT("["), TEXT("]") };

    // Python Operators
    Operators = {
        // Comment operators first to ensure proper matching
        TEXT("#"), TEXT("\"\"\""), TEXT("\"\"\""),
        // Standard operators
        TEXT("+"), TEXT("-"), TEXT("*"), TEXT("**"), TEXT("/"), TEXT("//"), TEXT("%"),
        TEXT("@"), TEXT("<<"), TEXT(">>"), TEXT("&"), TEXT("|"), TEXT("^"), TEXT("~"),
        TEXT("<"), TEXT(">"), TEXT("<="), TEXT(">="), TEXT("=="), TEXT("!="),
        TEXT("+="), TEXT("-="), TEXT("*="), TEXT("/="), TEXT("//="), TEXT("%="),
        TEXT("@="), TEXT("&="), TEXT("|="), TEXT("^="), TEXT(">>="), TEXT("<<="),
        TEXT("**="), TEXT("="), TEXT(","), TEXT(":"), TEXT(".")
    };

    // String delimiters
    StringDelimiters = { TEXT('\"'), TEXT('\'') };

    // Comment delimiters
    LineCommentDelimiter = TEXT("#");
    BlockCommentStart = TEXT("\"\"\"");
    BlockCommentEnd = TEXT("\"\"\"");
}

void FN2CPythonSyntaxDefinition::GetCommentDelimiters(FString& OutLineComment, FString& OutBlockCommentStart, FString& OutBlockCommentEnd) const
{
    OutLineComment = LineCommentDelimiter;
    OutBlockCommentStart = BlockCommentStart;
    OutBlockCommentEnd = BlockCommentEnd;
}
