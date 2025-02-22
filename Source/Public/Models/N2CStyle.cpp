// Copyright (c) 2025 Nick McClure (Protospatial). All Rights Reserved.

#include "N2CStyle.h"
#include "Styling/SlateStyleRegistry.h"
#include "Framework/Application/SlateApplication.h"

TSharedPtr<FSlateStyleSet> N2CStyle::StyleInstance = nullptr;

void N2CStyle::Initialize()
{
	if (!StyleInstance.IsValid())
	{
		StyleInstance = Create();
		FSlateStyleRegistry::RegisterSlateStyle(*StyleInstance.Get());
	}
}

void N2CStyle::Shutdown()
{
	if (StyleInstance.IsValid())
	{
		FSlateStyleRegistry::UnRegisterSlateStyle(*StyleInstance.Get());
		StyleInstance.Reset();
	}
}

const ISlateStyle& N2CStyle::Get()
{
	if (!StyleInstance.IsValid())
	{
		Initialize();
	}
	return *StyleInstance.Get();
}

FString N2CStyle::InContent(const FString& RelativePath, const ANSICHAR* Extension)
{
    static FString Content = IPluginManager::Get().FindPlugin(TEXT("NodeToCode"))->GetBaseDir() / TEXT("Resources");
    return (Content / RelativePath) + Extension;
}

const FName& N2CStyle::GetStyleSetName() const
{
    static FName StyleName(TEXT("NodeToCodeStyle"));
    return StyleName;
}

TSharedRef<FSlateStyleSet> N2CStyle::Create()
{
    static FName StyleSetName(TEXT("NodeToCodeStyle"));
    TSharedRef<FSlateStyleSet> Style = MakeShareable(new FSlateStyleSet(StyleSetName));
    
    // Set content root using plugin manager
    Style->SetContentRoot(IPluginManager::Get().FindPlugin(TEXT("NodeToCode"))->GetBaseDir() / TEXT("Resources"));
	
    Style->Set("NodeToCode.ToolbarButton", 
        new N2C_PLUGIN_BRUSH(TEXT("button_icon"), FVector2D(128.0f, 128.0f)));

    return Style;
}

