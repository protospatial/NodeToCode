// Copyright (c) 2025 Nick McClure (Protospatial). All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/Widget.h"
#include "Core/N2CSettings.h"
#include "Public/Code Editor/Models/N2CCodeLanguage.h"
#include "N2CCodeEditorWidget.generated.h"

/**
 * UMG widget wrapper for the N2C code editor
 */
UCLASS(Meta=(DisplayName="Code Editor"))
class NODETOCODE_API UN2CCodeEditorWidget : public UWidget
{
    GENERATED_BODY()

public:
    UN2CCodeEditorWidget();

    /** Get the current text content */
    UFUNCTION(BlueprintCallable, Category = "Code Editor")
    FText GetText() const;

    /** Set new text content */
    UFUNCTION(BlueprintCallable, Category = "Code Editor")
    void SetText(const FText& NewText);

    /** Change the programming language */
    UFUNCTION(BlueprintCallable, Category = "Code Editor")
    void SetLanguage(EN2CCodeLanguage NewLanguage);

    /** Event fired when text changes */
    DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnTextChangedEvent, const FText&, NewText);
    
    /** Event fired when cursor position changes */
    DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnCursorMovedEvent, int32, Line, int32, Column);

    /** Event fired when text selection changes */
    DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnSelectionChangedEvent, int32, StartIndex, int32, EndIndex);
    
    /** Fired when the text content changes */
    UPROPERTY(BlueprintAssignable, Category = "Code Editor|Events")
    FOnTextChangedEvent OnTextChanged;

    /** Fired when the cursor position changes */
    UPROPERTY(BlueprintAssignable, Category = "Code Editor|Events")
    FOnCursorMovedEvent OnCursorMoved;

    /** Fired when text selection changes */
    UPROPERTY(BlueprintAssignable, Category = "Code Editor|Events")
    FOnSelectionChangedEvent OnSelectionChanged;

    /** Insert text at the current cursor position */
    UFUNCTION(BlueprintCallable, Category = "Code Editor|Text Manipulation")
    void InsertTextAtCursor(const FString& InText);

    /** Replace the currently selected text */
    UFUNCTION(BlueprintCallable, Category = "Code Editor|Text Manipulation")
    void ReplaceSelectedText(const FString& NewText);

    /** Delete the currently selected text */
    UFUNCTION(BlueprintCallable, Category = "Code Editor|Text Manipulation")
    void DeleteSelectedText();

    /** Get the currently selected text */
    UFUNCTION(BlueprintPure, Category = "Code Editor|Text Manipulation")
    FText GetSelectedText() const;

    /** Set the cursor position */
    UFUNCTION(BlueprintCallable, Category = "Code Editor|Cursor")
    void SetCursorPosition(int32 Line, int32 Column);

    /** Get the current cursor position */
    UFUNCTION(BlueprintPure, Category = "Code Editor|Cursor")
    void GetCursorPosition(int32& OutLine, int32& OutColumn) const;

    /** Select text range using line and column positions */
    UFUNCTION(BlueprintCallable, Category = "Code Editor|Selection", meta=(DisplayName="Select Text"))
    void SelectText(int32 StartLine, int32 StartColumn, int32 EndLine, int32 EndColumn);

    /** Get the current text selection */
    UFUNCTION(BlueprintPure, Category = "Code Editor|Selection")
    void GetSelection(int32& OutStartIndex, int32& OutEndIndex) const;

    /** Set the font size */
    UFUNCTION(BlueprintCallable, Category = "Code Editor|Appearance")
    void SetFontSize(int32 NewSize);

    /** Set whether to enable word wrap */
    void SetWordWrap(bool bEnable);

    /** Set the current theme */
    UFUNCTION(BlueprintCallable, Category = "Code Editor|Appearance")
    void SetTheme(const FName& NewTheme);

    /** Set the tab size in spaces */
    void SetTabSize(int32 NewSize);

    /** Get available themes for the current language */
    UFUNCTION(BlueprintCallable, Category = "Code Editor|Themes")
    TArray<FName> GetAvailableThemes() const;

    /** Get available themes for the current language */
    UFUNCTION(BlueprintCallable, Category = "Code Editor|Themes")
    FN2CCodeEditorColors GetThemeFromName(const FName Name) const;
    
    /** Get the current theme name */
    UFUNCTION(BlueprintPure, Category = "Code Editor|Themes")
    FName GetCurrentTheme() const;

    /** Release Slate resources */
    virtual void ReleaseSlateResources(bool bReleaseChildren) override;

protected:
    virtual TSharedRef<SWidget> RebuildWidget() override;
    virtual void SynchronizeProperties() override;

    /** The text displayed in the editor */
    UPROPERTY(EditAnywhere, Category = "Code Editor")
    FText Text;

    /** The programming language for syntax highlighting */
    UPROPERTY(EditAnywhere, Category = "Code Editor", meta=(DisplayName="Language", ToolTip="Select the programming language for syntax highlighting"))
    EN2CCodeLanguage Language;

    /** The theme name to use for syntax highlighting */
    UPROPERTY(EditAnywhere, Category = "Code Editor|Appearance", 
        meta = (DisplayName = "Theme", 
               ToolTip = "The color theme to use for syntax highlighting"))
    FName ThemeName;

    /** Font size for the editor text */
    UPROPERTY(EditAnywhere, Category = "Code Editor|Appearance", meta=(ClampMin="8", ClampMax="72"))
    int32 FontSize;

    /** Whether to enable word wrap */
    bool bWordWrap;

    /** Number of spaces for each tab */
    int32 TabSize;

private:
    /** The Slate widget */
    TSharedPtr<class SN2CCodeEditor> CodeEditorWidget;

    /** Callback for text changes from slate widget */
    void HandleTextChanged(const FText& NewText);
};
