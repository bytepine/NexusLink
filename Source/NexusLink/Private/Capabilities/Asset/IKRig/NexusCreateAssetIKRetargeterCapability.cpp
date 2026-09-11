// Copyright byteyang. All Rights Reserved.

#include "Capabilities/Asset/IKRig/NexusCreateAssetIKRetargeterCapability.h"

#if WITH_IK_RIG

#include "NexusCapabilityRegistry.h"
#include "NexusMcpSchemaBuilder.h"
#include "Utils/NexusAssetUtils.h"
#include "Utils/NexusCapabilityResultBuilder.h"
#include "Utils/NexusArgs.h"
#include "Utils/NexusVersionCompat.h"
#include "Retargeter/IKRetargeter.h"
#if NX_UE_HAS_IK_RIG_RIG_SUBDIR
#include "Rig/IKRigDefinition.h"
#else
#include "IKRigDefinition.h"
#endif
#if WITH_EDITOR
#include "RetargetEditor/IKRetargeterController.h"
#endif
#include "NexusMcpTool.h"

static void ApplyIKRetargeterRigs(UIKRetargeter* R, UIKRigDefinition* Src, UIKRigDefinition* Tgt)
{
#if WITH_EDITOR
	UIKRetargeterController* Ctrl = UIKRetargeterController::GetController(R);
	if (!Ctrl) return;
	if (Src)
	{
#if NX_UE_HAS_IK_RETARGETER_CONTROLLER_SET_IKRIG
		Ctrl->SetIKRig(ERetargetSourceOrTarget::Source, Src);
#else
		Ctrl->SetSourceIKRig(Src);
#endif
	}
	if (Tgt)
	{
#if NX_UE_HAS_IK_RETARGETER_CONTROLLER_SET_IKRIG
		Ctrl->SetIKRig(ERetargetSourceOrTarget::Target, Tgt);
#elif NX_UE_HAS_IK_RETARGETER_CONTROLLER_SET_TARGET_IKRIG
		Ctrl->SetTargetIKRig(Tgt);
#else
		// 5.1 既无 SetTargetIKRig 也无 SetIKRig，TargetIKRigAsset 为私有：只能跳过
#endif
	}
#else
	(void)R;
	(void)Src;
	(void)Tgt;
#endif
}

void FCreateAssetIKRetargeterCapability::BuildDefinition(FNexusCapabilityDefinition& Out) const
{
	Out.Name = TEXT("create_asset_ik_retargeter");
	Out.Description = TEXT("Create empty IKRetargeter; optional source/target IKRig.");
	Out.InputSchema = FNexusSchema::Object()
		.Prop(TEXT("assetPath"), FNexusSchema::Str(TEXT("Asset package path")))
		.Prop(TEXT("sourceRigPath"), FNexusSchema::Str(TEXT("Source IKRig path")))
		.Prop(TEXT("targetRigPath"), FNexusSchema::Str(TEXT("Target IKRig path")))
		.Required({ TEXT("assetPath") })
		.Build();
	Out.Tags = { FNexusMcpTags::Write, FNexusMcpTags::Editor };
	Out.ExtraSearchKeywords = { TEXT("ikretargeter"), TEXT("retarget"), TEXT("ik") };
	Out.RelatedCapabilities = { TEXT("get_asset_ik_retargeter"), TEXT("manage_asset_ik_retargeter"), TEXT("create_asset_ik_rig") };
	Out.WhenToUse = TEXT("Create IKRetargeter; aligns with create_asset_ik_rig");
}

FCapabilityResult FCreateAssetIKRetargeterCapability::Execute(const TSharedPtr<FJsonObject>& Arguments) const
{
	return FNexusCapabilityResultBuilder::Build([&](auto& OutEntries, auto& OutTop, auto& OutError)
	{
		const FNexusArgs A(Arguments);
		const FString AssetPath = A.Str(TEXT("assetPath"));
		const FNexusAssetUtils::FAssetCreateOutcome Created =
			FNexusAssetUtils::CreatePlainAsset<UIKRetargeter>(AssetPath, RF_Public | RF_Standalone, false);
		if (!Created.Ok())
		{
			FNexusCapabilityResultBuilder::AddEntryError(OutEntries, Created.Error);
			return;
		}
		UIKRetargeter* R = Cast<UIKRetargeter>(Created.Asset);
		if (!R)
		{
			FNexusCapabilityResultBuilder::AddEntryError(OutEntries, TEXT("Create failed"));
			return;
		}
		FString SrcPath, TgtPath;
		SrcPath = FNexusArgs(Arguments).Str(TEXT("sourceRigPath"), SrcPath);
		TgtPath = FNexusArgs(Arguments).Str(TEXT("targetRigPath"), TgtPath);
		if (!SrcPath.IsEmpty())
		{
			if (UIKRigDefinition* Src = FNexusAssetUtils::LoadAssetWithFallback<UIKRigDefinition>(SrcPath))
			{
				ApplyIKRetargeterRigs(R, Src, nullptr);
			}
		}
		if (!TgtPath.IsEmpty())
		{
			if (UIKRigDefinition* Tgt = FNexusAssetUtils::LoadAssetWithFallback<UIKRigDefinition>(TgtPath))
			{
				ApplyIKRetargeterRigs(R, nullptr, Tgt);
			}
		}
		FNexusAssetUtils::NotifyAndSaveCreated(R->GetOutermost(), R, AssetPath);
		TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
		Entry->SetStringField(TEXT("name"), R->GetName());
		Entry->SetStringField(TEXT("path"), R->GetPathName());
		OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
	});
}

REGISTER_MCP_CAPABILITY(FCreateAssetIKRetargeterCapability)

#endif
