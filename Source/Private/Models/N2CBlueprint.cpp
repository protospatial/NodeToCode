// Copyright (c) 2025 Nick McClure (Protospatial). All Rights Reserved.

#include "Models/N2CBlueprint.h"
#include "Utils/Validators/N2CBlueprintValidator.h"

bool FN2CGraph::IsValid() const
{
    // Use the validator to check the graph
    FN2CBlueprintValidator Validator;
    FString ErrorMessage;
    return Validator.ValidateGraph(*this, ErrorMessage);
}

bool FN2CBlueprint::IsValid() const
{
    // Use the validator to check the blueprint
    FN2CBlueprintValidator Validator;
    FString ErrorMessage;
    return Validator.Validate(*this, ErrorMessage);
}

bool FN2CStruct::IsValid() const
{
    FN2CBlueprintValidator Validator;
    FString ErrorMessage;
    return Validator.ValidateStruct(*this, ErrorMessage);
}

bool FN2CEnum::IsValid() const
{
    FN2CBlueprintValidator Validator;
    FString ErrorMessage;
    return Validator.ValidateEnum(*this, ErrorMessage);
}
