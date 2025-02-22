// Copyright (c) 2025 Nick McClure (Protospatial). All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

class FN2CCodeEditorWidgetFactory : public TSharedFromThis<FN2CCodeEditorWidgetFactory>
{
public:
    static void Register();
    static void Unregister();

private:
    static TSharedPtr<FN2CCodeEditorWidgetFactory> Instance;
    
    void RegisterFactory();
    void UnregisterFactory();
};
