// Copyright (c) 2025 Nick McClure (Protospatial). All Rights Reserved.

#pragma once

#include "Interfaces/IPluginManager.h"
#include "Styling/SlateTypes.h"

class N2CStyle : public FSlateStyleSet
{
public:
    static void Initialize();
    static void Shutdown();
    static void Reinitialize();
    static const ISlateStyle& Get();
    virtual const FName& GetStyleSetName() const override;

    // Add helper method for content paths
    static FString InContent(const FString& RelativePath, const ANSICHAR* Extension);

    // Button style accessors — return stable pointers for live SButton updates
    static const FButtonStyle& GetButtonStyle();
    static const FButtonStyle& GetSimpleButtonStyle();
    static const FButtonStyle& GetNoBorderStyle();

    // Border brush accessors — return stable pointers for live SBorder updates
    static const FSlateBrush& GetPanelBorderBrush();
    static const FSlateBrush& GetDarkPanelBorderBrush();

    // Combo box style accessors — return stable pointers for live updates
    static const FComboBoxStyle& GetComboBoxStyle();
    static const FTableRowStyle& GetComboRowStyle();

    // Widget style accessors — return stable pointers for live updates
    static const FCheckBoxStyle& GetCheckBoxStyle();
    static const FEditableTextBoxStyle& GetEditableTextBoxStyle();
    static const FTableRowStyle& GetTableRowStyle();
    static const FHeaderRowStyle& GetHeaderRowStyle();
    static const FScrollBarStyle& GetScrollBarStyle();

    // Update styles in-place from current palette (called on settings change)
    static void UpdateButtonStyles();
    static void UpdateBorderBrushes();
    static void UpdateComboBoxStyle();
    static void UpdateWidgetStyles();

private:
    static TSharedRef<FSlateStyleSet> Create();
    static TSharedPtr<FSlateStyleSet> StyleInstance;

    // Static FButtonStyle instances with stable addresses so SButton pointers remain valid
    static FButtonStyle N2CButtonStyle;
    static FButtonStyle N2CSimpleButtonStyle;
    static FButtonStyle N2CNoBorderStyle;

    // Static FSlateBrush instances for panel borders (assigned FSlateRoundedBoxBrush values)
    static FSlateBrush N2CPanelBorderBrush;
    static FSlateBrush N2CDarkPanelBorderBrush;

    // Static combo box styles
    static FComboBoxStyle N2CComboBoxStyle;
    static FTableRowStyle N2CComboRowStyle;

    // Static widget styles
    static FCheckBoxStyle N2CCheckBoxStyle;
    static FEditableTextBoxStyle N2CEditableTextBoxStyle;
    static FTableRowStyle N2CTableRowStyle;
    static FHeaderRowStyle N2CHeaderRowStyle;
    static FScrollBarStyle N2CScrollBarStyle;
};

// Add macro for plugin brushes
#define N2C_PLUGIN_BRUSH(RelativePath,...) FSlateImageBrush(N2CStyle::InContent(RelativePath,".png"),__VA_ARGS__)
