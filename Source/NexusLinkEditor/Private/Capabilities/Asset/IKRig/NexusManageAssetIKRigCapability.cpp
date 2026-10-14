// Copyright byteyang. All Rights Reserved.

#include "Capabilities/Asset/IKRig/NexusManageAssetIKRigCapability.h"

#if WITH_IK_RIG

#include "Utils/NexusArgs.h"
#include "NexusCapabilityRegistry.h"
#include "NexusMcpSchemaBuilder.h"
#include "Utils/NexusAssetUtils.h"
#include "Utils/NexusVersionCompat.h"
#if NX_UE_HAS_IK_RIG_RIG_SUBDIR
#include "Rig/IKRigDefinition.h"
#else
#include "IKRigDefinition.h"
#endif
#include "Engine/SkeletalMesh.h"
#if NX_UE_HAS_IK_RIG_SOLVER_STRUCTS
#include "Rig/Solvers/IKRigSolverBase.h"
#elif NX_UE_HAS_IK_RIG_RIG_SUBDIR
#include "Rig/Solvers/IKRigSolver.h"
#else
#include "IKRigSolver.h"
#endif
#if WITH_EDITOR
#include "RigEditor/IKRigController.h"
#endif
#include "NexusMcpTool.h"

void FNexusManageAssetIKRigCapability::BuildDefinition(FNexusCapabilityDefinition& Out) const
{
	Out.Name = TEXT("ManageAssetIKRig");
	Out.SearchAssetTypes = {TEXT("IKRig"), TEXT("IKRigDefinition")};
	Out.Description = TEXT("Edit IKRig: preview mesh/solver/chain/goal.");
	TSharedPtr<FJsonObject> OpSchema = FNexusSchema::Object()
		.Required(TEXT("Action"), FNexusSchema::Enum(TEXT("Action"),
			{ TEXT("SetPreviewMesh"), TEXT("SetSolverEnabled"),
			  TEXT("AddChain"), TEXT("RemoveChain"), TEXT("SetGoal") }))
		.Prop(TEXT("MeshPath"),       FNexusSchema::Str(TEXT("SkeletalMesh path (SetPreviewMesh)")))
		.Prop(TEXT("SolverIndex"),    FNexusSchema::Int(TEXT("Solver index (SetSolverEnabled)")))
		.Prop(TEXT("bEnabled"),        FNexusSchema::Bool(TEXT("Enabled flag (SetSolverEnabled)")))
		.Prop(TEXT("ChainName"),      FNexusSchema::Str(TEXT("Chain name")))
		.Prop(TEXT("StartBone"),      FNexusSchema::Str(TEXT("AddChain start bone")))
		.Prop(TEXT("EndBone"),        FNexusSchema::Str(TEXT("AddChain end bone")))
		.Prop(TEXT("GoalName"),       FNexusSchema::Str(TEXT("SetGoal target name")))
		.Build();
	Out.InputSchema = FNexusSchema::Object()
		.Required(TEXT("AssetPath"),  FNexusSchema::Str(TEXT("IKRig asset path")))
		.Required(TEXT("Operations"), FNexusSchema::ArrayOf(TEXT("Operation list"), OpSchema.ToSharedRef()))
		.Build();
	Out.Tags = { FNexusMcpTags::Write, FNexusMcpTags::Editor };
	Out.ExtraSearchKeywords = { TEXT("ikrig"), TEXT("ik"), TEXT("solver"), TEXT("preview mesh") };
	Out.RelatedCapabilities = { TEXT("GetAssetIKRig"), TEXT("CreateAssetIKRig"), TEXT("CreateAssetIKRetargeter") };
	Out.WhenToUse = TEXT("Edit IKRig properties; persist with SaveAsset after changes");
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
	if (!IKRig)
	{
		Ctx.Entry->SetStringField(TEXT("Error"), TEXT("IKRigDefinition is null"));
		return;
	}
	const FString MeshPath = FNexusArgs(Op).Str(TEXT("MeshPath"));
	if (MeshPath.IsEmpty())
	{
		Ctx.Entry->SetStringField(TEXT("Error"), TEXT("SetPreviewMesh requires MeshPath"));
		return;
	}
	USkeletalMesh* Mesh = FNexusAssetUtils::LoadAssetWithFallback<USkeletalMesh>(MeshPath);
	if (!Mesh)
	{
		Ctx.Entry->SetStringField(TEXT("Error"),
			FString::Printf(TEXT("SkeletalMesh not found: %s"), *MeshPath));
		return;
	}
	IKRig->SetPreviewMesh(Mesh, true);
	MarkIKDirty(Ctx);
	Ctx.Entry->SetStringField(TEXT("MeshPath"), MeshPath);
}

#if WITH_EDITOR
static UIKRigController* RequireIKController(FNexusActionContext& Ctx)
{
#if NX_UE_HAS_IK_RIG_CONTROLLER_GET_CONTROLLER
	UIKRigController* Ctrl = UIKRigController::GetController(IKFrom(Ctx));
#else
	UIKRigController* Ctrl = UIKRigController::GetIKRigController(IKFrom(Ctx));
#endif
	if (!Ctrl)
	{
		Ctx.Entry->SetStringField(TEXT("Error"), TEXT("Unable to get IKRigController"));
	}
	return Ctrl;
}
#endif

static void HandleIK_SetSolverEnabled(const TSharedPtr<FJsonObject>& Op, FNexusActionContext& Ctx)
{
	int32 SolverIdx = -1;
	if (Op->HasField(TEXT("SolverIndex")))
		SolverIdx = static_cast<int32>(Op->GetNumberField(TEXT("SolverIndex")));
	bool bEnabled = true;
	if (Op->HasField(TEXT("bEnabled")))
		Op->TryGetBoolField(TEXT("bEnabled"), bEnabled);
#if WITH_EDITOR
#if NX_UE_HAS_IK_RIG_SOLVER_STRUCTS
	UIKRigController* Ctrl = RequireIKController(Ctx);
	if (!Ctrl) return;
	if (!Ctrl->SetSolverEnabled(SolverIdx, bEnabled))
	{
		Ctx.Entry->SetStringField(TEXT("Error"), TEXT("SolverIndex out of bounds"));
		return;
	}
#else
	UIKRigDefinition* IKRig = IKFrom(Ctx);
	if (!IKRig)
	{
		Ctx.Entry->SetStringField(TEXT("Error"), TEXT("IKRigDefinition is null"));
		return;
	}
	const TArray<UIKRigSolver*>& Solvers = IKRig->GetSolverArray();
	if (!Solvers.IsValidIndex(SolverIdx))
	{
		Ctx.Entry->SetStringField(TEXT("Error"), TEXT("SolverIndex out of bounds"));
		return;
	}
	Solvers[SolverIdx]->SetEnabled(bEnabled);
#endif
	MarkIKDirty(Ctx);
	Ctx.Entry->SetNumberField(TEXT("SolverIndex"), SolverIdx);
	Ctx.Entry->SetBoolField(TEXT("bEnabled"), bEnabled);
#else
	(void)SolverIdx;
	(void)bEnabled;
	Ctx.Entry->SetStringField(TEXT("Error"), TEXT("SetSolverEnabled editor only"));
#endif
}

static void HandleIK_AddChain(const TSharedPtr<FJsonObject>& Op, FNexusActionContext& Ctx)
{
#if WITH_EDITOR
	UIKRigController* Ctrl = RequireIKController(Ctx);
	if (!Ctrl) return;
	const FNexusArgs A(Op);
	const FString ChainName = A.Str(TEXT("ChainName"));
	if (ChainName.IsEmpty())
	{
		Ctx.Entry->SetStringField(TEXT("Error"), TEXT("ChainName required"));
		return;
	}
#if NX_UE_HAS_IK_RIG_ADD_CHAIN_GOAL
	Ctrl->AddRetargetChain(FName(*ChainName), FName(*A.Str(TEXT("StartBone"))), FName(*A.Str(TEXT("EndBone"))), NAME_None);
#elif NX_UE_HAS_IK_RIG_ADD_CHAIN_BONE_CHAIN_ONLY
	Ctrl->AddRetargetChain(FBoneChain(FName(*ChainName), FName(*A.Str(TEXT("StartBone"))), FName(*A.Str(TEXT("EndBone")))));
#else
	Ctrl->AddRetargetChain(FName(*ChainName), FName(*A.Str(TEXT("StartBone"))), FName(*A.Str(TEXT("EndBone"))));
#endif
	MarkIKDirty(Ctx);
	Ctx.Entry->SetStringField(TEXT("ChainName"), ChainName);
#else
	(void)Op;
	Ctx.Entry->SetStringField(TEXT("Error"), TEXT("chain/goal editor only"));
#endif
}

static void HandleIK_RemoveChain(const TSharedPtr<FJsonObject>& Op, FNexusActionContext& Ctx)
{
#if WITH_EDITOR
	UIKRigController* Ctrl = RequireIKController(Ctx);
	if (!Ctrl) return;
	const FString ChainName = FNexusArgs(Op).Str(TEXT("ChainName"));
	if (ChainName.IsEmpty())
	{
		Ctx.Entry->SetStringField(TEXT("Error"), TEXT("ChainName required"));
		return;
	}
	Ctrl->RemoveRetargetChain(FName(*ChainName));
	MarkIKDirty(Ctx);
#else
	(void)Op;
	Ctx.Entry->SetStringField(TEXT("Error"), TEXT("chain/goal editor only"));
#endif
}

static void HandleIK_SetGoal(const TSharedPtr<FJsonObject>& Op, FNexusActionContext& Ctx)
{
#if WITH_EDITOR
	UIKRigController* Ctrl = RequireIKController(Ctx);
	if (!Ctrl) return;
	const FNexusArgs A(Op);
	const FString ChainName = A.Str(TEXT("ChainName"));
	if (ChainName.IsEmpty())
	{
		Ctx.Entry->SetStringField(TEXT("Error"), TEXT("ChainName required"));
		return;
	}
	Ctrl->SetRetargetChainGoal(FName(*ChainName), FName(*A.Str(TEXT("GoalName"))));
	MarkIKDirty(Ctx);
#else
	(void)Op;
	Ctx.Entry->SetStringField(TEXT("Error"), TEXT("chain/goal editor only"));
#endif
}

bool FNexusManageAssetIKRigCapability::PrepareTarget(
	const TSharedPtr<FJsonObject>& Args,
	TSharedPtr<FJsonObject>& Entry,
	void*& OutTarget,
	FString& OutError) const
{
	const FString AssetPath = FNexusArgs(Args).Str(TEXT("AssetPath"));
	Entry->SetStringField(TEXT("Path"), AssetPath);
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

void FNexusManageAssetIKRigCapability::AfterPrepareTarget(
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

void FNexusManageAssetIKRigCapability::FinalizeTarget(void* Target) const
{
	FIKRigActionState* State = static_cast<FIKRigActionState*>(Target);
	if (!State) return;
	if (State->bDirty && State->IKRig)
	{
		State->IKRig->MarkPackageDirty();
		if (State->OutTop.IsValid())
		{
			State->OutTop->SetStringField(TEXT("Note"), TEXT("Modified; persist with SaveAsset"));
		}
	}
	delete State;
}

void FNexusManageAssetIKRigCapability::RegisterActions(TMap<FString, FNexusActionHandler>& OutHandlers) const
{
	OutHandlers.Add(TEXT("SetPreviewMesh"),   &HandleIK_SetPreviewMesh);
	OutHandlers.Add(TEXT("SetSolverEnabled"), &HandleIK_SetSolverEnabled);
	OutHandlers.Add(TEXT("AddChain"),          &HandleIK_AddChain);
	OutHandlers.Add(TEXT("RemoveChain"),       &HandleIK_RemoveChain);
	OutHandlers.Add(TEXT("SetGoal"),           &HandleIK_SetGoal);
}

REGISTER_MCP_CAPABILITY(FNexusManageAssetIKRigCapability)

#endif // WITH_IK_RIG
