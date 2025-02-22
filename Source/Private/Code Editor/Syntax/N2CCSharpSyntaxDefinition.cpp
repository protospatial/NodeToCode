// Copyright (c) 2025 Nick McClure (Protospatial). All Rights Reserved.

#include "Code Editor/Syntax/N2CCSharpSyntaxDefinition.h"

FN2CCSharpSyntaxDefinition::FN2CCSharpSyntaxDefinition()
{
    // C# Keywords
    Keywords = {
        TEXT("abstract"), TEXT("as"), TEXT("base"), TEXT("bool"), TEXT("break"), TEXT("byte"),
        TEXT("case"), TEXT("catch"), TEXT("char"), TEXT("checked"), TEXT("class"), TEXT("const"),
        TEXT("continue"), TEXT("decimal"), TEXT("default"), TEXT("delegate"), TEXT("do"), TEXT("double"),
        TEXT("else"), TEXT("enum"), TEXT("event"), TEXT("explicit"), TEXT("extern"), TEXT("false"),
        TEXT("finally"), TEXT("fixed"), TEXT("float"), TEXT("for"), TEXT("foreach"), TEXT("goto"),
        TEXT("if"), TEXT("implicit"), TEXT("in"), TEXT("int"), TEXT("interface"), TEXT("internal"),
        TEXT("is"), TEXT("lock"), TEXT("long"), TEXT("namespace"), TEXT("new"), TEXT("null"),
        TEXT("object"), TEXT("operator"), TEXT("out"), TEXT("override"), TEXT("params"), TEXT("private"),
        TEXT("protected"), TEXT("public"), TEXT("readonly"), TEXT("ref"), TEXT("return"), TEXT("sbyte"),
        TEXT("sealed"), TEXT("short"), TEXT("sizeof"), TEXT("stackalloc"), TEXT("static"), TEXT("string"),
        TEXT("struct"), TEXT("switch"), TEXT("this"), TEXT("throw"), TEXT("true"), TEXT("try"),
        TEXT("typeof"), TEXT("uint"), TEXT("ulong"), TEXT("unchecked"), TEXT("unsafe"), TEXT("ushort"),
        TEXT("using"), TEXT("virtual"), TEXT("void"), TEXT("volatile"), TEXT("while"), TEXT("var"),
        TEXT("async"), TEXT("await"), TEXT("dynamic")
    };

    // C# Operators
    Operators = {
        // Comment operators first to ensure proper matching
        TEXT("//"), TEXT("/*"), TEXT("*/"),
        // Standard operators
        TEXT("+"), TEXT("-"), TEXT("*"), TEXT("/"), TEXT("%"), TEXT("="), TEXT("!"),
        TEXT("<"), TEXT(">"), TEXT("&"), TEXT("|"), TEXT("^"), TEXT("~"), TEXT("?"),
        TEXT("++"), TEXT("--"), TEXT("&&"), TEXT("||"), TEXT("=="), TEXT("!="),
        TEXT("<="), TEXT(">="), TEXT("+="), TEXT("-="), TEXT("*="), TEXT("/="),
        TEXT("%="), TEXT("&="), TEXT("|="), TEXT("^="), TEXT("<<"), TEXT(">>"),
        TEXT("<<="), TEXT(">>="), TEXT("=>"), TEXT("??"), TEXT("?."), TEXT("?["),
        TEXT("?\?=")
    };

    // Brackets
    Parentheses = { TEXT("("), TEXT(")") };
    CurlyBraces = { TEXT("{"), TEXT("}") };
    SquareBrackets = { TEXT("["), TEXT("]") };

    // String delimiters
    StringDelimiters = { TEXT('\"'), TEXT('\'') };

    LineCommentDelimiter = TEXT("//");
    BlockCommentStart = TEXT("/*");
    BlockCommentEnd = TEXT("*/");
}

void FN2CCSharpSyntaxDefinition::GetCommentDelimiters(
    FString& OutLineComment,
    FString& OutBlockCommentStart,
    FString& OutBlockCommentEnd) const
{
    OutLineComment = LineCommentDelimiter;
    OutBlockCommentStart = BlockCommentStart;
    OutBlockCommentEnd = BlockCommentEnd;
}
