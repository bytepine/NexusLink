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

void FNexusGetAssetIKRetargeterCapability::BuildDefinition(FNexusCapabilityDefinition& Out) const
{
	Out.Name = TEXT("GetAssetIKRetargeter");
	Out.SearchAssetTypes = {TEXT("IKRetargeter")};
	Out.Description = TEXT("Read IKRetargeter: source/target IKRig, chain mappings. Use ManageAssetIKRetargeter for writes.");
	Out.InputSchema = FNexusSchema::Object()
		.Prop(TEXT("AssetPath"), FNexusSchema::Str(TEXT("IKRetargeter asset path")))
		.Required({ TEXT("AssetPath") })
		.Build();
	Out.Tags = { FNexusMcpTags::Readonly, FNexusMcpTags::Editor };
	Out.ExtraSearchKeywords = { TEXT("ikretargeter"), TEXT("retarget"), TEXT("chain mapping"), TEXT("ik") };
	Out.RelatedCapabilities = { TEXT("ManageAssetIKRetargeter"), TEXT("GetAssetIKRig"), TEXT("CreateAssetIKRetargeter") };
	Out.WhenToUse = TEXT("Read IKRetargeter config; use ManageAssetIKRetargeter for writes");
}

FCapabilityResult FNexusGetAssetIKRetargeterCapability::Execute(const TSharedPtr<FJsonObject>& Arguments) const
{
	return FNexusCapabilityResultBuilder::Build([&](auto& OutEntries, auto& OutTop, auto& OutError)
	{
		FString AssetPath;
		if (!FNexusCapability::RequireString(Arguments, TEXT("AssetPath"), AssetPath, OutEntries, {})) return;

		UIKRetargeter* Retargeter = FNexusAssetUtils::LoadAssetWithFallback<UIKRetargeter>(AssetPath);
		if (!Retargeter)
		{
			FNexusCapability::EmitError(OutEntries, {{TEXT("Path"), AssetPath}},
				FString::Printf(TEXT("IKRetargeter not found: %s"), *AssetPath));
			return;
		}

		TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
		Entry->SetStringField(TEXT("Path"), AssetPath);
		Entry->SetStringField(TEXT("name"),      Retargeter->GetName());
		Entry->SetStringField(TEXT("AssetType"), TEXT("IKRetargeter"));

#if NX_UE_HAS_IK_RETARGETER_GET_IKRIG
		const UIKRigDefinition* SrcRig = Retargeter->GetIKRig(ERetargetSourceOrTarget::Source);
		const UIKRigDefinition* TgtRig = Retargeter->GetIKRig(ERetargetSourceOrTarget::Target);
#else
		const UIKRigDefinition* SrcRig = Retargeter->GetSourceIKRig();
		const UIKRigDefinition* TgtRig = Retargeter->GetTargetIKRig();
#endif
		if (SrcRig) Entry->SetStringField(TEXT("SourceIKRig"), SrcRig->GetPathName());
		if (TgtRig) Entry->SetStringField(TEXT("TargetIKRig"), TgtRig->GetPathName());

		// Chain Mapping
		TArray<TSharedPtr<FJsonValue>> ChainsArr;
#if NX_UE_HAS_IK_RETARGETER_CHAIN_MAPPING
		PRAGMA_DISABLE_DEPRECATION_WARNINGS
		const TArray<FRetargetChainPair>& Pairs = Retargeter->GetChainMapping().GetChainPairs();
		for (const FRetargetChainPair& Pair : Pairs)
		{
			TSharedPtr<FJsonObject> CObj = MakeShared<FJsonObject>();
			CObj->SetStringField(TEXT("SourceChain"), Pair.SourceChainName.ToString());
			CObj->SetStringField(TEXT("TargetChain"), Pair.TargetChainName.ToString());
			ChainsArr.Add(MakeShared<FJsonValueObject>(CObj));
		}
		Entry->SetArrayField(TEXT("ChainMapping"), ChainsArr);
		Entry->SetNumberField(TEXT("ChainCount"), Pairs.Num());
		PRAGMA_ENABLE_DEPRECATION_WARNINGS
#else
		const TArray<TObjectPtr<URetargetChainSettings>>& AllChains = Retargeter->GetAllChainSettings();
		for (const TObjectPtr<URetargetChainSettings>& CS : AllChains)
		{
			if (!CS) continue;
			TSharedPtr<FJsonObject> CObj = MakeShared<FJsonObject>();
			CObj->SetStringField(TEXT("SourceChain"), CS->SourceChain.ToString());
			CObj->SetStringField(TEXT("TargetChain"), CS->TargetChain.ToString());
			ChainsArr.Add(MakeShared<FJsonValueObject>(CObj));
		}
		Entry->SetArrayField(TEXT("ChainMapping"),  ChainsArr);
		Entry->SetNumberField(TEXT("ChainCount"),   AllChains.Num());
#endif

		OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
	});
}

REGISTER_MCP_CAPABILITY(FNexusGetAssetIKRetargeterCapability)

#endif // WITH_IK_RIG
