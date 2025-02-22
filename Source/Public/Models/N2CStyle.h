// Copyright (c) 2025 Nick McClure (Protospatial). All Rights Reserved.

#pragma once

#include "Interfaces/IPluginManager.h"

class N2CStyle : public FSlateStyleSet
{
public:
    static void Initialize();
    static void Shutdown();
    static const ISlateStyle& Get();
    virtual const FName& GetStyleSetName() const override;
    
    // Add helper method for content paths
    static FString InContent(const FString& RelativePath, const ANSICHAR* Extension);

private:
    static TSharedRef<FSlateStyleSet> Create();
    static TSharedPtr<FSlateStyleSet> StyleInstance;
};

// Add macro for plugin brushes
#define N2C_PLUGIN_BRUSH(RelativePath,...) FSlateImageBrush(N2CStyle::InContent(RelativePath,".png"),__VA_ARGS__)
