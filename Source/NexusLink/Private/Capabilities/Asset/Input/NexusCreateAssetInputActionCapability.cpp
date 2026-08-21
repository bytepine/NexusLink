// Copyright byteyang. All Rights Reserved.

#include "Capabilities/Asset/Input/NexusCreateAssetInputActionCapability.h"

#if WITH_ENHANCED_INPUT

#include "Utils/NexusCapabilityResultBuilder.h"
#include "Utils/NexusArgs.h"
#include "NexusCapabilityRegistry.h"
#include "NexusMcpSchemaBuilder.h"
#include "Utils/NexusAssetUtils.h"
#include "NexusMcpTool.h"
#include "InputAction.h"

void FCreateAssetInputActionCapability::BuildDefinition(FNexusCapabilityDefinition& Out) const
{
	Out.Name = TEXT("create_asset_input_action");
	Out.Description = TEXT("Create empty InputAction; add Trigger/Modifier via manage after valueType.");
	Out.InputSchema = FNexusSchema::Object()
		.Required(TEXT("assetPath"), FNexusSchema::Str(TEXT("Asset package path (/Game/…/IA_Jump)")))
		.Prop(TEXT("valueType"), FNexusSchema::Enum(
			TEXT("Value type"),
			{ TEXT("Boolean"), TEXT("Axis1D"), TEXT("Axis2D"), TEXT("Axis3D") },
			TEXT("Boolean")))
		.Build();
	Out.Tags = { FNexusMcpTags::Write, FNexusMcpTags::Editor };
	Out.ExtraSearchKeywords = { TEXT("input"), TEXT("action"), TEXT("ia"), TEXT("enhanced"), TEXT("trigger"), TEXT("axis") };
	Out.RelatedCapabilities = { TEXT("get_asset_input_action"), TEXT("manage_asset_input_action"), TEXT("create_asset_input_mapping_context") };
	Out.WhenToUse = TEXT("Create InputAction; configure Trigger/Modifier via manage");
}

FCapabilityResult FCreateAssetInputActionCapability::Execute(const TSharedPtr<FJsonObject>& Arguments) const
{
	return FNexusCapabilityResultBuilder::Build([&](auto& OutEntries, auto& OutTop, auto& OutError)
	{
		const FNexusArgs A(Arguments);
		TSharedPtr<FJsonObject> OutEntry = MakeShared<FJsonObject>();

		const FString AssetPath = A.Str(TEXT("assetPath"));
		const FNexusAssetUtils::FAssetCreateOutcome Created =
			FNexusAssetUtils::CreatePlainAsset<UInputAction>(AssetPath, RF_Public | RF_Standalone, false);
		if (!Created.Ok())
		{
			FNexusCapabilityResultBuilder::AddEntryError(OutEntries, Created.Error);
			return;
		}
		UInputAction* IA = Cast<UInputAction>(Created.Asset);

		// 设置 ValueType
		FString ValueTypeStr;
		if (Arguments->TryGetStringField(TEXT("valueType"), ValueTypeStr))
		{
			if (ValueTypeStr == TEXT("Axis1D"))
				IA->ValueType = EInputActionValueType::Axis1D;
			else if (ValueTypeStr == TEXT("Axis2D"))
				IA->ValueType = EInputActionValueType::Axis2D;
			else if (ValueTypeStr == TEXT("Axis3D"))
				IA->ValueType = EInputActionValueType::Axis3D;
			else
				IA->ValueType = EInputActionValueType::Boolean;
		}

		FNexusAssetUtils::NotifyAndSaveCreated(IA->GetOutermost(), IA, AssetPath);

		OutEntry->SetStringField(TEXT("name"), IA->GetName());
		OutEntry->SetStringField(TEXT("path"), FNexusAssetUtils::PackagePathOf(IA));
		OutEntries.Add(MakeShared<FJsonValueObject>(OutEntry));
	});
}

REGISTER_MCP_CAPABILITY(FCreateAssetInputActionCapability)

#endif // WITH_ENHANCED_INPUT
