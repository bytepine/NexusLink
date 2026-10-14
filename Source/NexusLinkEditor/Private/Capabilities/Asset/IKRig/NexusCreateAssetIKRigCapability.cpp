// Copyright byteyang. All Rights Reserved.

#include "Capabilities/Asset/IKRig/NexusCreateAssetIKRigCapability.h"

#if WITH_IK_RIG

#include "Utils/NexusCapabilityResultBuilder.h"
#include "Utils/NexusArgs.h"
#include "NexusCapabilityRegistry.h"
#include "NexusMcpSchemaBuilder.h"
#include "Utils/NexusAssetUtils.h"
#include "Utils/NexusVersionCompat.h"
#if NX_UE_HAS_IK_RIG_RIG_SUBDIR
#include "Rig/IKRigDefinition.h"
#else
#include "IKRigDefinition.h"
#endif
#include "Engine/SkeletalMesh.h"
#include "NexusMcpTool.h"

void FNexusCreateAssetIKRigCapability::BuildDefinition(FNexusCapabilityDefinition& Out) const
{
	Out.Name = TEXT("CreateAssetIKRig");
	Out.Description = TEXT("Create empty IKRig; optional preview SkeletalMesh.");
	Out.InputSchema = FNexusSchema::Object()
		.Prop(TEXT("AssetPath"),  FNexusSchema::Str(TEXT("Asset path (package path)")))
		.Prop(TEXT("MeshPath"),   FNexusSchema::Str(TEXT("Optional preview SkeletalMesh path")))
		.Required({ TEXT("AssetPath") })
		.Build();
	Out.Tags = { FNexusMcpTags::Write, FNexusMcpTags::Editor };
	Out.ExtraSearchKeywords = { TEXT("ikrig"), TEXT("ik"), TEXT("new"), TEXT("create"), TEXT("retarget") };
	Out.RelatedCapabilities = { TEXT("GetAssetIKRig"), TEXT("ManageAssetIKRig"), TEXT("GetAssetIKRetargeter") };
	Out.WhenToUse = TEXT("Create empty IKRig definition; UE5.0+ only");
}

FCapabilityResult FNexusCreateAssetIKRigCapability::Execute(const TSharedPtr<FJsonObject>& Arguments) const
{
	return FNexusCapabilityResultBuilder::Build([&](auto& OutEntries, auto& OutTop, auto& OutError)
	{
		const FNexusArgs A(Arguments);
		FString AssetPath;
		AssetPath = A.Str(TEXT("AssetPath"));

		const FNexusAssetUtils::FAssetCreateOutcome Created =
			FNexusAssetUtils::CreatePlainAsset<UIKRigDefinition>(AssetPath, RF_Public | RF_Standalone, false);
		if (!Created.Ok())
		{
			FNexusCapabilityResultBuilder::AddEntryError(OutEntries, Created.Error);
			return;
		}
		UIKRigDefinition* IKRig = Cast<UIKRigDefinition>(Created.Asset);
		if (!IKRig)
		{
			FNexusCapabilityResultBuilder::AddEntryError(OutEntries, TEXT("Create failed"));
			return;
		}
		FString MeshPath;
		if (Arguments->TryGetStringField(TEXT("MeshPath"), MeshPath) && !MeshPath.IsEmpty())
		{
			USkeletalMesh* Mesh = FNexusAssetUtils::LoadAssetWithFallback<USkeletalMesh>(MeshPath);
			if (Mesh) IKRig->SetPreviewMesh(Mesh, false);
		}

		FNexusAssetUtils::NotifyAndSaveCreated(IKRig->GetOutermost(), IKRig, AssetPath);

		TSharedPtr<FJsonObject> Entry = FNexusCapabilityResultBuilder::MakeCreatedEntry(IKRig);
		Entry->SetStringField(TEXT("AssetType"), TEXT("IKRigDefinition"));
		OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
	});
}

REGISTER_MCP_CAPABILITY(FNexusCreateAssetIKRigCapability)

#endif // WITH_IK_RIG
