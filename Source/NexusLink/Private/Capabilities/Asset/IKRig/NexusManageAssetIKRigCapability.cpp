// Copyright byteyang. All Rights Reserved.

#include "Capabilities/Asset/IKRig/NexusManageAssetIKRigCapability.h"

#if WITH_IK_RIG

#include "Utils/NexusCapabilityResultBuilder.h"
#include "Utils/NexusJsonUtils.h"
#include "NexusCapabilityRegistry.h"
#include "NexusMcpSchemaBuilder.h"
#include "Utils/NexusAssetUtils.h"
#include "Rig/IKRigDefinition.h"
#include "Rig/Solvers/IKRigSolver.h"
#include "Rig/IKRigDefinition.h"
#include "Engine/SkeletalMesh.h"
#if WITH_EDITOR
#include "RigEditor/IKRigController.h"
#endif
#include "NexusMcpTool.h"

void FManageAssetIKRigCapability::BuildDefinition(FNexusCapabilityDefinition& Out) const
{
	Out.Name = TEXT("manage_asset_ik_rig");
	Out.SearchAssetTypes = {TEXT("IKRig"), TEXT("IKRigDefinition")};
	Out.Description = TEXT("Edit IKRig: preview mesh/solver/chain/goal.");
	TSharedPtr<FJsonObject> OpSchema = FNexusSchema::Object()
		.Required(TEXT("action"), FNexusSchema::Enum(TEXT("Action"),
			{ TEXT("set_preview_mesh"), TEXT("set_solver_enabled"),
			  TEXT("add_chain"), TEXT("remove_chain"), TEXT("set_goal") }))
		.Prop(TEXT("meshPath"),       FNexusSchema::Str(TEXT("SkeletalMesh path (set_preview_mesh)")))
		.Prop(TEXT("solverIndex"),    FNexusSchema::Int(TEXT("Solver index (set_solver_enabled)")))
		.Prop(TEXT("enabled"),        FNexusSchema::Bool(TEXT("Enabled flag (set_solver_enabled)")))
		.Prop(TEXT("chainName"),      FNexusSchema::Str(TEXT("Chain name")))
		.Prop(TEXT("startBone"),      FNexusSchema::Str(TEXT("add_chain start bone")))
		.Prop(TEXT("endBone"),        FNexusSchema::Str(TEXT("add_chain end bone")))
		.Prop(TEXT("goalName"),       FNexusSchema::Str(TEXT("set_goal target name")))
		.Build();
	Out.InputSchema = FNexusSchema::Object()
		.Required(TEXT("assetPath"),  FNexusSchema::Str(TEXT("IKRig asset path")))
		.Required(TEXT("operations"), FNexusSchema::ArrayOf(TEXT("Operation list"), OpSchema.ToSharedRef()))
		.Build();
	Out.Tags = { FNexusMcpTags::Write, FNexusMcpTags::Editor };
	Out.ExtraSearchKeywords = { TEXT("ikrig"), TEXT("ik"), TEXT("solver"), TEXT("preview mesh") };
	Out.RelatedCapabilities = { TEXT("get_asset_ik_rig"), TEXT("create_asset_ik_rig"), TEXT("create_asset_ik_retargeter") };
	Out.WhenToUse = TEXT("Edit IKRig properties; persist with save_asset after changes");
}

FCapabilityResult FManageAssetIKRigCapability::Execute(const TSharedPtr<FJsonObject>& Arguments) const
{
	return FNexusCapabilityResultBuilder::Build([&](auto& OutEntries, auto& OutTop, auto& OutError)
	{
		FString AssetPath;
		if (!FNexusCapability::RequireString(Arguments, TEXT("assetPath"), AssetPath, OutEntries, {})) return;

		UIKRigDefinition* IKRig = FNexusAssetUtils::LoadAssetWithFallback<UIKRigDefinition>(AssetPath);
		if (!IKRig)
		{
			FNexusCapability::EmitError(OutEntries, {{TEXT("path"), AssetPath}},
				FString::Printf(TEXT("IKRig not found: %s"), *AssetPath));
			return;
		}

		const TArray<TSharedPtr<FJsonValue>> OpsArr = FNexusJsonUtils::ExtractOperations(Arguments);
		if (OpsArr.Num() == 0)
		{
			FNexusCapability::EmitError(OutEntries, {{TEXT("path"), AssetPath}}, TEXT("Missing operations array"));
			return;
		}

		bool bDirty = false;
		for (const TSharedPtr<FJsonValue>& OpVal : OpsArr)
		{
			const TSharedPtr<FJsonObject>* OpObjPtr = nullptr;
			if (!OpVal.IsValid() || !OpVal->TryGetObject(OpObjPtr) || !OpObjPtr) continue;
			const TSharedPtr<FJsonObject>& Op = *OpObjPtr;
			FString Action;
			Op->TryGetStringField(TEXT("action"), Action);
			TSharedPtr<FJsonObject> ResEntry = MakeShared<FJsonObject>();
			ResEntry->SetStringField(TEXT("path"), AssetPath);
			ResEntry->SetStringField(TEXT("action"), Action);

			if (Action.Equals(TEXT("set_preview_mesh"), ESearchCase::IgnoreCase))
			{
				FString MeshPath;
				Op->TryGetStringField(TEXT("meshPath"), MeshPath);
				if (MeshPath.IsEmpty())
				{
					ResEntry->SetStringField(TEXT("error"), TEXT("set_preview_mesh requires meshPath"));
					OutEntries.Add(MakeShared<FJsonValueObject>(ResEntry)); continue;
				}
				USkeletalMesh* Mesh = FNexusAssetUtils::LoadAssetWithFallback<USkeletalMesh>(MeshPath);
				if (!Mesh)
				{
					ResEntry->SetStringField(TEXT("error"),
						FString::Printf(TEXT("SkeletalMesh not found: %s"), *MeshPath));
					OutEntries.Add(MakeShared<FJsonValueObject>(ResEntry)); continue;
				}
				IKRig->SetPreviewMesh(Mesh, true);
				bDirty = true;
				ResEntry->SetStringField(TEXT("meshPath"), MeshPath);
			}
			else if (Action.Equals(TEXT("set_solver_enabled"), ESearchCase::IgnoreCase))
			{
				int32 SolverIdx = -1;
				if (Op->HasField(TEXT("solverIndex")))
					SolverIdx = static_cast<int32>(Op->GetNumberField(TEXT("solverIndex")));
				bool bEnabled = true;
				if (Op->HasField(TEXT("enabled")))
					Op->TryGetBoolField(TEXT("enabled"), bEnabled);
				// mutable access via GetSolverArray (const) -- need cast
				const TArray<UIKRigSolver*>& Solvers = IKRig->GetSolverArray();
				if (!Solvers.IsValidIndex(SolverIdx))
				{
					ResEntry->SetStringField(TEXT("error"), TEXT("solverIndex out of bounds"));
					OutEntries.Add(MakeShared<FJsonValueObject>(ResEntry)); continue;
				}
				// IKRig asset's solvers are read-only; modification requires IKRigController API
				// but IKRigController is editor-only (IKRigDeveloper module)
#if WITH_EDITOR
				Solvers[SolverIdx]->SetEnabled(bEnabled);
				bDirty = true;
				ResEntry->SetNumberField(TEXT("solverIndex"), SolverIdx);
				ResEntry->SetBoolField(TEXT("enabled"), bEnabled);
#else
				ResEntry->SetStringField(TEXT("error"), TEXT("set_solver_enabled editor only"));
#endif
			}
			else if (Action.Equals(TEXT("add_chain"), ESearchCase::IgnoreCase)
				|| Action.Equals(TEXT("remove_chain"), ESearchCase::IgnoreCase)
				|| Action.Equals(TEXT("set_goal"), ESearchCase::IgnoreCase))
			{
#if WITH_EDITOR
				UIKRigController* Ctrl = UIKRigController::GetController(IKRig);
				if (!Ctrl)
				{
					ResEntry->SetStringField(TEXT("error"), TEXT("Unable to get IKRigController"));
				}
				else
				{
					FString ChainName, StartBone, EndBone, GoalName;
					Op->TryGetStringField(TEXT("chainName"), ChainName);
					Op->TryGetStringField(TEXT("startBone"), StartBone);
					Op->TryGetStringField(TEXT("endBone"), EndBone);
					Op->TryGetStringField(TEXT("goalName"), GoalName);
					if (ChainName.IsEmpty())
					{
						ResEntry->SetStringField(TEXT("error"), TEXT("chainName required"));
					}
					else if (Action.Equals(TEXT("add_chain"), ESearchCase::IgnoreCase))
					{
						Ctrl->AddRetargetChain(FName(*ChainName), FName(*StartBone), FName(*EndBone));
						bDirty = true;
						ResEntry->SetStringField(TEXT("chainName"), ChainName);
					}
					else if (Action.Equals(TEXT("remove_chain"), ESearchCase::IgnoreCase))
					{
						Ctrl->RemoveRetargetChain(FName(*ChainName));
						bDirty = true;
					}
					else
					{
						Ctrl->SetRetargetChainGoal(FName(*ChainName), FName(*GoalName));
						bDirty = true;
					}
				}
#else
				ResEntry->SetStringField(TEXT("error"), TEXT("chain/goal editor only"));
#endif
			}
			else
			{
				ResEntry->SetStringField(TEXT("error"), FString::Printf(TEXT("Unknown action: %s"), *Action));
			}
			OutEntries.Add(MakeShared<FJsonValueObject>(ResEntry));
		}

		if (bDirty)
		{
			IKRig->MarkPackageDirty();
			OutTop->SetStringField(TEXT("note"), TEXT("Modified; persist with save_asset"));
		}
	});
}

REGISTER_MCP_CAPABILITY(FManageAssetIKRigCapability)

#endif // WITH_IK_RIG
