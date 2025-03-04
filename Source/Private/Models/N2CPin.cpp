// Copyright (c) 2025 Nick McClure (Protospatial). All Rights Reserved.

#include "Models/N2CPin.h"
#include "Utils/N2CPinTypeCompatibility.h"
#include "Utils/Validators/N2CPinValidator.h"

bool FN2CPinDefinition::IsCompatibleWith(const EN2CPinType& OtherType) const
{
    return FN2CPinTypeCompatibility::AreTypesCompatible(Type, OtherType);
}

bool FN2CPinDefinition::IsCompatibleWith(const FN2CPinDefinition& OtherPin) const
{
    return FN2CPinTypeCompatibility::ArePinsCompatible(*this, OtherPin);
}

bool FN2CPinDefinition::IsValid() const
{
    FN2CPinValidator Validator;
    FString ErrorMessage;
    return Validator.Validate(*this, ErrorMessage);
}
