// Copyright (c) 2025 Nick McClure (Protospatial). All Rights Reserved.

#include "Code Editor/Syntax/N2CSwiftSyntaxDefinition.h"
#include "Models/N2CLogging.h"

FN2CSwiftSyntaxDefinition::FN2CSwiftSyntaxDefinition()
{
    N2C_LOG_FUNCTION_ENTRY();
    // Swift Keywords
    Keywords = {
        TEXT("as"), TEXT("break"), TEXT("case"), TEXT("catch"), TEXT("class"), TEXT("continue"),
        TEXT("default"), TEXT("defer"), TEXT("deinit"), TEXT("do"), TEXT("else"), TEXT("enum"),
        TEXT("extension"), TEXT("fallthrough"), TEXT("false"), TEXT("fileprivate"), TEXT("for"),
        TEXT("func"), TEXT("guard"), TEXT("if"), TEXT("import"), TEXT("in"), TEXT("init"),
        TEXT("inout"), TEXT("internal"), TEXT("is"), TEXT("let"), TEXT("nil"), TEXT("open"),
        TEXT("operator"), TEXT("private"), TEXT("protocol"), TEXT("public"), TEXT("repeat"),
        TEXT("rethrows"), TEXT("return"), TEXT("self"), TEXT("static"), TEXT("struct"),
        TEXT("super"), TEXT("switch"), TEXT("throw"), TEXT("throws"), TEXT("true"), TEXT("try"),
        TEXT("typealias"), TEXT("var"), TEXT("where"), TEXT("while"), TEXT("Any"), TEXT("String"),
        TEXT("Int"), TEXT("Double"), TEXT("Bool"), TEXT("Array"), TEXT("Dictionary"), TEXT("Set")
    };

    // Swift Operators
    Operators = {
        // Comment operators first to ensure proper matching
        TEXT("//"), TEXT("/*"), TEXT("*/"),
        // Standard operators
        TEXT("+"), TEXT("-"), TEXT("*"), TEXT("/"), TEXT("%"), TEXT("="), TEXT("!"),
        TEXT("<"), TEXT(">"), TEXT("&"), TEXT("|"), TEXT("^"), TEXT("~"), TEXT("?"),
        TEXT("++"), TEXT("--"), TEXT("&&"), TEXT("||"), TEXT("=="), TEXT("!="),
        TEXT("<="), TEXT(">="), TEXT("+="), TEXT("-="), TEXT("*="), TEXT("/="),
        TEXT("%="), TEXT("&="), TEXT("|="), TEXT("^="), TEXT("<<"), TEXT(">>"),
        TEXT("..."), TEXT("..<"), TEXT("->"), TEXT("??")
    };

    // Brackets
    Parentheses = { TEXT("("), TEXT(")") };
    CurlyBraces = { TEXT("{"), TEXT("}") };
    SquareBrackets = { TEXT("["), TEXT("]") };

    // String delimiters
    StringDelimiters = { TEXT('\"') };

    // Comment delimiters
    LineCommentDelimiter = TEXT("//");
    BlockCommentStart = TEXT("/*");
    BlockCommentEnd = TEXT("*/");
}

void FN2CSwiftSyntaxDefinition::GetCommentDelimiters(FString& OutLineComment, FString& OutBlockCommentStart, FString& OutBlockCommentEnd) const
{
    N2C_LOG_FUNCTION_ENTRY();
    OutLineComment = LineCommentDelimiter;
    OutBlockCommentStart = BlockCommentStart;
    OutBlockCommentEnd = BlockCommentEnd;
}
