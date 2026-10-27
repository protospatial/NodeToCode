// Copyright (c) 2025 Nick McClure (Protospatial). All Rights Reserved.

#include "Models/N2CPin.h"
#include "Utils/Validators/N2CPinValidator.h"

bool FN2CPinDefinition::IsValid() const
{
    FN2CPinValidator Validator;
    FString ErrorMessage;
    return Validator.Validate(*this, ErrorMessage);
}
