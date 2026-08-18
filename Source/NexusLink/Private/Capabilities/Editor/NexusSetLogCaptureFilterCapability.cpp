// Copyright byteyang. All Rights Reserved.

#include "Capabilities/Editor/NexusSetLogCaptureFilterCapability.h"

#if WITH_EDITOR

#include "Utils/NexusCapabilityResultBuilder.h"
#include "Utils/NexusArgs.h"
#include "NexusCapabilityRegistry.h"
#include "NexusMcpSchemaBuilder.h"
#include "Editor/NexusLogCapture.h"
#include "NexusLinkSettings.h"
#include "NexusMcpTool.h"

void FSetLogCaptureFilterCapability::BuildDefinition(FNexusCapabilityDefinition& Out) const
{
	Out.Name = TEXT("set_log_capture_filter");
	Out.Description = TEXT("Configure buffered log categories. Empty=all; Warning/Error always captured. Affects get_output_log.");
	Out.InputSchema = FNexusSchema::Object()
		.Prop(TEXT("categories"), FNexusSchema::StrArr(
			TEXT("Log category substrings to capture. Empty array=all. "
			     "Examples: [\"LogTemp\"], [\"LogBlueprintUserMessages\",\"LogNexusLink\"], []. "
			     "Warning/Error always written regardless of this list.")))
		.Required({ TEXT("categories") })
		.Build();
	Out.Tags = {FNexusMcpTags::Write, FNexusMcpTags::Editor };
	Out.ExtraSearchKeywords = { TEXT("configure"), TEXT("whitelist"), TEXT("category"), TEXT("include"), TEXT("exclude") };
	Out.RelatedCapabilities = { TEXT("get_output_log") };
}

FCapabilityResult FSetLogCaptureFilterCapability::Execute(const TSharedPtr<FJsonObject>& Arguments) const
{

	return FNexusCapabilityResultBuilder::Build([&](auto& OutEntries, auto& OutTop, auto& OutError)
	{


		const TArray<TSharedPtr<FJsonValue>>* CatArrPtr = nullptr;
		if (!Arguments->TryGetArrayField(TEXT("categories"), CatArrPtr) || !CatArrPtr)
		{
			OutError = TEXT("Missing categories (string array)");
			return;
		}

		TArray<FString> Categories;
		for (const TSharedPtr<FJsonValue>& V : *CatArrPtr)
		{
			if (V.IsValid() && V->Type == EJson::String)
			{
				const FString Cat = V->AsString().TrimStartAndEnd();
				if (!Cat.IsEmpty()) Categories.Add(Cat);
			}
		}

		FNexusLogCapture::Get().SetCategoryWhitelist(Categories);

		UNexusLinkSettings* Settings = UNexusLinkSettings::Get();
		if (Settings)
		{
			Settings->LogCaptureCategories = Categories;
			Settings->SaveConfig();
		}

		TArray<TSharedPtr<FJsonValue>> CatArr;
		for (const FString& Cat : Categories)
			CatArr.Add(MakeShared<FJsonValueString>(Cat));

		TSharedPtr<FJsonObject> OutEntry = MakeShared<FJsonObject>();
		OutEntry->SetStringField(TEXT("captureFilter"), Categories.Num() == 0 ? TEXT("all") : TEXT("custom"));
		OutEntry->SetArrayField(TEXT("categories"), CatArr);
		OutEntry->SetStringField(TEXT("note"),
			Categories.Num() == 0
				? TEXT("Capturing ALL log categories (Warning/Error always captured)")
				: FString::Printf(TEXT("Capturing %d log category filters (Warning/Error always captured)"), Categories.Num()));
		OutEntries.Add(MakeShared<FJsonValueObject>(OutEntry));
	
	});
}

REGISTER_MCP_CAPABILITY(FSetLogCaptureFilterCapability)

#endif // WITH_EDITOR
