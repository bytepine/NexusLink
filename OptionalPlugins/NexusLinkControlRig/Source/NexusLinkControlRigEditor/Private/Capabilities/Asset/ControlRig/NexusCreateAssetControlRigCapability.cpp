// Copyright byteyang. All Rights Reserved.

#include "Capabilities/Asset/ControlRig/NexusCreateAssetControlRigCapability.h"

#if WITH_CONTROL_RIG

#if WITH_EDITOR

#include "Utils/NexusCapabilityResultBuilder.h"
#include "Utils/NexusArgs.h"
#include "NexusCapabilityRegistry.h"
#include "NexusMcpSchemaBuilder.h"
#include "Utils/NexusAssetUtils.h"
#include "Utils/NexusAssetEditorUtils.h"
#include "Utils/NexusVersionCompat.h"
#if NX_UE_HAS_CONTROL_RIG_BLUEPRINT_LEGACY_HEADER
#include "ControlRigBlueprintLegacy.h"
#else
#include "ControlRigBlueprint.h"
#endif
#include "ControlRigBlueprintGeneratedClass.h"
#include "ControlRig.h"
#include "NexusMcpTool.h"

void FNexusCreateAssetControlRigCapability::BuildDefinition(FNexusCapabilityDefinition& Out) const
{
	Out.Name = TEXT("CreateAssetControlRig");
	Out.Description = TEXT("Create empty ControlRig Blueprint; add bones/controls via manage.");
	Out.InputSchema = FNexusSchema::Object()
		.Prop(TEXT("AssetPath"), FNexusSchema::Str(TEXT("Asset path (package path)")))
		.Required({ TEXT("AssetPath") })
		.Build();
	Out.Tags = { FNexusMcpTags::Write, FNexusMcpTags::Editor };
	Out.ExtraSearchKeywords = { TEXT("controlrig"), TEXT("rig"), TEXT("new"), TEXT("create") };
	Out.RelatedCapabilities = { TEXT("GetAssetControlRig"), TEXT("ManageAssetControlRig") };
	Out.WhenToUse = TEXT("Create empty ControlRig Blueprint; UE5.0+ only");
}

FCapabilityResult FNexusCreateAssetControlRigCapability::Execute(const TSharedPtr<FJsonObject>& Arguments) const
{
	return FNexusCapabilityResultBuilder::Build([&](auto& OutEntries, auto& OutTop, auto& OutError)
	{
		const FNexusArgs A(Arguments);
		const FString AssetPath = A.Str(TEXT("AssetPath"));
		const FNexusAssetEditorUtils::FAssetCreateOutcome Created = FNexusAssetEditorUtils::CreateBlueprintAsset(
			AssetPath, TEXT("ControlRig"), UControlRig::StaticClass(),
			UControlRigBlueprint::StaticClass(), UControlRigBlueprintGeneratedClass::StaticClass());
		if (!Created.Ok())
		{
			FNexusCapabilityResultBuilder::AddEntryError(OutEntries, Created.Error);
			return;
		}
		UControlRigBlueprint* CRBp = Cast<UControlRigBlueprint>(Created.Asset);
		TSharedPtr<FJsonObject> Entry = FNexusCapabilityResultBuilder::MakeCreatedEntry(CRBp);
		Entry->SetStringField(TEXT("AssetType"), TEXT("ControlRigBlueprint"));
		OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
	});
}

REGISTER_MCP_CAPABILITY(FNexusCreateAssetControlRigCapability)

#endif // WITH_EDITOR
#endif // WITH_CONTROL_RIG
