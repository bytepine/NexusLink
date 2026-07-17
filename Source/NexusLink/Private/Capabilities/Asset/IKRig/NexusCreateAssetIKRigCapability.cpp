// Copyright byteyang. All Rights Reserved.

#include "Capabilities/Asset/IKRig/NexusCreateAssetIKRigCapability.h"

#if WITH_IK_RIG

#include "Utils/NexusCapabilityResultBuilder.h"
#include "NexusCapabilityRegistry.h"
#include "NexusMcpSchemaBuilder.h"
#include "Utils/NexusAssetUtils.h"
#include "Rig/IKRigDefinition.h"
#include "Engine/SkeletalMesh.h"
#include "NexusMcpTool.h"

void FCreateAssetIKRigCapability::BuildDefinition(FNexusCapabilityDefinition& Out) const
{
	Out.Name = TEXT("create_asset_ik_rig");
	Out.Description = TEXT("创建空白 IKRig 资产；可选关联预览 SkeletalMesh。");
	Out.InputSchema = FNexusSchema::Object()
		.Prop(TEXT("assetPath"),  FNexusSchema::Str(TEXT("资产路径（包路径）")))
		.Prop(TEXT("meshPath"),   FNexusSchema::Str(TEXT("可选：预览 SkeletalMesh 路径")))
		.Required({ TEXT("assetPath") })
		.Build();
	Out.Tags = { FNexusMcpTags::Write, FNexusMcpTags::Editor };
	Out.ExtraSearchKeywords = { TEXT("ikrig"), TEXT("ik"), TEXT("new"), TEXT("create"), TEXT("retarget") };
	Out.RelatedCapabilities = { TEXT("get_asset_ik_rig"), TEXT("manage_asset_ik_rig"), TEXT("get_asset_ik_retargeter") };
	Out.WhenToUse = TEXT("创建空白 IKRig 定义；UE5.0+ 专用");
}

FCapabilityResult FCreateAssetIKRigCapability::Execute(const TSharedPtr<FJsonObject>& Arguments) const
{
	return FNexusCapabilityResultBuilder::Build([&](auto& OutEntries, auto& OutTop, auto& OutError)
	{
		FString AssetPath;
		if (!Arguments.IsValid() || !Arguments->HasField(TEXT("assetPath")))
		{
			OutError = TEXT("缺少 assetPath");
			return;
		}
		AssetPath = Arguments->GetStringField(TEXT("assetPath"));

		if (LoadObject<UIKRigDefinition>(nullptr, *AssetPath))
		{
			FNexusCapabilityResultBuilder::AddEntryError(OutEntries,
				FString::Printf(TEXT("IKRig 已存在: %s"), *AssetPath));
			return;
		}

		UPackage* Package = CreatePackage(*AssetPath);
		if (!Package) { FNexusCapabilityResultBuilder::AddEntryError(OutEntries, TEXT("创建包失败")); return; }

		const FString AssetName = FPaths::GetBaseFilename(AssetPath);
		UIKRigDefinition* IKRig = NewObject<UIKRigDefinition>(Package, *AssetName, RF_Public | RF_Standalone);
		if (!IKRig) { FNexusCapabilityResultBuilder::AddEntryError(OutEntries, TEXT("IKRig 创建失败")); return; }

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
