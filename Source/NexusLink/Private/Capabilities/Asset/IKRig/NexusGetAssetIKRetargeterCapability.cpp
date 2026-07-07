// Copyright byteyang. All Rights Reserved.

#include "Capabilities/Asset/IKRig/NexusGetAssetIKRetargeterCapability.h"

#if WITH_IK_RIG

#include "Utils/NexusCapabilityResultBuilder.h"
#include "NexusCapabilityRegistry.h"
#include "NexusMcpSchemaBuilder.h"
#include "Utils/NexusAssetUtils.h"
#include "Retargeter/IKRetargeter.h"
#include "Rig/IKRigDefinition.h"
#include "NexusMcpTool.h"

void FGetAssetIKRetargeterCapability::BuildDefinition(FNexusCapabilityDefinition& Out) const
{
	Out.Name = TEXT("get_asset_ik_retargeter");
	Out.Description = TEXT("读取 IKRetargeter：源/目标 IKRig、Chain Mapping 列表。写用 manage_asset_ik_retargeter。");
	Out.InputSchema = FNexusSchema::Object()
		.Prop(TEXT("assetPath"), FNexusSchema::Str(TEXT("IKRetargeter 资产路径")))
		.Required({ TEXT("assetPath") })
		.Build();
	Out.Tags = { FNexusMcpTags::Readonly, FNexusMcpTags::Editor };
	Out.ExtraSearchKeywords = { TEXT("ikretargeter"), TEXT("retarget"), TEXT("chain mapping"), TEXT("ik") };
	Out.RelatedCapabilities = { TEXT("manage_asset_ik_retargeter"), TEXT("get_asset_ik_rig") };
	Out.WhenToUse = TEXT("读取 IKRetargeter 配置；写用 manage_asset_ik_retargeter");
}

FCapabilityResult FGetAssetIKRetargeterCapability::Execute(const TSharedPtr<FJsonObject>& Arguments) const
{
	return FNexusCapabilityResultBuilder::Build([&](auto& OutEntries, auto& OutTop, auto& OutError)
	{
		FString AssetPath;
		if (!FNexusCapability::RequireString(Arguments, TEXT("assetPath"), AssetPath, OutEntries, {})) return;

		UIKRetargeter* Retargeter = FNexusAssetUtils::LoadAssetWithFallback<UIKRetargeter>(AssetPath);
		if (!Retargeter)
		{
			FNexusCapability::EmitError(OutEntries, {{TEXT("assetPath"), AssetPath}},
				FString::Printf(TEXT("IKRetargeter 未找到: %s"), *AssetPath));
			return;
		}

		TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
		Entry->SetStringField(TEXT("assetPath"), AssetPath);
		Entry->SetStringField(TEXT("name"),      Retargeter->GetName());
		Entry->SetStringField(TEXT("assetType"), TEXT("IKRetargeter"));

		const UIKRigDefinition* SrcRig = Retargeter->GetIKRig(ERetargetSourceOrTarget::Source);
		const UIKRigDefinition* TgtRig = Retargeter->GetIKRig(ERetargetSourceOrTarget::Target);
		if (SrcRig) Entry->SetStringField(TEXT("sourceIKRig"), SrcRig->GetPathName());
		if (TgtRig) Entry->SetStringField(TEXT("targetIKRig"), TgtRig->GetPathName());

		// Chain Mapping
		const TArray<TObjectPtr<URetargetChainSettings>>& AllChains = Retargeter->GetAllChainSettings();
		TArray<TSharedPtr<FJsonValue>> ChainsArr;
		for (const TObjectPtr<URetargetChainSettings>& CS : AllChains)
		{
			if (!CS) continue;
			TSharedPtr<FJsonObject> CObj = MakeShared<FJsonObject>();
			CObj->SetStringField(TEXT("sourceChain"), CS->SourceChain.ToString());
			CObj->SetStringField(TEXT("targetChain"), CS->TargetChain.ToString());
			ChainsArr.Add(MakeShared<FJsonValueObject>(CObj));
		}
		Entry->SetArrayField(TEXT("chainMapping"),  ChainsArr);
		Entry->SetNumberField(TEXT("chainCount"),   AllChains.Num());

		OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
	});
}

REGISTER_MCP_CAPABILITY(FGetAssetIKRetargeterCapability)

#endif // WITH_IK_RIG
