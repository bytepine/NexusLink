// Copyright byteyang. All Rights Reserved.

#include "Capabilities/Asset/IKRig/NexusGetAssetIKRigCapability.h"

#if WITH_IK_RIG

#include "Utils/NexusCapabilityResultBuilder.h"
#include "NexusCapabilityRegistry.h"
#include "NexusMcpSchemaBuilder.h"
#include "Utils/NexusAssetUtils.h"
#include "Utils/NexusVersionCompat.h"
#if NX_UE_HAS_IK_RIG_RIG_SUBDIR
#include "Rig/IKRigDefinition.h"
#else
#include "IKRigDefinition.h"
#endif
#if NX_UE_HAS_IK_RIG_SOLVER_STRUCTS
#include "Rig/Solvers/IKRigSolverBase.h"
#elif NX_UE_HAS_IK_RIG_RIG_SUBDIR
#include "Rig/Solvers/IKRigSolver.h"
#else
#include "IKRigSolver.h"
#endif
#include "NexusMcpTool.h"

void FNexusGetAssetIKRigCapability::BuildDefinition(FNexusCapabilityDefinition& Out) const
{
	Out.Name = TEXT("GetAssetIKRig");
	Out.SearchAssetTypes = {TEXT("IKRig"), TEXT("IKRigDefinition")};
	Out.Description = TEXT("Read IKRig: preview mesh/Solver/BoneChain lists. Use ManageAssetIKRig for writes.");
	Out.InputSchema = FNexusSchema::Object()
		.Prop(TEXT("AssetPath"), FNexusSchema::Str(TEXT("IKRig asset path")))
		.Required({ TEXT("AssetPath") })
		.Build();
	Out.Tags = { FNexusMcpTags::Readonly, FNexusMcpTags::Editor };
	Out.ExtraSearchKeywords = { TEXT("ikrig"), TEXT("ik"), TEXT("retarget"), TEXT("solver"), TEXT("bone chain") };
	Out.RelatedCapabilities = { TEXT("ManageAssetIKRig"), TEXT("CreateAssetIKRig"), TEXT("GetAssetIKRetargeter") };
	Out.WhenToUse = TEXT("Read IKRig structure overview; use ManageAssetIKRig for writes");
}

FCapabilityResult FNexusGetAssetIKRigCapability::Execute(const TSharedPtr<FJsonObject>& Arguments) const
{
	return FNexusCapabilityResultBuilder::Build([&](auto& OutEntries, auto& OutTop, auto& OutError)
	{
		FString AssetPath;
		if (!FNexusCapability::RequireString(Arguments, TEXT("AssetPath"), AssetPath, OutEntries, {})) return;

		UIKRigDefinition* IKRig = FNexusAssetUtils::LoadAssetWithFallback<UIKRigDefinition>(AssetPath);
		if (!IKRig)
		{
			FNexusCapability::EmitError(OutEntries, {{TEXT("Path"), AssetPath}},
				FString::Printf(TEXT("IKRig not found: %s"), *AssetPath));
			return;
		}

		TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
		Entry->SetStringField(TEXT("Path"), AssetPath);
		Entry->SetStringField(TEXT("name"),      IKRig->GetName());
		Entry->SetStringField(TEXT("AssetType"), TEXT("IKRigDefinition"));

		if (USkeletalMesh* Mesh = IKRig->GetPreviewMesh())
			Entry->SetStringField(TEXT("PreviewMesh"), Mesh->GetPathName());

		// Solvers
		TArray<TSharedPtr<FJsonValue>> SolversArr;
#if NX_UE_HAS_IK_RIG_SOLVER_STRUCTS
		const TArray<FInstancedStruct>& SolverStructs = IKRig->GetSolverStructs();
		for (int32 SolverStructIndex = 0; SolverStructIndex < SolverStructs.Num(); ++SolverStructIndex)
		{
			const FInstancedStruct& Inst = SolverStructs[SolverStructIndex];
			if (!Inst.IsValid()) continue;
			TSharedPtr<FJsonObject> SObj = MakeShared<FJsonObject>();
			SObj->SetNumberField(TEXT("Index"), SolverStructIndex);
			const UScriptStruct* ST = Inst.GetScriptStruct();
			SObj->SetStringField(TEXT("Class"), ST ? ST->GetName() : TEXT(""));
			const FIKRigSolverBase& Solver = Inst.Get<FIKRigSolverBase>();
			SObj->SetBoolField(TEXT("bEnabled"), Solver.IsEnabled());
			SolversArr.Add(MakeShared<FJsonValueObject>(SObj));
		}
#else
		const TArray<UIKRigSolver*>& Solvers = IKRig->GetSolverArray();
		for (int32 SolverIndex = 0; SolverIndex < Solvers.Num(); ++SolverIndex)
		{
			if (!Solvers[SolverIndex]) continue;
			TSharedPtr<FJsonObject> SObj = MakeShared<FJsonObject>();
			SObj->SetNumberField(TEXT("Index"),   SolverIndex);
			SObj->SetStringField(TEXT("Class"),   Solvers[SolverIndex]->GetClass()->GetName());
			SObj->SetBoolField(TEXT("bEnabled"),   Solvers[SolverIndex]->IsEnabled());
			SolversArr.Add(MakeShared<FJsonValueObject>(SObj));
		}
#endif
		Entry->SetArrayField(TEXT("Solvers"), SolversArr);

		// BoneChains (Retarget Chains)
		TArray<TSharedPtr<FJsonValue>> ChainsArr;
		for (const FBoneChain& Chain : IKRig->GetRetargetChains())
		{
			TSharedPtr<FJsonObject> CObj = MakeShared<FJsonObject>();
			CObj->SetStringField(TEXT("ChainName"),  Chain.ChainName.ToString());
			CObj->SetStringField(TEXT("StartBone"),  Chain.StartBone.BoneName.ToString());
			CObj->SetStringField(TEXT("EndBone"),    Chain.EndBone.BoneName.ToString());
			ChainsArr.Add(MakeShared<FJsonValueObject>(CObj));
		}
		Entry->SetArrayField(TEXT("RetargetChains"), ChainsArr);

		OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
	});
}

REGISTER_MCP_CAPABILITY(FNexusGetAssetIKRigCapability)

#endif // WITH_IK_RIG
