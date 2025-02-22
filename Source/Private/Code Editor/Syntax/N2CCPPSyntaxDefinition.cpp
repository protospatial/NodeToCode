// Copyright (c) 2025 Nick McClure (Protospatial). All Rights Reserved.

#include "Code Editor/Syntax/N2CCPPSyntaxDefinition.h"

FN2CCPPSyntaxDefinition::FN2CCPPSyntaxDefinition()
{
    // C++ Keywords
    Keywords = {
        TEXT("alignas"), TEXT("alignof"), TEXT("and"), TEXT("and_eq"), TEXT("asm"),
        TEXT("auto"), TEXT("bitand"), TEXT("bitor"), TEXT("bool"), TEXT("break"),
        TEXT("case"), TEXT("catch"), TEXT("char"), TEXT("char16_t"), TEXT("char32_t"),
        TEXT("class"), TEXT("compl"), TEXT("concept"), TEXT("const"), TEXT("constexpr"),
        TEXT("const_cast"), TEXT("continue"), TEXT("decltype"), TEXT("default"), TEXT("delete"),
        TEXT("do"), TEXT("double"), TEXT("dynamic_cast"), TEXT("else"), TEXT("enum"),
        TEXT("explicit"), TEXT("export"), TEXT("extern"), TEXT("false"), TEXT("float"),
        TEXT("for"), TEXT("friend"), TEXT("goto"), TEXT("if"), TEXT("inline"),
        TEXT("int8"), TEXT("int16"), TEXT("int32"), TEXT("int64"), TEXT("uint8"), TEXT("uint16"), TEXT("uint32"), TEXT("uint64"),
        TEXT("int"), TEXT("long"), TEXT("mutable"), TEXT("namespace"), TEXT("new"),
        TEXT("noexcept"), TEXT("not"), TEXT("not_eq"), TEXT("nullptr"), TEXT("operator"),
        TEXT("or"), TEXT("or_eq"), TEXT("private"), TEXT("protected"), TEXT("public"),
        TEXT("register"), TEXT("reinterpret_cast"), TEXT("requires"), TEXT("return"),
        TEXT("short"), TEXT("signed"), TEXT("sizeof"), TEXT("static"), TEXT("static_assert"),
        TEXT("static_cast"), TEXT("struct"), TEXT("switch"), TEXT("template"), TEXT("this"),
        TEXT("thread_local"), TEXT("throw"), TEXT("true"), TEXT("try"), TEXT("typedef"),
        TEXT("typeid"), TEXT("typename"), TEXT("union"), TEXT("unsigned"), TEXT("using"),
        TEXT("virtual"), TEXT("void"), TEXT("volatile"), TEXT("wchar_t"), TEXT("while"),
        TEXT("xor"), TEXT("xor_eq")
    };

    // Bracket operators
    Parentheses = { TEXT("("), TEXT(")") };
    CurlyBraces = { TEXT("{"), TEXT("}") };
    SquareBrackets = { TEXT("["), TEXT("]") };

    // C++ Operators
    Operators = {
        // Comment operators first to ensure proper matching
        TEXT("//"), TEXT("/*"), TEXT("*/"),
        // Standard operators
        TEXT("::"), TEXT(":"), TEXT("+="), TEXT("++"), TEXT("+"), TEXT("--"), TEXT("-="),
        TEXT("-"), TEXT("*="), TEXT("*"), TEXT("/="), TEXT("/"), TEXT("%="), TEXT("%"),
        TEXT("&="), TEXT("&&"), TEXT("&"), TEXT("|="), TEXT("||"), TEXT("|"), TEXT("^="),
        TEXT("^"), TEXT("!="), TEXT("!"), TEXT("="), TEXT("=="), TEXT("<<="), TEXT("<<"),
        TEXT("<="), TEXT("<"), TEXT(">>="), TEXT(">>"), TEXT(">="), TEXT(">"), TEXT("->"),
        TEXT("."), TEXT("?"), TEXT(","), TEXT(";")
    };

    // String delimiters
    StringDelimiters = { TEXT('\"'), TEXT('\'') };

    // Comment delimiters
    LineCommentDelimiter = TEXT("//");
    BlockCommentStart = TEXT("/*");
    BlockCommentEnd = TEXT("*/");
}

void FN2CCPPSyntaxDefinition::GetCommentDelimiters(FString& OutLineComment, FString& OutBlockCommentStart, FString& OutBlockCommentEnd) const
{
    OutLineComment = LineCommentDelimiter;
    OutBlockCommentStart = BlockCommentStart;
    OutBlockCommentEnd = BlockCommentEnd;
}
