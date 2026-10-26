// Copyright (c) 2025 Nick McClure (Protospatial). All Rights Reserved.

#include "Models/N2CLogging.h"
#include "Models/N2CPin.h"
#include "Utils/N2CPinTypeCompatibility.h"
#include "Utils/Validators/N2CPinValidator.h"

bool FN2CPinDefinition::IsCompatibleWith(const EN2CPinType& OtherType) const
{
    N2C_LOG_FUNCTION_ENTRY();
    return FN2CPinTypeCompatibility::AreTypesCompatible(Type, OtherType);
}

bool FN2CPinDefinition::IsCompatibleWith(const FN2CPinDefinition& OtherPin) const
{
    N2C_LOG_FUNCTION_ENTRY();
    return FN2CPinTypeCompatibility::ArePinsCompatible(*this, OtherPin);
}

bool FN2CPinDefinition::IsValid() const
{
    N2C_LOG_FUNCTION_ENTRY();
    FN2CPinValidator Validator;
    FString ErrorMessage;
    return Validator.Validate(*this, ErrorMessage);
}
