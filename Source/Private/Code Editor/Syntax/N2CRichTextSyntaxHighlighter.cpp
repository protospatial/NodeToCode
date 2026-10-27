// Copyright (c) 2025 Nick McClure (Protospatial). All Rights Reserved.

#include "Code Editor/Syntax/N2CRichTextSyntaxHighlighter.h"
#include "Code Editor/Syntax/N2CSyntaxDefinitionFactory.h"
#include "Code Editor/Syntax/N2CWhiteSpaceRun.h"
#include "Framework/Text/SlateTextRun.h"
#include "Algo/StableSort.h"

TSharedRef<FN2CRichTextSyntaxHighlighter> FN2CRichTextSyntaxHighlighter::Create(EN2CCodeLanguage Language, const FName& ThemeName, const FTextBlockStyle& BaseStyle)
{
    // Get the syntax definition for this language
    TSharedPtr<FN2CSyntaxDefinition> SyntaxDef = FN2CSyntaxDefinitionFactory::Get().CreateDefinition(Language);
    check(SyntaxDef.IsValid());

    // Get the language name for style lookup
    FName LanguageId;
    switch (Language)
    {
        case EN2CCodeLanguage::Cpp:
            LanguageId = TEXT("CPP");
            break;
        case EN2CCodeLanguage::Python:
            LanguageId = TEXT("Python");
            break;
        case EN2CCodeLanguage::JavaScript:
            LanguageId = TEXT("JavaScript");
            break;
        case EN2CCodeLanguage::CSharp:
            LanguageId = TEXT("CSharp");
            break;
        case EN2CCodeLanguage::Swift:
            LanguageId = TEXT("Swift");
            break;
        case EN2CCodeLanguage::Pseudocode:
            LanguageId = TEXT("Pseudocode");
            break;
        default:
            checkf(false, TEXT("Unsupported language type"));
            break;
    }

    return CreateForDefinition(SyntaxDef.ToSharedRef(), LanguageId, ThemeName, BaseStyle);
}

TSharedRef<FN2CRichTextSyntaxHighlighter> FN2CRichTextSyntaxHighlighter::CreateJson(const FName& ThemeName, const FTextBlockStyle& BaseStyle)
{
    // JSON is JavaScript's object notation: its strings, numbers, literals and brackets take the JavaScript colours
    return CreateForDefinition(FN2CSyntaxDefinitionFactory::Get().CreateJsonDefinition().ToSharedRef(), TEXT("JavaScript"), ThemeName, BaseStyle);
}

TSharedRef<FN2CRichTextSyntaxHighlighter> FN2CRichTextSyntaxHighlighter::CreateForDefinition(TSharedRef<FN2CSyntaxDefinition> SyntaxDef,
    const FName& StyleLanguageId, const FName& ThemeName, const FTextBlockStyle& BaseStyle)
{
    // Tokenizer rules are operators only. FSyntaxTokenizer compares each rule literally (Strncmp) and takes the first
    // that matches, so longer operators go first or "->" would arrive as "-" and ">". Keywords need no rule: the word
    // break iterator already hands them over as whole words
    TArray<FString> Operators = SyntaxDef->GetOperators();
    Algo::StableSort(Operators, [](const FString& A, const FString& B) { return A.Len() > B.Len(); });

    TArray<FSyntaxTokenizer::FRule> TokenizerRules;
    TokenizerRules.Reserve(Operators.Num());
    for (const FString& Operator : Operators)
    {
        if (!Operator.IsEmpty())
        {
            TokenizerRules.Emplace(FSyntaxTokenizer::FRule(Operator));
        }
    }

    // Create the syntax text styles
    FSyntaxTextStyle SyntaxStyles(StyleLanguageId, ThemeName);

    // Apply the base font to all styles
    SyntaxStyles.NormalTextStyle.SetFont(BaseStyle.Font);
    SyntaxStyles.OperatorTextStyle.SetFont(BaseStyle.Font);
    SyntaxStyles.KeywordTextStyle.SetFont(BaseStyle.Font);
    SyntaxStyles.StringTextStyle.SetFont(BaseStyle.Font);
    SyntaxStyles.NumberTextStyle.SetFont(BaseStyle.Font);
    SyntaxStyles.CommentTextStyle.SetFont(BaseStyle.Font);
    SyntaxStyles.PreprocessorTextStyle.SetFont(BaseStyle.Font);
    SyntaxStyles.ParenthesesTextStyle.SetFont(BaseStyle.Font);
    SyntaxStyles.CurlyBracesTextStyle.SetFont(BaseStyle.Font);
    SyntaxStyles.SquareBracketsTextStyle.SetFont(BaseStyle.Font);

    // Create the highlighter
    return MakeShareable(new FN2CRichTextSyntaxHighlighter(
        FSyntaxTokenizer::Create(TokenizerRules),
        SyntaxStyles,
        SyntaxDef
    ));
}

FN2CRichTextSyntaxHighlighter::~FN2CRichTextSyntaxHighlighter()
{
}

const FTextBlockStyle& FN2CRichTextSyntaxHighlighter::GetStyle(ETokenKind Kind) const
{
    switch (Kind)
    {
        case ETokenKind::Operator:       return SyntaxTextStyle.OperatorTextStyle;
        case ETokenKind::Keyword:        return SyntaxTextStyle.KeywordTextStyle;
        case ETokenKind::String:         return SyntaxTextStyle.StringTextStyle;
        case ETokenKind::Number:         return SyntaxTextStyle.NumberTextStyle;
        case ETokenKind::Comment:        return SyntaxTextStyle.CommentTextStyle;
        case ETokenKind::Preprocessor:   return SyntaxTextStyle.PreprocessorTextStyle;
        case ETokenKind::Parentheses:    return SyntaxTextStyle.ParenthesesTextStyle;
        case ETokenKind::CurlyBraces:    return SyntaxTextStyle.CurlyBracesTextStyle;
        case ETokenKind::SquareBrackets: return SyntaxTextStyle.SquareBracketsTextStyle;
        case ETokenKind::Normal:
        default:                         return SyntaxTextStyle.NormalTextStyle;
    }
}

const TCHAR* FN2CRichTextSyntaxHighlighter::GetRunName(ETokenKind Kind)
{
    switch (Kind)
    {
        case ETokenKind::Operator:       return TEXT("Operator");
        case ETokenKind::Keyword:        return TEXT("Keyword");
        case ETokenKind::String:         return TEXT("String");
        case ETokenKind::Number:         return TEXT("Number");
        case ETokenKind::Comment:        return TEXT("Comment");
        case ETokenKind::Preprocessor:   return TEXT("Preprocessor");
        case ETokenKind::Parentheses:    return TEXT("Parentheses");
        case ETokenKind::CurlyBraces:    return TEXT("CurlyBraces");
        case ETokenKind::SquareBrackets: return TEXT("SquareBrackets");
        case ETokenKind::Normal:
        default:                         return TEXT("N2CCodeEditor.Normal");
    }
}

bool FN2CRichTextSyntaxHighlighter::IsNumeric(FStringView Text)
{
    // Handle hexadecimal numbers
    if (Text.StartsWith(TEXT("0x")) || Text.StartsWith(TEXT("0X")))
    {
        for (int32 i = 2; i < Text.Len(); ++i)
        {
            if (!FChar::IsHexDigit(Text[i]))
            {
                return false;
            }
        }
        return Text.Len() > 2; // Must have at least one digit after 0x
    }

    // Handle float suffixes
    FStringView NumericPart = Text;
    if (Text.EndsWith(TEXT('f')) || Text.EndsWith(TEXT('F')))
    {
        NumericPart = Text.LeftChop(1);
    }

    bool bHasDecimalPoint = false;
    bool bHasDigits = false;

    // Handle negative numbers
    const int32 StartIdx = NumericPart.StartsWith(TEXT('-')) ? 1 : 0;

    for (int32 i = StartIdx; i < NumericPart.Len(); ++i)
    {
        const TCHAR Ch = NumericPart[i];
        if (Ch == TEXT('.'))
        {
            if (bHasDecimalPoint) // Multiple decimal points = invalid
            {
                return false;
            }
            bHasDecimalPoint = true;
        }
        else if (!FChar::IsDigit(Ch))
        {
            return false;
        }
        else
        {
            bHasDigits = true;
        }
    }

    return bHasDigits; // Must have at least one digit
}

FN2CRichTextSyntaxHighlighter::ETokenKind FN2CRichTextSyntaxHighlighter::ClassifyWord(FStringView Word, bool bPreprocessorLine) const
{
    if (const ETokenKind* Kind = WordKinds.Find(Word))
    {
        return *Kind;
    }
    if (IsNumeric(Word))
    {
        return ETokenKind::Number;
    }
    // Preprocessor directives (only for C++): the # word, and every other word on a line that starts with #
    if (bHasPreprocessor && (Word.StartsWith(TEXT('#')) || bPreprocessorLine))
    {
        return ETokenKind::Preprocessor;
    }
    return ETokenKind::Normal;
}

void FN2CRichTextSyntaxHighlighter::ParseTokens(const FString& SourceString, FTextLayout& TargetTextLayout, TArray<ISyntaxTokenizer::FTokenizedLine> TokenizedLines)
{
    // The marshaller tokenizes the whole document and calls this once, so string and block comment state carries
    // from line to line here. A string ends at its own closing delimiter (not any delimiter), skips escaped ones, and
    // ends at the end of its line unless it is a form that may span lines (Python and Swift triple quotes, JavaScript
    // template literals, C++ raw strings, C# verbatim strings): an unclosed quote colours one line, not the rest of
    // the file. Strings and comments are found character by character; the tokenizer's words classify the rest.
    enum class EParseState : uint8
    {
        None,
        InString,
        InBlockComment
    };

    struct FSpan
    {
        int32 Begin;
        int32 End;
        ETokenKind Kind;
    };

    const FN2CSyntaxDefinition& Definition = *SyntaxDefinition;
    const TArray<TCHAR>& StringDelimiters = Definition.GetStringDelimiters();
    const TCHAR* const Src = *SourceString;

    // The string being read: its closing delimiter, how that may be escaped, and whether it may span lines
    FString StringClose;
    EStringEscape StringEscape = EStringEscape::Backslash;
    bool bStringSpansLines = false;

    auto MatchesAt = [Src](int32 Index, int32 LineEnd, const FString& Text)
    {
        return Text.Len() > 0 && Index + Text.Len() <= LineEnd && FCString::Strncmp(Src + Index, *Text, Text.Len()) == 0;
    };

    auto IsIdentifierChar = [](TCHAR Char)
    {
        return FChar::IsAlnum(Char) || Char == TEXT('_');
    };

    // A string opening at Index: sets the string state and returns the opening's length, or 0 when none opens here
    auto OpenStringAt = [&](int32 Index, int32 LineBegin, int32 LineEnd) -> int32
    {
        const TCHAR Char = Src[Index];
        const TCHAR Next = Index + 1 < LineEnd ? Src[Index + 1] : TEXT('\0');
        const TCHAR After = Index + 2 < LineEnd ? Src[Index + 2] : TEXT('\0');

        // C# verbatim strings: @"...", $@"..." and @$"..."
        if (Definition.SupportsVerbatimStrings())
        {
            int32 OpenLen = 0;
            if (Char == TEXT('@') && Next == TEXT('"'))
            {
                OpenLen = 2;
            }
            else if ((Char == TEXT('$') && Next == TEXT('@') && After == TEXT('"')) || (Char == TEXT('@') && Next == TEXT('$') && After == TEXT('"')))
            {
                OpenLen = 3;
            }
            if (OpenLen > 0)
            {
                StringClose = TEXT("\"");
                StringEscape = EStringEscape::DoubledQuote;
                bStringSpansLines = true;
                return OpenLen;
            }
        }

        if (!StringDelimiters.Contains(Char))
        {
            return 0;
        }

        // Triple quotes: """ (Python and Swift) and ''' (Python)
        if (Definition.SupportsTripleQuotedStrings() && Next == Char && After == Char)
        {
            StringClose = FString::ChrN(3, Char);
            StringEscape = EStringEscape::Backslash;
            bStringSpansLines = true;
            return 3;
        }

        // C++ raw strings: R"delim( ... )delim", the R optionally after u8, u, U or L, and not ending a longer name
        if (Definition.SupportsRawStrings() && Char == TEXT('"') && Index - 1 >= LineBegin && Src[Index - 1] == TEXT('R'))
        {
            int32 PrefixStart = Index - 1;
            if (PrefixStart - 2 >= LineBegin && Src[PrefixStart - 2] == TEXT('u') && Src[PrefixStart - 1] == TEXT('8'))
            {
                PrefixStart -= 2;
            }
            else if (PrefixStart - 1 >= LineBegin && (Src[PrefixStart - 1] == TEXT('u') || Src[PrefixStart - 1] == TEXT('U') || Src[PrefixStart - 1] == TEXT('L')))
            {
                PrefixStart -= 1;
            }
            if (PrefixStart == LineBegin || !IsIdentifierChar(Src[PrefixStart - 1]))
            {
                // The delimiter: up to 16 characters before '(', none of them space, backslash or parentheses
                for (int32 Paren = Index + 1; Paren < LineEnd && Paren <= Index + 17; ++Paren)
                {
                    const TCHAR DelimChar = Src[Paren];
                    if (DelimChar == TEXT('('))
                    {
                        StringClose = TEXT(")") + FString::ConstructFromPtrSize(Src + Index + 1, Paren - Index - 1) + TEXT("\"");
                        StringEscape = EStringEscape::None;
                        bStringSpansLines = true;
                        return Paren - Index + 1;
                    }
                    if (DelimChar == TEXT(')') || DelimChar == TEXT('\\') || FChar::IsWhitespace(DelimChar))
                    {
                        break;
                    }
                }
            }
        }

        StringClose = FString::ChrN(1, Char);
        StringEscape = EStringEscape::Backslash;
        bStringSpansLines = Definition.IsMultiLineStringDelimiter(Char);
        return 1;
    };

    // Where the open string ends on this line (just past its closing delimiter), or LineEnd; bOutClosed says which
    auto FindStringEnd = [&](int32 Index, int32 LineEnd, bool& bOutClosed) -> int32
    {
        bOutClosed = false;
        while (Index < LineEnd)
        {
            const TCHAR Char = Src[Index];
            if (StringEscape == EStringEscape::Backslash && Char == TEXT('\\'))
            {
                // Skips the escaped character: of a run of backslashes, an odd count escapes what follows
                Index += 2;
                continue;
            }
            if (StringEscape == EStringEscape::DoubledQuote && Char == TEXT('"') && Index + 1 < LineEnd && Src[Index + 1] == TEXT('"'))
            {
                Index += 2;
                continue;
            }
            if (MatchesAt(Index, LineEnd, StringClose))
            {
                bOutClosed = true;
                return Index + StringClose.Len();
            }
            ++Index;
        }
        return LineEnd;
    };

    TArray<FTextLayout::FNewLineData> LinesToAdd;
    LinesToAdd.Reserve(TokenizedLines.Num());

    TArray<FSpan, TInlineAllocator<64>> Spans;
    EParseState ParseState = EParseState::None;
    for (const ISyntaxTokenizer::FTokenizedLine& TokenizedLine : TokenizedLines)
    {
        const int32 LineBegin = TokenizedLine.Range.BeginIndex;
        const int32 LineEnd = TokenizedLine.Range.EndIndex;
        const int32 LineLen = LineEnd - LineBegin;

        TSharedRef<FString> ModelString = MakeShared<FString>(FString::ConstructFromPtrSize(Src + LineBegin, LineLen));
        TArray<TSharedRef<IRun>> Runs;

        if (LineLen == 0)
        {
            Runs.Add(FN2CWhiteSpaceRun::Create(FRunInfo(TEXT("WhiteSpace")), ModelString, SyntaxTextStyle.NormalTextStyle, FTextRange(0, 0), TabSize));
            if (ParseState == EParseState::InString && !bStringSpansLines)
            {
                ParseState = EParseState::None;
            }
            LinesToAdd.Emplace(MoveTemp(ModelString), MoveTemp(Runs));
            continue;
        }

        if (LineLen > MaxHighlightedLineLength)
        {
            // Plain: one run, and the state from the lines before carries past it unchanged
            Runs.Add(FSlateTextRun::Create(FRunInfo(GetRunName(ETokenKind::Normal)), ModelString, SyntaxTextStyle.NormalTextStyle, FTextRange(0, LineLen)));
            LinesToAdd.Emplace(MoveTemp(ModelString), MoveTemp(Runs));
            continue;
        }

        // Pass 1: split the line into spans of one kind each
        Spans.Reset();
        const bool bPreprocessorLine = bHasPreprocessor && Src[LineBegin] == TEXT('#');
        const TArray<ISyntaxTokenizer::FToken>& Tokens = TokenizedLine.Tokens;
        int32 TokenIndex = 0;
        int32 Pos = LineBegin;
        while (Pos < LineEnd)
        {
            if (ParseState == EParseState::InString)
            {
                bool bClosed = false;
                const int32 End = FindStringEnd(Pos, LineEnd, bClosed);
                Spans.Add({Pos, End, ETokenKind::String});
                Pos = End;
                if (bClosed)
                {
                    ParseState = EParseState::None;
                }
                continue;
            }

            if (ParseState == EParseState::InBlockComment)
            {
                int32 End = LineEnd;
                for (int32 Index = Pos; Index < LineEnd; ++Index)
                {
                    if (MatchesAt(Index, LineEnd, BlockCommentEnd))
                    {
                        End = Index + BlockCommentEnd.Len();
                        ParseState = EParseState::None;
                        break;
                    }
                }
                Spans.Add({Pos, End, ETokenKind::Comment});
                Pos = End;
                continue;
            }

            if (MatchesAt(Pos, LineEnd, LineComment))
            {
                Spans.Add({Pos, LineEnd, ETokenKind::Comment});
                Pos = LineEnd;
                continue;
            }
            if (MatchesAt(Pos, LineEnd, BlockCommentStart))
            {
                Spans.Add({Pos, Pos + BlockCommentStart.Len(), ETokenKind::Comment});
                Pos += BlockCommentStart.Len();
                ParseState = EParseState::InBlockComment;
                continue;
            }
            if (const int32 OpenLen = OpenStringAt(Pos, LineBegin, LineEnd))
            {
                // A C++ raw string's prefix word (R, u8R, LR, ...) belongs to the string
                if (Definition.SupportsRawStrings() && OpenLen > 1 && Spans.Num() > 0 && Spans.Last().End == Pos && Src[Pos - 1] == TEXT('R'))
                {
                    Spans.Last().Kind = ETokenKind::String;
                }
                Spans.Add({Pos, Pos + OpenLen, ETokenKind::String});
                Pos += OpenLen;
                ParseState = EParseState::InString;
                continue;
            }

            // A word: the rest of the tokenizer's token, cut where a string or comment starts inside it. The word
            // break iterator joins an apostrophe between letters or digits (1'000, don't), so a quote inside a token
            // starts a string only after punctuation or a short prefix (L'x', u8'x', f"x" arrive as one token)
            while (TokenIndex < Tokens.Num() && Tokens[TokenIndex].Range.EndIndex <= Pos)
            {
                ++TokenIndex;
            }
            const int32 TokenEnd = TokenIndex < Tokens.Num() ? FMath::Min(Tokens[TokenIndex].Range.EndIndex, LineEnd) : LineEnd;
            int32 WordEnd = TokenEnd;
            for (int32 Index = Pos + 1; Index < TokenEnd; ++Index)
            {
                const bool bQuote = StringDelimiters.Contains(Src[Index])
                    && (!IsIdentifierChar(Src[Index - 1]) || (Index - Pos <= 2 && FChar::IsAlpha(Src[Pos])));
                if (bQuote || MatchesAt(Index, LineEnd, LineComment) || MatchesAt(Index, LineEnd, BlockCommentStart))
                {
                    WordEnd = Index;
                    break;
                }
            }
            Spans.Add({Pos, WordEnd, ClassifyWord(FStringView(Src + Pos, WordEnd - Pos), bPreprocessorLine)});
            Pos = WordEnd;
        }

        // Only multi-line string forms and block comments carry to the next line
        if (ParseState == EParseState::InString && !bStringSpansLines)
        {
            ParseState = EParseState::None;
        }

        // Pass 2: one run per piece of a token inside a span, so whitespace keeps its tab-aware run
        int32 SpanIndex = 0;
        for (const ISyntaxTokenizer::FToken& Token : Tokens)
        {
            int32 PieceBegin = FMath::Max(Token.Range.BeginIndex, LineBegin);
            const int32 TokenEnd = FMath::Min(Token.Range.EndIndex, LineEnd);
            while (PieceBegin < TokenEnd)
            {
                while (SpanIndex < Spans.Num() - 1 && Spans[SpanIndex].End <= PieceBegin)
                {
                    ++SpanIndex;
                }
                const FSpan& Span = Spans[SpanIndex];
                const int32 PieceEnd = FMath::Max(PieceBegin + 1, FMath::Min(TokenEnd, Span.End));
                const FTextRange ModelRange(PieceBegin - LineBegin, PieceEnd - LineBegin);

                bool bIsWhitespace = true;
                for (int32 Index = PieceBegin; Index < PieceEnd && bIsWhitespace; ++Index)
                {
                    bIsWhitespace = FChar::IsWhitespace(Src[Index]);
                }

                if (bIsWhitespace)
                {
                    Runs.Add(FN2CWhiteSpaceRun::Create(FRunInfo(TEXT("WhiteSpace")), ModelString, SyntaxTextStyle.NormalTextStyle, ModelRange, TabSize));
                }
                else
                {
                    Runs.Add(FSlateTextRun::Create(FRunInfo(GetRunName(Span.Kind)), ModelString, GetStyle(Span.Kind), ModelRange));
                }
                PieceBegin = PieceEnd;
            }
        }

        LinesToAdd.Emplace(MoveTemp(ModelString), MoveTemp(Runs));
    }

    TargetTextLayout.AddLines(LinesToAdd);
}

FN2CRichTextSyntaxHighlighter::FN2CRichTextSyntaxHighlighter(
    TSharedPtr<ISyntaxTokenizer> InTokenizer,
    const FSyntaxTextStyle& InSyntaxTextStyle,
    TSharedPtr<FN2CSyntaxDefinition> InSyntaxDef)
    : FSyntaxHighlighterTextLayoutMarshaller(MoveTemp(InTokenizer))
    , SyntaxTextStyle(InSyntaxTextStyle)
    , SyntaxDefinition(InSyntaxDef)
{
    check(SyntaxDefinition.IsValid());
    SyntaxDefinition->GetCommentDelimiters(LineComment, BlockCommentStart, BlockCommentEnd);
    bHasPreprocessor = SyntaxDefinition->GetLanguage() == EN2CCodeLanguage::Cpp;

    // First kind wins: brackets, then keywords, then operators
    auto AddWords = [this](const TArray<FString>& Words, ETokenKind Kind)
    {
        for (const FString& Word : Words)
        {
            if (!Word.IsEmpty() && !WordKinds.Contains(FStringView(Word)))
            {
                WordKinds.Add(FStringView(Word), Kind);
            }
        }
    };
    AddWords(SyntaxDefinition->GetParentheses(), ETokenKind::Parentheses);
    AddWords(SyntaxDefinition->GetCurlyBraces(), ETokenKind::CurlyBraces);
    AddWords(SyntaxDefinition->GetSquareBrackets(), ETokenKind::SquareBrackets);
    AddWords(SyntaxDefinition->GetKeywords(), ETokenKind::Keyword);
    AddWords(SyntaxDefinition->GetOperators(), ETokenKind::Operator);
}
