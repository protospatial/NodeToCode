// Copyright (c) 2025 Nick McClure (Protospatial). All Rights Reserved.

#include "Code Editor/Syntax/N2CJavaScriptSyntaxDefinition.h"

FN2CJavaScriptSyntaxDefinition::FN2CJavaScriptSyntaxDefinition()
{
    // JavaScript Keywords
    Keywords = {
        TEXT("break"), TEXT("case"), TEXT("catch"), TEXT("class"), TEXT("const"),
        TEXT("continue"), TEXT("debugger"), TEXT("default"), TEXT("delete"), TEXT("do"),
        TEXT("else"), TEXT("export"), TEXT("extends"), TEXT("false"), TEXT("finally"),
        TEXT("for"), TEXT("function"), TEXT("if"), TEXT("import"), TEXT("in"),
        TEXT("instanceof"), TEXT("new"), TEXT("null"), TEXT("return"), TEXT("super"),
        TEXT("switch"), TEXT("this"), TEXT("throw"), TEXT("true"), TEXT("try"),
        TEXT("typeof"), TEXT("var"), TEXT("void"), TEXT("while"), TEXT("with"),
        TEXT("let"), TEXT("static"), TEXT("yield"), TEXT("await"), TEXT("async")
    };

    // Bracket operators
    Parentheses = { TEXT("("), TEXT(")") };
    CurlyBraces = { TEXT("{"), TEXT("}") };
    SquareBrackets = { TEXT("["), TEXT("]") };

    // JavaScript Operators
    Operators = {
        // Comment operators first to ensure proper matching
        TEXT("//"), TEXT("/*"), TEXT("*/"),
        // Standard operators
        TEXT("+"), TEXT("-"), TEXT("*"), TEXT("/"), TEXT("%"), TEXT("++"), TEXT("--"),
        TEXT("="), TEXT("+="), TEXT("-="), TEXT("*="), TEXT("/="), TEXT("%="),
        TEXT("=="), TEXT("==="), TEXT("!="), TEXT("!=="), TEXT(">"), TEXT("<"),
        TEXT(">="), TEXT("<="), TEXT("&&"), TEXT("||"), TEXT("!"), TEXT("&"),
        TEXT("|"), TEXT("^"), TEXT("~"), TEXT("<<"), TEXT(">>"), TEXT(">>>"),
        TEXT("."), TEXT(","), TEXT(";"), TEXT("?"), TEXT(":"), TEXT("=>")
    };

    // String delimiters
    StringDelimiters = { TEXT('\"'), TEXT('\''), TEXT('`') };

    // Comment delimiters
    LineCommentDelimiter = TEXT("//");
    BlockCommentStart = TEXT("/*");
    BlockCommentEnd = TEXT("*/");
}

void FN2CJavaScriptSyntaxDefinition::GetCommentDelimiters(FString& OutLineComment, FString& OutBlockCommentStart, FString& OutBlockCommentEnd) const
{
    OutLineComment = LineCommentDelimiter;
    OutBlockCommentStart = BlockCommentStart;
    OutBlockCommentEnd = BlockCommentEnd;
}
