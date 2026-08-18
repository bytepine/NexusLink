// Copyright byteyang. All Rights Reserved.

#include "Capabilities/Asset/IKRig/NexusCreateAssetIKRigCapability.h"

#if WITH_IK_RIG

#include "Utils/NexusCapabilityResultBuilder.h"
#include "Utils/NexusArgs.h"
#include "NexusCapabilityRegistry.h"
#include "NexusMcpSchemaBuilder.h"
#include "Utils/NexusAssetUtils.h"
#include "Rig/IKRigDefinition.h"
#include "Engine/SkeletalMesh.h"
#include "NexusMcpTool.h"

void FCreateAssetIKRigCapability::BuildDefinition(FNexusCapabilityDefinition& Out) const
{
	Out.Name = TEXT("create_asset_ik_rig");
	Out.Description = TEXT("Create empty IKRig; optional preview SkeletalMesh.");
	Out.InputSchema = FNexusSchema::Object()
		.Prop(TEXT("assetPath"),  FNexusSchema::Str(TEXT("Asset path (package path)")))
		.Prop(TEXT("meshPath"),   FNexusSchema::Str(TEXT("Optional preview SkeletalMesh path")))
		.Required({ TEXT("assetPath") })
		.Build();
	Out.Tags = { FNexusMcpTags::Write, FNexusMcpTags::Editor };
	Out.ExtraSearchKeywords = { TEXT("ikrig"), TEXT("ik"), TEXT("new"), TEXT("create"), TEXT("retarget") };
	Out.RelatedCapabilities = { TEXT("get_asset_ik_rig"), TEXT("manage_asset_ik_rig"), TEXT("get_asset_ik_retargeter") };
	Out.WhenToUse = TEXT("Create empty IKRig definition; UE5.0+ only");
}

FCapabilityResult FCreateAssetIKRigCapability::Execute(const TSharedPtr<FJsonObject>& Arguments) const
{
	return FNexusCapabilityResultBuilder::Build([&](auto& OutEntries, auto& OutTop, auto& OutError)
	{
		const FNexusArgs A(Arguments);
		FString AssetPath;
		AssetPath = A.Str(TEXT("assetPath"));

		if (LoadObject<UIKRigDefinition>(nullptr, *AssetPath))
		{
			FNexusCapabilityResultBuilder::AddEntryError(OutEntries,
				FString::Printf(TEXT("IKRig already exists: %s"), *AssetPath));
			return;
		}

		UPackage* Package = CreatePackage(*AssetPath);
		if (!Package) { FNexusCapabilityResultBuilder::AddEntryError(OutEntries, TEXT("Failed to create package")); return; }

		const FString AssetName = FPaths::GetBaseFilename(AssetPath);
		UIKRigDefinition* IKRig = NewObject<UIKRigDefinition>(Package, *AssetName, RF_Public | RF_Standalone);
		if (!IKRig) { FNexusCapabilityResultBuilder::AddEntryError(OutEntries, TEXT("IKRig Createfailed")); return; }

		FString MeshPath;
		if (Arguments->TryGetStringField(TEXT("meshPath"), MeshPath) && !MeshPath.IsEmpty())
		{
			USkeletalMesh* Mesh = FNexusAssetUtils::LoadAssetWithFallback<USkeletalMesh>(MeshPath);
			if (Mesh) IKRig->SetPreviewMesh(Mesh, false);
		}

		FNexusAssetUtils::NotifyAndSaveCreated(Package, IKRig, AssetPath);

		TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
		Entry->SetStringField(TEXT("name"),      IKRig->GetName());
		Entry->SetStringField(TEXT("path"),      IKRig->GetPathName());
		Entry->SetStringField(TEXT("assetType"), TEXT("IKRigDefinition"));
		OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
	});
}

REGISTER_MCP_CAPABILITY(FCreateAssetIKRigCapability)

#endif // WITH_IK_RIG
