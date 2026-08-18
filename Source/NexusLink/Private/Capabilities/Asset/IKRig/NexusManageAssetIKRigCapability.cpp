// Copyright byteyang. All Rights Reserved.

#include "Capabilities/Asset/IKRig/NexusManageAssetIKRigCapability.h"

#if WITH_IK_RIG

#include "Utils/NexusArgs.h"
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

struct FIKRigActionState
{
	UIKRigDefinition* IKRig = nullptr;
	bool bDirty = false;
	TSharedPtr<FJsonObject> OutTop;
};

static FIKRigActionState* IKState(FNexusActionContext& Ctx)
{
	return static_cast<FIKRigActionState*>(Ctx.Target);
}

static UIKRigDefinition* IKFrom(FNexusActionContext& Ctx)
{
	FIKRigActionState* S = IKState(Ctx);
	return S ? S->IKRig : nullptr;
}

static void MarkIKDirty(FNexusActionContext& Ctx)
{
	if (FIKRigActionState* S = IKState(Ctx))
	{
		S->bDirty = true;
	}
}

static void HandleIK_SetPreviewMesh(const TSharedPtr<FJsonObject>& Op, FNexusActionContext& Ctx)
{
	UIKRigDefinition* IKRig = IKFrom(Ctx);
	const FString MeshPath = FNexusArgs(Op).Str(TEXT("meshPath"));
	if (MeshPath.IsEmpty())
	{
		Ctx.Entry->SetStringField(TEXT("error"), TEXT("set_preview_mesh requires meshPath"));
		return;
	}
	USkeletalMesh* Mesh = FNexusAssetUtils::LoadAssetWithFallback<USkeletalMesh>(MeshPath);
	if (!Mesh)
	{
		Ctx.Entry->SetStringField(TEXT("error"),
			FString::Printf(TEXT("SkeletalMesh not found: %s"), *MeshPath));
		return;
	}
	IKRig->SetPreviewMesh(Mesh, true);
	MarkIKDirty(Ctx);
	Ctx.Entry->SetStringField(TEXT("meshPath"), MeshPath);
}

static void HandleIK_SetSolverEnabled(const TSharedPtr<FJsonObject>& Op, FNexusActionContext& Ctx)
{
	UIKRigDefinition* IKRig = IKFrom(Ctx);
	int32 SolverIdx = -1;
	if (Op->HasField(TEXT("solverIndex")))
		SolverIdx = static_cast<int32>(Op->GetNumberField(TEXT("solverIndex")));
	bool bEnabled = true;
	if (Op->HasField(TEXT("enabled")))
		Op->TryGetBoolField(TEXT("enabled"), bEnabled);
	const TArray<UIKRigSolver*>& Solvers = IKRig->GetSolverArray();
	if (!Solvers.IsValidIndex(SolverIdx))
	{
		Ctx.Entry->SetStringField(TEXT("error"), TEXT("solverIndex out of bounds"));
		return;
	}
#if WITH_EDITOR
	Solvers[SolverIdx]->SetEnabled(bEnabled);
	MarkIKDirty(Ctx);
	Ctx.Entry->SetNumberField(TEXT("solverIndex"), SolverIdx);
	Ctx.Entry->SetBoolField(TEXT("enabled"), bEnabled);
#else
	Ctx.Entry->SetStringField(TEXT("error"), TEXT("set_solver_enabled editor only"));
#endif
}

#if WITH_EDITOR
static UIKRigController* RequireIKController(FNexusActionContext& Ctx)
{
	UIKRigController* Ctrl = UIKRigController::GetController(IKFrom(Ctx));
	if (!Ctrl)
	{
		Ctx.Entry->SetStringField(TEXT("error"), TEXT("Unable to get IKRigController"));
	}
	return Ctrl;
}
#endif

static void HandleIK_AddChain(const TSharedPtr<FJsonObject>& Op, FNexusActionContext& Ctx)
{
#if WITH_EDITOR
	UIKRigController* Ctrl = RequireIKController(Ctx);
	if (!Ctrl) return;
	const FNexusArgs A(Op);
	const FString ChainName = A.Str(TEXT("chainName"));
	if (ChainName.IsEmpty())
	{
		Ctx.Entry->SetStringField(TEXT("error"), TEXT("chainName required"));
		return;
	}
	Ctrl->AddRetargetChain(FName(*ChainName), FName(*A.Str(TEXT("startBone"))), FName(*A.Str(TEXT("endBone"))));
	MarkIKDirty(Ctx);
	Ctx.Entry->SetStringField(TEXT("chainName"), ChainName);
#else
	(void)Op;
	Ctx.Entry->SetStringField(TEXT("error"), TEXT("chain/goal editor only"));
#endif
}

static void HandleIK_RemoveChain(const TSharedPtr<FJsonObject>& Op, FNexusActionContext& Ctx)
{
#if WITH_EDITOR
	UIKRigController* Ctrl = RequireIKController(Ctx);
	if (!Ctrl) return;
	const FString ChainName = FNexusArgs(Op).Str(TEXT("chainName"));
	if (ChainName.IsEmpty())
	{
		Ctx.Entry->SetStringField(TEXT("error"), TEXT("chainName required"));
		return;
	}
	Ctrl->RemoveRetargetChain(FName(*ChainName));
	MarkIKDirty(Ctx);
#else
	(void)Op;
	Ctx.Entry->SetStringField(TEXT("error"), TEXT("chain/goal editor only"));
#endif
}

static void HandleIK_SetGoal(const TSharedPtr<FJsonObject>& Op, FNexusActionContext& Ctx)
{
#if WITH_EDITOR
	UIKRigController* Ctrl = RequireIKController(Ctx);
	if (!Ctrl) return;
	const FNexusArgs A(Op);
	const FString ChainName = A.Str(TEXT("chainName"));
	if (ChainName.IsEmpty())
	{
		Ctx.Entry->SetStringField(TEXT("error"), TEXT("chainName required"));
		return;
	}
	Ctrl->SetRetargetChainGoal(FName(*ChainName), FName(*A.Str(TEXT("goalName"))));
	MarkIKDirty(Ctx);
#else
	(void)Op;
	Ctx.Entry->SetStringField(TEXT("error"), TEXT("chain/goal editor only"));
#endif
}

bool FManageAssetIKRigCapability::PrepareTarget(
	const TSharedPtr<FJsonObject>& Args,
	TSharedPtr<FJsonObject>& Entry,
	void*& OutTarget,
	FString& OutError) const
{
	const FString AssetPath = FNexusArgs(Args).Str(TEXT("assetPath"));
	Entry->SetStringField(TEXT("path"), AssetPath);
	UIKRigDefinition* IKRig = FNexusAssetUtils::LoadAssetWithFallback<UIKRigDefinition>(AssetPath);
	if (!IKRig)
	{
		OutError = FString::Printf(TEXT("IKRig not found: %s"), *AssetPath);
		return false;
	}
	FIKRigActionState* State = new FIKRigActionState();
	State->IKRig = IKRig;
	OutTarget = State;
	return true;
}

void FManageAssetIKRigCapability::AfterPrepareTarget(
	void* Target,
	const TSharedPtr<FJsonObject>& Args,
	TSharedPtr<FJsonObject>& OutTop) const
{
	(void)Args;
	if (FIKRigActionState* State = static_cast<FIKRigActionState*>(Target))
	{
		State->OutTop = OutTop;
	}
}

void FManageAssetIKRigCapability::FinalizeTarget(void* Target) const
{
	FIKRigActionState* State = static_cast<FIKRigActionState*>(Target);
	if (!State) return;
	if (State->bDirty && State->IKRig)
	{
		State->IKRig->MarkPackageDirty();
		if (State->OutTop.IsValid())
		{
			State->OutTop->SetStringField(TEXT("note"), TEXT("Modified; persist with save_asset"));
		}
	}
	delete State;
}

void FManageAssetIKRigCapability::RegisterActions(TMap<FString, FNexusActionHandler>& OutHandlers) const
{
	OutHandlers.Add(TEXT("set_preview_mesh"),   &HandleIK_SetPreviewMesh);
	OutHandlers.Add(TEXT("set_solver_enabled"), &HandleIK_SetSolverEnabled);
	OutHandlers.Add(TEXT("add_chain"),          &HandleIK_AddChain);
	OutHandlers.Add(TEXT("remove_chain"),       &HandleIK_RemoveChain);
	OutHandlers.Add(TEXT("set_goal"),           &HandleIK_SetGoal);
}

REGISTER_MCP_CAPABILITY(FManageAssetIKRigCapability)

#endif // WITH_IK_RIG
