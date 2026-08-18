// Copyright byteyang. All Rights Reserved.

#include "Capabilities/Asset/ControlRig/NexusCreateAssetControlRigCapability.h"

#if WITH_CONTROL_RIG

#if WITH_EDITOR

#include "Utils/NexusCapabilityResultBuilder.h"
#include "Utils/NexusArgs.h"
#include "NexusCapabilityRegistry.h"
#include "NexusMcpSchemaBuilder.h"
#include "Utils/NexusAssetUtils.h"
#include "ControlRigBlueprint.h"
#include "ControlRigBlueprintGeneratedClass.h"
#include "ControlRig.h"
#include "NexusMcpTool.h"

void FCreateAssetControlRigCapability::BuildDefinition(FNexusCapabilityDefinition& Out) const
{
	Out.Name = TEXT("create_asset_control_rig");
	Out.Description = TEXT("Create empty ControlRig Blueprint; add bones/controls via manage.");
	Out.InputSchema = FNexusSchema::Object()
		.Prop(TEXT("assetPath"), FNexusSchema::Str(TEXT("Asset path (package path)")))
		.Required({ TEXT("assetPath") })
		.Build();
	Out.Tags = { FNexusMcpTags::Write, FNexusMcpTags::Editor };
	Out.ExtraSearchKeywords = { TEXT("controlrig"), TEXT("rig"), TEXT("new"), TEXT("create") };
	Out.RelatedCapabilities = { TEXT("get_asset_control_rig"), TEXT("manage_asset_control_rig") };
	Out.WhenToUse = TEXT("Create empty ControlRig Blueprint; UE5.0+ only");
}

FCapabilityResult FCreateAssetControlRigCapability::Execute(const TSharedPtr<FJsonObject>& Arguments) const
{
	return FNexusCapabilityResultBuilder::Build([&](auto& OutEntries, auto& OutTop, auto& OutError)
	{
		const FNexusArgs A(Arguments);
		const FString AssetPath = A.Str(TEXT("assetPath"));
		const FNexusAssetUtils::FAssetCreateOutcome Created = FNexusAssetUtils::CreateBlueprintAsset(
			AssetPath, TEXT("ControlRig"), UControlRig::StaticClass(),
			UControlRigBlueprint::StaticClass(), UControlRigBlueprintGeneratedClass::StaticClass());
		if (!Created.Ok())
		{
			FNexusCapabilityResultBuilder::AddEntryError(OutEntries, Created.Error);
			return;
		}
		UControlRigBlueprint* CRBp = Cast<UControlRigBlueprint>(Created.Asset);
		TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
		Entry->SetStringField(TEXT("name"),     CRBp->GetName());
		Entry->SetStringField(TEXT("path"),     CRBp->GetPathName());
		Entry->SetStringField(TEXT("assetType"), TEXT("ControlRigBlueprint"));
		OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
	});
}

REGISTER_MCP_CAPABILITY(FCreateAssetControlRigCapability)

#endif // WITH_EDITOR
#endif // WITH_CONTROL_RIG
