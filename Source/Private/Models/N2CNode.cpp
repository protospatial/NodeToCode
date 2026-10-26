// Copyright (c) 2025 Nick McClure (Protospatial). All Rights Reserved.

#include "Models/N2CNode.h"
#include "Models/N2CLogging.h"
#include "Utils/Validators/N2CNodeValidator.h"

bool FN2CNodeDefinition::IsValid() const
{
    N2C_LOG_FUNCTION_ENTRY();
    FN2CNodeValidator Validator;
    FString ErrorMessage;
    return Validator.Validate(*this, ErrorMessage);
}
