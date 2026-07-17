// Copyright byteyang. All Rights Reserved.

#include "Capabilities/Asset/IKRig/NexusGetAssetIKRigCapability.h"

#if WITH_IK_RIG

#include "Utils/NexusCapabilityResultBuilder.h"
#include "NexusCapabilityRegistry.h"
#include "NexusMcpSchemaBuilder.h"
#include "Utils/NexusAssetUtils.h"
#include "Rig/IKRigDefinition.h"
#include "Rig/Solvers/IKRigSolver.h"
#include "NexusMcpTool.h"

void FGetAssetIKRigCapability::BuildDefinition(FNexusCapabilityDefinition& Out) const
{
	Out.Name = TEXT("get_asset_ik_rig");
	Out.SearchAssetTypes = {TEXT("IKRig"), TEXT("IKRigDefinition")};
	Out.Description = TEXT("读取 IKRig 资产：预览网格/Solver 列表/BoneChain 列表。写用 manage_asset_ik_rig。");
	Out.InputSchema = FNexusSchema::Object()
		.Prop(TEXT("assetPath"), FNexusSchema::Str(TEXT("IKRig 资产路径")))
		.Required({ TEXT("assetPath") })
		.Build();
	Out.Tags = { FNexusMcpTags::Readonly, FNexusMcpTags::Editor };
	Out.ExtraSearchKeywords = { TEXT("ikrig"), TEXT("ik"), TEXT("retarget"), TEXT("solver"), TEXT("bone chain") };
	Out.RelatedCapabilities = { TEXT("manage_asset_ik_rig"), TEXT("create_asset_ik_rig"), TEXT("get_asset_ik_retargeter") };
	Out.WhenToUse = TEXT("读取 IKRig 结构概览；写用 manage_asset_ik_rig");
}

FCapabilityResult FGetAssetIKRigCapability::Execute(const TSharedPtr<FJsonObject>& Arguments) const
{
	return FNexusCapabilityResultBuilder::Build([&](auto& OutEntries, auto& OutTop, auto& OutError)
	{
		FString AssetPath;
		if (!FNexusCapability::RequireString(Arguments, TEXT("assetPath"), AssetPath, OutEntries, {})) return;

		UIKRigDefinition* IKRig = FNexusAssetUtils::LoadAssetWithFallback<UIKRigDefinition>(AssetPath);
		if (!IKRig)
		{
			FNexusCapability::EmitError(OutEntries, {{TEXT("assetPath"), AssetPath}},
				FString::Printf(TEXT("IKRig 未找到: %s"), *AssetPath));
			return;
		}

		TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
		Entry->SetStringField(TEXT("assetPath"), AssetPath);
		Entry->SetStringField(TEXT("name"),      IKRig->GetName());
		Entry->SetStringField(TEXT("assetType"), TEXT("IKRigDefinition"));

		if (USkeletalMesh* Mesh = IKRig->GetPreviewMesh())
			Entry->SetStringField(TEXT("previewMesh"), Mesh->GetPathName());

		// Solvers
		TArray<TSharedPtr<FJsonValue>> SolversArr;
		const TArray<UIKRigSolver*>& Solvers = IKRig->GetSolverArray();
		for (int32 i = 0; i < Solvers.Num(); ++i)
		{
			if (!Solvers[i]) continue;
			TSharedPtr<FJsonObject> SObj = MakeShared<FJsonObject>();
			SObj->SetNumberField(TEXT("index"),   i);
			SObj->SetStringField(TEXT("class"),   Solvers[i]->GetClass()->GetName());
			SObj->SetBoolField(TEXT("enabled"),   Solvers[i]->IsEnabled());
			SolversArr.Add(MakeShared<FJsonValueObject>(SObj));
		}
		Entry->SetArrayField(TEXT("solvers"), SolversArr);

		// BoneChains (Retarget Chains)
		TArray<TSharedPtr<FJsonValue>> ChainsArr;
		for (const FBoneChain& Chain : IKRig->GetRetargetChains())
		{
			TSharedPtr<FJsonObject> CObj = MakeShared<FJsonObject>();
			CObj->SetStringField(TEXT("chainName"),  Chain.ChainName.ToString());
			CObj->SetStringField(TEXT("startBone"),  Chain.StartBone.BoneName.ToString());
			CObj->SetStringField(TEXT("endBone"),    Chain.EndBone.BoneName.ToString());
			ChainsArr.Add(MakeShared<FJsonValueObject>(CObj));
		}
		Entry->SetArrayField(TEXT("retargetChains"), ChainsArr);

		OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
	});
}

REGISTER_MCP_CAPABILITY(FGetAssetIKRigCapability)

#endif // WITH_IK_RIG
