// Copyright (c) 2025 Nick McClure (Protospatial). All Rights Reserved.

#pragma once

#include "N2CSyntaxDefinition.h"
#include "Framework/Text/SyntaxHighlighterTextLayoutMarshaller.h"
#include "Code Editor/Models/N2CCodeEditorStyle.h"


class FTextLayout;

/**
 * Handles syntax highlighting for different programming languages
 */
class FN2CRichTextSyntaxHighlighter : public FSyntaxHighlighterTextLayoutMarshaller
{
public:
    struct FSyntaxTextStyle
    {
        FSyntaxTextStyle(const FName& InLanguageId, const FName& InThemeName)
            : NormalTextStyle(FN2CCodeEditorStyle::GetLanguageStyle(InLanguageId, InThemeName, TEXT("Normal")))
            , OperatorTextStyle(FN2CCodeEditorStyle::GetLanguageStyle(InLanguageId, InThemeName, TEXT("Operator")))
            , KeywordTextStyle(FN2CCodeEditorStyle::GetLanguageStyle(InLanguageId, InThemeName, TEXT("Keyword")))
            , StringTextStyle(FN2CCodeEditorStyle::GetLanguageStyle(InLanguageId, InThemeName, TEXT("String")))
            , NumberTextStyle(FN2CCodeEditorStyle::GetLanguageStyle(InLanguageId, InThemeName, TEXT("Number")))
            , CommentTextStyle(FN2CCodeEditorStyle::GetLanguageStyle(InLanguageId, InThemeName, TEXT("Comment")))
            , PreprocessorTextStyle(FN2CCodeEditorStyle::GetLanguageStyle(InLanguageId, InThemeName, TEXT("Preprocessor")))
            , ParenthesesTextStyle(FN2CCodeEditorStyle::GetLanguageStyle(InLanguageId, InThemeName, TEXT("Parentheses")))
            , CurlyBracesTextStyle(FN2CCodeEditorStyle::GetLanguageStyle(InLanguageId, InThemeName, TEXT("CurlyBraces")))
            , SquareBracketsTextStyle(FN2CCodeEditorStyle::GetLanguageStyle(InLanguageId, InThemeName, TEXT("SquareBrackets")))
        {
        }

        FTextBlockStyle NormalTextStyle;
        FTextBlockStyle OperatorTextStyle;
        FTextBlockStyle KeywordTextStyle;
        FTextBlockStyle StringTextStyle;
        FTextBlockStyle NumberTextStyle;
        FTextBlockStyle CommentTextStyle;
        FTextBlockStyle PreprocessorTextStyle;
        FTextBlockStyle ParenthesesTextStyle;
        FTextBlockStyle CurlyBracesTextStyle;
        FTextBlockStyle SquareBracketsTextStyle;
    };

    static TSharedRef<FN2CRichTextSyntaxHighlighter> Create(EN2CCodeLanguage Language, const FName& ThemeName, const FTextBlockStyle& BaseStyle = FTextBlockStyle());

    /** A highlighter for JSON (the translation viewer's JSON tab), in the JavaScript colours */
    static TSharedRef<FN2CRichTextSyntaxHighlighter> CreateJson(const FName& ThemeName, const FTextBlockStyle& BaseStyle = FTextBlockStyle());

    /** Lines longer than this render as plain text: highlighting them costs more than it shows (minified JSON, data) */
    static constexpr int32 MaxHighlightedLineLength = 2000;

    virtual ~FN2CRichTextSyntaxHighlighter();

    /** Spaces a tab is drawn as in the whitespace runs (the code editor's tab size); takes effect on the next SetText */
    void SetTabSize(int32 InTabSize) { TabSize = FMath::Max(1, InTabSize); }
    int32 GetTabSize() const { return TabSize; }

protected:
    virtual void ParseTokens(const FString& SourceString, FTextLayout& TargetTextLayout, TArray<ISyntaxTokenizer::FTokenizedLine> TokenizedLines) override;

    FN2CRichTextSyntaxHighlighter(TSharedPtr<ISyntaxTokenizer> InTokenizer, const FSyntaxTextStyle& InSyntaxTextStyle, TSharedPtr<FN2CSyntaxDefinition> InSyntaxDef);

private:
    /** What a stretch of text is coloured as; each kind has a style in FSyntaxTextStyle and a run name */
    enum class ETokenKind : uint8
    {
        Normal,
        Operator,
        Keyword,
        String,
        Number,
        Comment,
        Preprocessor,
        Parentheses,
        CurlyBraces,
        SquareBrackets
    };

    /** How a string's closing delimiter may be escaped */
    enum class EStringEscape : uint8
    {
        /** A backslash escapes the next character, so an odd run of backslashes escapes the delimiter */
        Backslash,
        /** C# verbatim strings: a doubled quote is a quote */
        DoubledQuote,
        /** C++ raw strings: nothing is escaped */
        None
    };

    /** Case-sensitive keys for the word lookup: keywords are case-sensitive in every language here */
    struct FWordKeyFuncs : TDefaultMapKeyFuncs<FStringView, ETokenKind, false>
    {
        static FORCEINLINE bool Matches(FStringView A, FStringView B) { return A.Equals(B, ESearchCase::CaseSensitive); }
        static FORCEINLINE uint32 GetKeyHash(FStringView Key)
        {
            // FNV-1a over the characters (FStringView's own hash ignores case)
            uint32 Hash = 2166136261u;
            for (const TCHAR Char : Key)
            {
                Hash = (Hash ^ static_cast<uint32>(Char)) * 16777619u;
            }
            return Hash;
        }
    };

    static TSharedRef<FN2CRichTextSyntaxHighlighter> CreateForDefinition(TSharedRef<FN2CSyntaxDefinition> SyntaxDef, const FName& StyleLanguageId,
        const FName& ThemeName, const FTextBlockStyle& BaseStyle);

    /** The style a kind of text is drawn with (a reference: runs copy it once) */
    const FTextBlockStyle& GetStyle(ETokenKind Kind) const;

    /** The run name for a kind of text (FRunInfo::Name) */
    static const TCHAR* GetRunName(ETokenKind Kind);

    /** What a word outside strings and comments is: bracket, keyword or operator by lookup, else number or preprocessor */
    ETokenKind ClassifyWord(FStringView Word, bool bPreprocessorLine) const;

    /** Check if a string represents a numeric value */
    static bool IsNumeric(FStringView Text);

    /** Styles used to display the text */
    FSyntaxTextStyle SyntaxTextStyle;

    /** The syntax definition for the current language */
    TSharedPtr<FN2CSyntaxDefinition> SyntaxDefinition;

    /** Spaces per tab in whitespace runs */
    int32 TabSize = 4;

    /**
     * Brackets, keywords and operators, each mapped to its kind (brackets win over keywords, keywords over operators).
     * The keys view the definition's own strings, which SyntaxDefinition keeps alive and never changes
     */
    TMap<FStringView, ETokenKind, FDefaultSetAllocator, FWordKeyFuncs> WordKinds;

    /** C++ only: colour # directives as preprocessor */
    bool bHasPreprocessor = false;

    /** The definition's comment delimiters, read once */
    FString LineComment;
    FString BlockCommentStart;
    FString BlockCommentEnd;

    /** String representing tabs */
    FString TabString;
};
