// Copyright (c) 2025 Nick McClure (Protospatial). All Rights Reserved.

#include "Code Editor/Syntax/N2CRichTextSyntaxHighlighter.h"
#include "Code Editor/Syntax/N2CSyntaxDefinitionFactory.h"
#include "Code Editor/Syntax/N2CWhiteSpaceRun.h"

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

    // Create tokenizer rules from syntax definition
    TArray<FSyntaxTokenizer::FRule> TokenizerRules;

    // Add operator rules
    for (const FString& Operator : SyntaxDef->GetOperators())
    {
        TokenizerRules.Emplace(FSyntaxTokenizer::FRule(Operator));
    }

    // Add keyword rules with word boundary markers to prevent partial matches
    for (const FString& Keyword : SyntaxDef->GetKeywords())
    {
        TokenizerRules.Emplace(FSyntaxTokenizer::FRule(TEXT("\\b") + Keyword + TEXT("\\b")));
    }

    // Create the syntax text styles
    FSyntaxTextStyle SyntaxStyles(LanguageId, ThemeName);

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

void FN2CRichTextSyntaxHighlighter::ParseTokens(const FString& SourceString, FTextLayout& TargetTextLayout, TArray<ISyntaxTokenizer::FTokenizedLine> TokenizedLines)
{
    enum class EParseState : uint8
    {
        None,
        LookingForString,
        LookingForChar,
        LookingForLineComment,
        LookingForBlockComment,
        LookingForPreprocessor
    };

    FString LineComment, BlockCommentStart, BlockCommentEnd;
    SyntaxDefinition->GetCommentDelimiters(LineComment, BlockCommentStart, BlockCommentEnd);

    TArray<FTextLayout::FNewLineData> LinesToAdd;
    LinesToAdd.Reserve(TokenizedLines.Num());

    EParseState ParseState = EParseState::None;
    for (const ISyntaxTokenizer::FTokenizedLine& TokenizedLine : TokenizedLines)
    {
        TSharedRef<FString> ModelString = MakeShareable(new FString());
        TArray<TSharedRef<IRun>> Runs;

        for (const ISyntaxTokenizer::FToken& Token : TokenizedLine.Tokens)
        {
            const FString TokenText = SourceString.Mid(Token.Range.BeginIndex, Token.Range.Len());
            const FTextRange ModelRange(ModelString->Len(), ModelString->Len() + TokenText.Len());
            ModelString->Append(TokenText);

            FRunInfo RunInfo(TEXT("N2CCodeEditor.Normal"));
            FTextBlockStyle TextBlockStyle = SyntaxTextStyle.NormalTextStyle;

            const bool bIsWhitespace = FString(TokenText).TrimEnd().IsEmpty();
            if (!bIsWhitespace)
            {
                bool bHasMatchedSyntax = false;

                // Handle strings first
                if (ParseState == EParseState::None && 
                    TokenText.Len() == 1 && 
                    SyntaxDefinition->GetStringDelimiters().Contains(TokenText[0]))
                {
                    RunInfo.Name = TEXT("String");
                    TextBlockStyle = SyntaxTextStyle.StringTextStyle;
                    ParseState = EParseState::LookingForString;
                    bHasMatchedSyntax = true;
                }
                else if (ParseState == EParseState::LookingForString)
                {
                    RunInfo.Name = TEXT("String");
                    TextBlockStyle = SyntaxTextStyle.StringTextStyle;
                    
                    if (TokenText.Len() == 1 && SyntaxDefinition->GetStringDelimiters().Contains(TokenText[0]))
                    {
                        ParseState = EParseState::None;
                    }
                    bHasMatchedSyntax = true;
                }
                // Handle comments
                else if (ParseState == EParseState::None && TokenText == LineComment)
                {
                    RunInfo.Name = TEXT("Comment");
                    TextBlockStyle = SyntaxTextStyle.CommentTextStyle;
                    ParseState = EParseState::LookingForLineComment;
                    bHasMatchedSyntax = true;
                }
                else if (ParseState == EParseState::None && TokenText == BlockCommentStart)
                {
                    RunInfo.Name = TEXT("Comment");
                    TextBlockStyle = SyntaxTextStyle.CommentTextStyle;
                    ParseState = EParseState::LookingForBlockComment;
                    bHasMatchedSyntax = true;
                }
                else if (ParseState == EParseState::LookingForBlockComment && TokenText == BlockCommentEnd)
                {
                    RunInfo.Name = TEXT("Comment");
                    TextBlockStyle = SyntaxTextStyle.CommentTextStyle;
                    ParseState = EParseState::None;
                    bHasMatchedSyntax = true;
                }
                // Handle brackets, keywords and operators
                else if (ParseState == EParseState::None)
                {
                    if (SyntaxDefinition->GetParentheses().Contains(TokenText))
                    {
                        RunInfo.Name = TEXT("Parentheses");
                        TextBlockStyle = SyntaxTextStyle.ParenthesesTextStyle;
                        bHasMatchedSyntax = true;
                    }
                    else if (SyntaxDefinition->GetCurlyBraces().Contains(TokenText))
                    {
                        RunInfo.Name = TEXT("CurlyBraces");
                        TextBlockStyle = SyntaxTextStyle.CurlyBracesTextStyle;
                        bHasMatchedSyntax = true;
                    }
                    else if (SyntaxDefinition->GetSquareBrackets().Contains(TokenText))
                    {
                        RunInfo.Name = TEXT("SquareBrackets");
                        TextBlockStyle = SyntaxTextStyle.SquareBracketsTextStyle;
                        bHasMatchedSyntax = true;
                    }
                    else if (SyntaxDefinition->GetKeywords().Contains(TokenText))
                    {
                        RunInfo.Name = TEXT("Keyword");
                        TextBlockStyle = SyntaxTextStyle.KeywordTextStyle;
                        bHasMatchedSyntax = true;
                    }
                    else if (SyntaxDefinition->GetOperators().Contains(TokenText))
                    {
                        RunInfo.Name = TEXT("Operator");
                        TextBlockStyle = SyntaxTextStyle.OperatorTextStyle;
                        bHasMatchedSyntax = true;
                    }
                    // Handle numbers
                    else if (IsNumeric(TokenText))
                    {
                        RunInfo.Name = TEXT("Number");
                        TextBlockStyle = SyntaxTextStyle.NumberTextStyle;
                        bHasMatchedSyntax = true;
                    }
                    // Handle preprocessor directives (only for C++)
                    else if (ParseState == EParseState::None && 
                            SyntaxDefinition->GetLanguage() == EN2CCodeLanguage::Cpp)
                    {
                        // Check if this token starts with # or is part of the preprocessor directive
                        if (TokenText.StartsWith(TEXT("#")) || 
                            (TokenText.Len() > 0 && ModelString->Len() > 0 && (*ModelString)[0] == TEXT('#') && 
                             !FChar::IsWhitespace(TokenText[0])))
                        {
                            RunInfo.Name = TEXT("Preprocessor");
                            TextBlockStyle = SyntaxTextStyle.PreprocessorTextStyle;
                            bHasMatchedSyntax = true;
                        }
                    }
                }

                // Handle literals and continuing states
                if (!bHasMatchedSyntax)
                {
                    switch (ParseState)
                    {
                        case EParseState::LookingForString:
                            RunInfo.Name = TEXT("String");
                            TextBlockStyle = SyntaxTextStyle.StringTextStyle;
                            break;
                        case EParseState::LookingForLineComment:
                        case EParseState::LookingForBlockComment:
                            RunInfo.Name = TEXT("Comment");
                            TextBlockStyle = SyntaxTextStyle.CommentTextStyle;
                            break;
                        default:
                            break;
                    }
                }
            }
            else
            {
                RunInfo.Name = TEXT("WhiteSpace");
                TSharedRef<ISlateRun> Run = FN2CWhiteSpaceRun::Create(RunInfo, ModelString, TextBlockStyle, ModelRange, 4);
                Runs.Add(Run);
                continue;
            }

            TSharedRef<ISlateRun> Run = FSlateTextRun::Create(RunInfo, ModelString, TextBlockStyle, ModelRange);
            Runs.Add(Run);
        }

        // Reset line comment state at end of line
        if (ParseState == EParseState::LookingForLineComment)
        {
            ParseState = EParseState::None;
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
}
