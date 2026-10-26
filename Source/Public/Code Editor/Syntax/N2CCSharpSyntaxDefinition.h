// Copyright (c) 2025 Nick McClure (Protospatial). All Rights Reserved.

#pragma once

#include "Models/N2CLogging.h"
#include "N2CSyntaxDefinition.h"

class FN2CCSharpSyntaxDefinition : public FN2CSyntaxDefinition
{
public:
    FN2CCSharpSyntaxDefinition();
    
    virtual const TArray<FString>& GetKeywords() const override {
      N2C_LOG_FUNCTION_ENTRY();
      return Keywords; }
    virtual const TArray<FString>& GetOperators() const override {
      N2C_LOG_FUNCTION_ENTRY();
      return Operators; }
    virtual void GetCommentDelimiters(FString& OutLineComment, FString& OutBlockCommentStart, FString& OutBlockCommentEnd) const override;
    virtual const TArray<TCHAR>& GetStringDelimiters() const override {
      N2C_LOG_FUNCTION_ENTRY();
      return StringDelimiters; }
    virtual const TArray<FString>& GetParentheses() const override {
      N2C_LOG_FUNCTION_ENTRY();
      return Parentheses; }
    virtual const TArray<FString>& GetCurlyBraces() const override {
      N2C_LOG_FUNCTION_ENTRY();
      return CurlyBraces; }
    virtual const TArray<FString>& GetSquareBrackets() const override {
      N2C_LOG_FUNCTION_ENTRY();
      return SquareBrackets; }
    virtual EN2CCodeLanguage GetLanguage() const override {
      N2C_LOG_FUNCTION_ENTRY();
      return EN2CCodeLanguage::CSharp; }
};
