// Copyright byteyang. All Rights Reserved.

#include "Capabilities/Asset/Input/NexusCreateAssetInputMappingContextCapability.h"

#if WITH_ENHANCED_INPUT

#include "Utils/NexusCapabilityResultBuilder.h"
#include "Utils/NexusArgs.h"
#include "NexusCapabilityRegistry.h"
#include "NexusMcpSchemaBuilder.h"
#include "Utils/NexusAssetUtils.h"
#include "NexusMcpTool.h"
#include "InputMappingContext.h"

void FNexusCreateAssetInputMappingContextCapability::BuildDefinition(FNexusCapabilityDefinition& Out) const
{
	Out.Name = TEXT("CreateAssetInputMappingContext");
	Out.Description = TEXT("Create empty InputMappingContext; bind Action-Key via manage.");
	Out.InputSchema = FNexusSchema::Object()
		.Required(TEXT("AssetPath"), FNexusSchema::Str(TEXT("Asset package path (/Game/…/IMC_Default)")))
		.Build();
	Out.Tags = { FNexusMcpTags::Write, FNexusMcpTags::Editor };
	Out.ExtraSearchKeywords = { TEXT("input"), TEXT("mapping"), TEXT("imc"), TEXT("enhanced"), TEXT("keybind"), TEXT("context") };
	Out.RelatedCapabilities = { TEXT("GetAssetInputMappingContext"), TEXT("ManageAssetInputMappingContext"), TEXT("CreateAssetInputAction") };
	Out.WhenToUse = TEXT("Create InputMappingContext; add Action-Key bindings via manage");
}

FCapabilityResult FNexusCreateAssetInputMappingContextCapability::Execute(const TSharedPtr<FJsonObject>& Arguments) const
{
	return FNexusCapabilityResultBuilder::Build([&](auto& OutEntries, auto& OutTop, auto& OutError)
	{
		const FNexusArgs A(Arguments);
		const FString AssetPath = A.Str(TEXT("AssetPath"));
		const FNexusAssetUtils::FAssetCreateOutcome Created =
			FNexusAssetUtils::CreatePlainAsset<UInputMappingContext>(AssetPath);
		if (!Created.Ok())
		{
			FNexusCapabilityResultBuilder::AddEntryError(OutEntries, Created.Error);
			return;
		}
		UInputMappingContext* IMC = Cast<UInputMappingContext>(Created.Asset);
		TSharedPtr<FJsonObject> OutEntry = MakeShared<FJsonObject>();
		OutEntry->SetStringField(TEXT("name"), IMC->GetName());
		OutEntry->SetStringField(TEXT("Path"), FNexusAssetUtils::PackagePathOf(IMC));
		OutEntries.Add(MakeShared<FJsonValueObject>(OutEntry));
	});
}

REGISTER_MCP_CAPABILITY(FNexusCreateAssetInputMappingContextCapability)

#endif // WITH_ENHANCED_INPUT
