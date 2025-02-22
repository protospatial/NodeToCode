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

    virtual ~FN2CRichTextSyntaxHighlighter();

protected:
    virtual void ParseTokens(const FString& SourceString, FTextLayout& TargetTextLayout, TArray<ISyntaxTokenizer::FTokenizedLine> TokenizedLines) override;

    FN2CRichTextSyntaxHighlighter(TSharedPtr<ISyntaxTokenizer> InTokenizer, const FSyntaxTextStyle& InSyntaxTextStyle, TSharedPtr<FN2CSyntaxDefinition> InSyntaxDef);

private:
    /** Check if a string represents a numeric value */
    bool IsNumeric(const FString& Text) const
    {
        // Handle hexadecimal numbers
        if (Text.StartsWith(TEXT("0x")) || Text.StartsWith(TEXT("0X")))
        {
            // Check if all remaining characters are valid hex digits
            for (int32 i = 2; i < Text.Len(); ++i)
            {
                TCHAR Ch = Text[i];
                if (!FChar::IsHexDigit(Ch))
                {
                    return false;
                }
            }
            return Text.Len() > 2; // Must have at least one digit after 0x
        }

        // Handle float suffixes
        FString NumericPart = Text;
        if (Text.EndsWith(TEXT("f")) || Text.EndsWith(TEXT("F")))
        {
            NumericPart = Text.LeftChop(1);
        }

        // Check if it's a valid number (integer or float)
        bool bHasDecimalPoint = false;
        bool bHasDigits = false;
        
        // Handle negative numbers
        int32 StartIdx = NumericPart.StartsWith(TEXT("-")) ? 1 : 0;
        
        for (int32 i = StartIdx; i < NumericPart.Len(); ++i)
        {
            TCHAR Ch = NumericPart[i];
            
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

    /** Styles used to display the text */
    FSyntaxTextStyle SyntaxTextStyle;

    /** The syntax definition for the current language */
    TSharedPtr<FN2CSyntaxDefinition> SyntaxDefinition;

    /** String representing tabs */
    FString TabString;
};
