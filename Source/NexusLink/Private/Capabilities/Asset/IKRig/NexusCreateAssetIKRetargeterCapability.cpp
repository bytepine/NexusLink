// Copyright byteyang. All Rights Reserved.

#include "Capabilities/Asset/IKRig/NexusCreateAssetIKRetargeterCapability.h"

#if WITH_IK_RIG

#include "NexusCapabilityRegistry.h"
#include "NexusMcpSchemaBuilder.h"
#include "Utils/NexusAssetUtils.h"
#include "Utils/NexusCapabilityResultBuilder.h"
#include "Retargeter/IKRetargeter.h"
#include "Rig/IKRigDefinition.h"
#include "NexusMcpTool.h"

void FCreateAssetIKRetargeterCapability::BuildDefinition(FNexusCapabilityDefinition& Out) const
{
	Out.Name = TEXT("create_asset_ik_retargeter");
	Out.Description = TEXT("创建空白 IKRetargeter；可选源/目标 IKRig。");
	Out.InputSchema = FNexusSchema::Object()
		.Prop(TEXT("assetPath"), FNexusSchema::Str(TEXT("资产包路径")))
		.Prop(TEXT("sourceRigPath"), FNexusSchema::Str(TEXT("源 IKRig 路径")))
		.Prop(TEXT("targetRigPath"), FNexusSchema::Str(TEXT("目标 IKRig 路径")))
		.Required({ TEXT("assetPath") })
		.Build();
	Out.Tags = { FNexusMcpTags::Write, FNexusMcpTags::Editor };
	Out.ExtraSearchKeywords = { TEXT("ikretargeter"), TEXT("retarget"), TEXT("ik") };
	Out.RelatedCapabilities = { TEXT("get_asset_ik_retargeter"), TEXT("manage_asset_ik_retargeter"), TEXT("create_asset_ik_rig") };
	Out.WhenToUse = TEXT("新建 IKRetargeter；对齐 create_asset_ik_rig");
}

FCapabilityResult FCreateAssetIKRetargeterCapability::Execute(const TSharedPtr<FJsonObject>& Arguments) const
{
	return FNexusCapabilityResultBuilder::Build([&](auto& OutEntries, auto& OutTop, auto& OutError)
	{
		if (!Arguments.IsValid() || !Arguments->HasField(TEXT("assetPath")))
		{
			OutError = TEXT("缺少 assetPath");
			return;
		}
		const FString AssetPath = Arguments->GetStringField(TEXT("assetPath"));
		if (LoadObject<UIKRetargeter>(nullptr, *AssetPath))
		{
			FNexusCapabilityResultBuilder::AddEntryError(OutEntries,
				FString::Printf(TEXT("IKRetargeter already exists: %s"), *AssetPath));
			return;
		}
		UPackage* Package = CreatePackage(*AssetPath);
		if (!Package) { FNexusCapabilityResultBuilder::AddEntryError(OutEntries, TEXT("创建包失败")); return; }
		const FString AssetName = FPaths::GetBaseFilename(AssetPath);
		UIKRetargeter* R = NewObject<UIKRetargeter>(Package, *AssetName, RF_Public | RF_Standalone);
		if (!R) { FNexusCapabilityResultBuilder::AddEntryError(OutEntries, TEXT("创建失败")); return; }
		FString SrcPath, TgtPath;
		Arguments->TryGetStringField(TEXT("sourceRigPath"), SrcPath);
		Arguments->TryGetStringField(TEXT("targetRigPath"), TgtPath);
		if (!SrcPath.IsEmpty())
		{
			if (UIKRigDefinition* Src = FNexusAssetUtils::LoadAssetWithFallback<UIKRigDefinition>(SrcPath))
			{
				R->SetIKRig(ERetargetSourceOrTarget::Source, Src);
			}
		}
		if (!TgtPath.IsEmpty())
		{
			if (UIKRigDefinition* Tgt = FNexusAssetUtils::LoadAssetWithFallback<UIKRigDefinition>(TgtPath))
			{
				R->SetIKRig(ERetargetSourceOrTarget::Target, Tgt);
			}
		}
		FNexusAssetUtils::NotifyAndSaveCreated(Package, R, AssetPath);
		TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
		Entry->SetStringField(TEXT("name"), R->GetName());
		Entry->SetStringField(TEXT("path"), R->GetPathName());
		OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
	});
}

REGISTER_MCP_CAPABILITY(FCreateAssetIKRetargeterCapability)

#endif
