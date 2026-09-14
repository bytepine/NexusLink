// Copyright byteyang. All Rights Reserved.

#include "Capabilities/Asset/IKRig/NexusGetAssetIKRetargeterCapability.h"

#if WITH_IK_RIG

#include "Utils/NexusCapabilityResultBuilder.h"
#include "NexusCapabilityRegistry.h"
#include "NexusMcpSchemaBuilder.h"
#include "Utils/NexusAssetUtils.h"
#include "Utils/NexusVersionCompat.h"
#include "Retargeter/IKRetargeter.h"
#if NX_UE_HAS_IK_RIG_RIG_SUBDIR
#include "Rig/IKRigDefinition.h"
#else
#include "IKRigDefinition.h"
#endif
#include "NexusMcpTool.h"

void FGetAssetIKRetargeterCapability::BuildDefinition(FNexusCapabilityDefinition& Out) const
{
	Out.Name = TEXT("get_asset_ik_retargeter");
	Out.SearchAssetTypes = {TEXT("IKRetargeter")};
	Out.Description = TEXT("Read IKRetargeter: source/target IKRig, chain mappings. Use manage_asset_ik_retargeter for writes.");
	Out.InputSchema = FNexusSchema::Object()
		.Prop(TEXT("assetPath"), FNexusSchema::Str(TEXT("IKRetargeter asset path")))
		.Required({ TEXT("assetPath") })
		.Build();
	Out.Tags = { FNexusMcpTags::Readonly, FNexusMcpTags::Editor };
	Out.ExtraSearchKeywords = { TEXT("ikretargeter"), TEXT("retarget"), TEXT("chain mapping"), TEXT("ik") };
	Out.RelatedCapabilities = { TEXT("manage_asset_ik_retargeter"), TEXT("get_asset_ik_rig"), TEXT("create_asset_ik_retargeter") };
	Out.WhenToUse = TEXT("Read IKRetargeter config; use manage_asset_ik_retargeter for writes");
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
			FNexusCapability::EmitError(OutEntries, {{TEXT("path"), AssetPath}},
				FString::Printf(TEXT("IKRetargeter not found: %s"), *AssetPath));
			return;
		}

		TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
		Entry->SetStringField(TEXT("path"), AssetPath);
		Entry->SetStringField(TEXT("name"),      Retargeter->GetName());
		Entry->SetStringField(TEXT("assetType"), TEXT("IKRetargeter"));

#if NX_UE_HAS_IK_RETARGETER_GET_IKRIG
		const UIKRigDefinition* SrcRig = Retargeter->GetIKRig(ERetargetSourceOrTarget::Source);
		const UIKRigDefinition* TgtRig = Retargeter->GetIKRig(ERetargetSourceOrTarget::Target);
#else
		const UIKRigDefinition* SrcRig = Retargeter->GetSourceIKRig();
		const UIKRigDefinition* TgtRig = Retargeter->GetTargetIKRig();
#endif
		if (SrcRig) Entry->SetStringField(TEXT("sourceIKRig"), SrcRig->GetPathName());
		if (TgtRig) Entry->SetStringField(TEXT("targetIKRig"), TgtRig->GetPathName());

		// Chain Mapping
		TArray<TSharedPtr<FJsonValue>> ChainsArr;
#if NX_UE_HAS_IK_RETARGETER_CHAIN_MAPPING
		PRAGMA_DISABLE_DEPRECATION_WARNINGS
		const TArray<FRetargetChainPair>& Pairs = Retargeter->GetChainMapping().GetChainPairs();
		for (const FRetargetChainPair& Pair : Pairs)
		{
			TSharedPtr<FJsonObject> CObj = MakeShared<FJsonObject>();
			CObj->SetStringField(TEXT("sourceChain"), Pair.SourceChainName.ToString());
			CObj->SetStringField(TEXT("targetChain"), Pair.TargetChainName.ToString());
			ChainsArr.Add(MakeShared<FJsonValueObject>(CObj));
		}
		Entry->SetArrayField(TEXT("chainMapping"), ChainsArr);
		Entry->SetNumberField(TEXT("chainCount"), Pairs.Num());
		PRAGMA_ENABLE_DEPRECATION_WARNINGS
#else
		const TArray<TObjectPtr<URetargetChainSettings>>& AllChains = Retargeter->GetAllChainSettings();
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
#endif

		OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
	});
}

REGISTER_MCP_CAPABILITY(FGetAssetIKRetargeterCapability)

#endif // WITH_IK_RIG
