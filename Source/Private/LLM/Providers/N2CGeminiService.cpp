// Copyright ProtoSpatial. All Rights Reserved.

#include "LLM/Providers/N2CGeminiService.h"

#include "LLM/N2CHttpHandler.h"
#include "LLM/N2CHttpHandlerBase.h"
#include "LLM/N2CResponseParserBase.h"
#include "LLM/N2CSystemPromptManager.h"
#include "Utils/N2CLogger.h"

bool UN2CGeminiService::Initialize(const FN2CLLMConfig& InConfig)
{
    Config = InConfig;

    // Use default endpoint if none provided                                                                                                                                                              
    if (Config.ApiEndpoint.IsEmpty())                                                                                                                                                                     
    {                                                                                                                                                                                                     
        Config.ApiEndpoint = DefaultEndpoint;                                                                                                                                                             
    }

    // Create HTTP handler
    HttpHandler = NewObject<UN2CHttpHandler>(this);
    if (!HttpHandler)
    {
        FN2CLogger::Get().LogError(TEXT("Failed to create HTTP handler"), TEXT("GeminiService"));
        return false;
    }
    HttpHandler->Initialize(Config);

    // Create Gemini response parser
    ResponseParser = NewObject<UN2CGeminiResponseParser>(this);
    if (!ResponseParser)
    {
        FN2CLogger::Get().LogError(TEXT("Failed to create Gemini response parser"), TEXT("GeminiService"));
        return false;
    }
    ResponseParser->Initialize();

    // Create system prompt manager
    PromptManager = NewObject<UN2CSystemPromptManager>(this);
    if (!PromptManager)
    {
        FN2CLogger::Get().LogError(TEXT("Failed to create system prompt manager"), TEXT("GeminiService"));
        return false;
    }
    PromptManager->Initialize(Config);

    // Set required headers
    TMap<FString, FString> Headers;
    GetProviderHeaders(Headers);
    HttpHandler->ExtraHeaders = Headers;

    bIsInitialized = true;
    return true;
}

void UN2CGeminiService::SendRequest(
    const FString& JsonPayload,
    const FString& SystemMessage,
    const FOnLLMResponseReceived& OnComplete)
{
    if (!bIsInitialized)
    {
        FN2CLogger::Get().LogError(TEXT("Service not initialized"), TEXT("GeminiService"));
        const bool bExecuted = OnComplete.ExecuteIfBound(TEXT("{\"error\": \"Service not initialized\"}"));
        return;
    }
    
    // Log provider and model info
    FN2CLogger::Get().Log(
        FString::Printf(TEXT("Sending request to Gemini using model: %s"), *Config.Model),
        EN2CLogSeverity::Info,
        TEXT("GeminiService")
    );

    // Format request payload for Gemini
    FString FormattedPayload = FormatRequestPayload(JsonPayload, SystemMessage);

    // Build full endpoint (Gemini typically uses "model_name:generateContent")
    FString FullEndpoint = FString::Printf(TEXT("%s%s:generateContent?key=%s"),
                                           *Config.ApiEndpoint, *Config.Model, *Config.ApiKey);

    // Send request through HTTP handler
	// Gemini REST API doesn't use an Authorization header but instead includes it as a "key" query parameter in the URL.
    HttpHandler->PostLLMRequest(
        FullEndpoint,
        TEXT(""),
        FormattedPayload,
        OnComplete
    );
}

void UN2CGeminiService::GetConfiguration(
    FString& OutEndpoint,
    FString& OutAuthToken,
    bool& OutSupportsSystemPrompts)
{
    OutEndpoint = Config.ApiEndpoint;
    OutAuthToken = Config.ApiKey;
    
    // Default to supporting system prompts since all Gemini models currently support system prompts
    OutSupportsSystemPrompts = true;
}

void UN2CGeminiService::GetProviderHeaders(TMap<FString, FString>& OutHeaders) const
{
    OutHeaders.Add(TEXT("Content-Type"), TEXT("application/json"));
}

FString UN2CGeminiService::FormatRequestPayload(const FString& UserMessage, const FString& SystemMessage) const
{

    // STEP 1: Combine reference source files into user message if applicable.
    FString FinalUserMessage = UserMessage;
    PromptManager->PrependSourceFilesToUserMessage(FinalUserMessage);

    // STEP 2: Load & parse JSON schema
    const FString JsonSchema = TEXT(R"(
      {
		  "type": "object",
		  "properties": {
		    "graphs": {
		      "type": "array",
		      "items": {
		        "type": "object",
		        "properties": {
		          "graph_name": {
		            "type": "string"
		          },
		          "graph_type": {
		            "type": "string"
		          },
		          "graph_class": {
		            "type": "string"
		          },
		          "code": {
		            "type": "object",
		            "properties": {
		              "graphDeclaration": {
		                "type": "string"
		              },
		              "graphImplementation": {
		                "type": "string"
		              },
		              "implementationNotes": {
		                "type": "string"
		              }
		            },
		            "required": [
		              "graphDeclaration",
		              "graphImplementation"
		            ]
		          }
		        },
		        "required": [
		          "graph_name",
		          "graph_type",
		          "graph_class",
		          "code"
		        ]
		      }
		    }
		  },
		  "required": [
		    "graphs"
		  ]
		}
    )");

    TSharedPtr<FJsonObject> SchemaObject;
    {
        TSharedRef<TJsonReader<>> SchemaReader = TJsonReaderFactory<>::Create(JsonSchema);
        FJsonSerializer::Deserialize(SchemaReader, SchemaObject);
        if (!SchemaObject.IsValid())
        {
            FN2CLogger::Get().LogError(TEXT("Failed to parse Gemini JSON schema"), TEXT("GeminiService"));
        }
    }

    // 3. Construct the root JSON
    TSharedPtr<FJsonObject> RootObject = MakeShared<FJsonObject>();

    // 3a. Build the "contents" array with role = user, containing parts with the user message.
    {
        TSharedPtr<FJsonObject> UserObject = MakeShared<FJsonObject>();
        UserObject->SetStringField(TEXT("role"), TEXT("user"));

        TArray<TSharedPtr<FJsonValue>> PartsArray;
        {
            TSharedPtr<FJsonObject> PartObj = MakeShared<FJsonObject>();
            PartObj->SetStringField(TEXT("text"), FinalUserMessage);
            PartsArray.Add(MakeShared<FJsonValueObject>(PartObj));
        }

        UserObject->SetArrayField(TEXT("parts"), PartsArray);

        // Wrap this user object in the "contents" array
        TArray<TSharedPtr<FJsonValue>> ContentsArray;
        ContentsArray.Add(MakeShared<FJsonValueObject>(UserObject));

        RootObject->SetArrayField(TEXT("contents"), ContentsArray);
    }

    // 3b. Add the "systemInstruction" object. 
    {
        TSharedPtr<FJsonObject> SysInstructionObj = MakeShared<FJsonObject>();
        SysInstructionObj->SetStringField(TEXT("role"), TEXT("user")); // or "system" if needed

        TArray<TSharedPtr<FJsonValue>> SysPartsArray;
        {
            TSharedPtr<FJsonObject> PartObj = MakeShared<FJsonObject>();
            PartObj->SetStringField(TEXT("text"), SystemMessage);
            SysPartsArray.Add(MakeShared<FJsonValueObject>(PartObj));
        }
        SysInstructionObj->SetArrayField(TEXT("parts"), SysPartsArray);

        RootObject->SetObjectField(TEXT("systemInstruction"), SysInstructionObj);
    }

    // 3c. Build the "generationConfig" block with structured output + other parameters.
    {
        TSharedPtr<FJsonObject> GenConfigObj = MakeShared<FJsonObject>();

        // Example parameters:
        GenConfigObj->SetNumberField(TEXT("temperature"), 0.0);
        GenConfigObj->SetNumberField(TEXT("topK"), 40.0);
        GenConfigObj->SetNumberField(TEXT("topP"), 0.95);
        GenConfigObj->SetNumberField(TEXT("maxOutputTokens"), 8192);

        // Insert our parsed schema if valid and model supports structured outputs
        if (SchemaObject.IsValid() && Config.Model != TEXT("gemini-2.0-flash-thinking-exp-01-21"))
        {
        	// We want application/json structured output
        	GenConfigObj->SetStringField(TEXT("responseMimeType"), TEXT("application/json"));
            GenConfigObj->SetObjectField(TEXT("responseSchema"), SchemaObject);
        }

        RootObject->SetObjectField(TEXT("generationConfig"), GenConfigObj);
    }

    // 4. Serialize to JSON string
    FString Payload;
    const TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Payload);
    FJsonSerializer::Serialize(RootObject.ToSharedRef(), Writer);

    // Optionally log the payload
    FN2CLogger::Get().Log(FString::Printf(TEXT("Gemini Request Payload:\n%s"), *Payload), EN2CLogSeverity::Debug);

    return Payload;
}
