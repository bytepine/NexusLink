// Copyright byteyang. All Rights Reserved.

#include "Capabilities/Asset/IKRig/NexusManageAssetIKRetargeterCapability.h"

#if WITH_IK_RIG

#include "Utils/NexusArgs.h"
#include "NexusCapabilityRegistry.h"
#include "NexusMcpSchemaBuilder.h"
#include "Utils/NexusAssetUtils.h"
#include "Utils/NexusVersionCompat.h"
#include "Retargeter/IKRetargeter.h"
#if NX_UE_HAS_IK_RETARGET_SETTINGS_HEADER
#include "Retargeter/IKRetargetSettings.h"
#endif
#if NX_UE_HAS_IK_RIG_RIG_SUBDIR
#include "Rig/IKRigDefinition.h"
#else
#include "IKRigDefinition.h"
#endif
#if WITH_EDITOR
#include "RetargetEditor/IKRetargeterController.h"
#endif
#include "NexusMcpTool.h"

void FNexusManageAssetIKRetargeterCapability::BuildDefinition(FNexusCapabilityDefinition& Out) const
{
	Out.Name = TEXT("ManageAssetIKRetargeter");
	Out.SearchAssetTypes = {TEXT("IKRetargeter")};
	Out.Description = TEXT("Edit IKRetargeter: SetSourceRig / SetTargetRig / SetChainSource.");
	TSharedPtr<FJsonObject> OpSchema = FNexusSchema::Object()
		.Required(TEXT("Action"), FNexusSchema::Enum(TEXT("Action"),
			{ TEXT("SetSourceRig"), TEXT("SetTargetRig"), TEXT("SetChainSource") }))
		.Prop(TEXT("RigPath"),       FNexusSchema::Str(TEXT("IKRig asset path (SetSource/target_rig)")))
		.Prop(TEXT("TargetChain"),   FNexusSchema::Str(TEXT("Target chain name (SetChainSource)")))
		.Prop(TEXT("SourceChain"),   FNexusSchema::Str(TEXT("Source chain name (SetChainSource)")))
		.Build();
	Out.InputSchema = FNexusSchema::Object()
		.Required(TEXT("AssetPath"),  FNexusSchema::Str(TEXT("IKRetargeter asset path")))
		.Required(TEXT("Operations"), FNexusSchema::ArrayOf(TEXT("Operation list"), OpSchema.ToSharedRef()))
		.Build();
	Out.Tags = { FNexusMcpTags::Write, FNexusMcpTags::Editor };
	Out.ExtraSearchKeywords = { TEXT("ikretargeter"), TEXT("retarget"), TEXT("chain"), TEXT("Source"), TEXT("Target") };
	Out.RelatedCapabilities = { TEXT("GetAssetIKRetargeter"), TEXT("GetAssetIKRig"), TEXT("CreateAssetIKRetargeter") };
	Out.WhenToUse = TEXT("Edit IKRetargeter bindings; persist with SaveAsset after changes");
}

struct FIKRetargeterActionState
{
	UIKRetargeter* Retargeter = nullptr;
	bool bDirty = false;
	TSharedPtr<FJsonObject> OutTop;
};

static FIKRetargeterActionState* IKRState(FNexusActionContext& Ctx)
{
	return static_cast<FIKRetargeterActionState*>(Ctx.Target);
}

static UIKRetargeter* IKRFrom(FNexusActionContext& Ctx)
{
	FIKRetargeterActionState* S = IKRState(Ctx);
	return S ? S->Retargeter : nullptr;
}

static void MarkIKRDirty(FNexusActionContext& Ctx)
{
	if (FIKRetargeterActionState* S = IKRState(Ctx))
	{
		S->bDirty = true;
	}
}

static void HandleIKR_SetRig(const TSharedPtr<FJsonObject>& Op, FNexusActionContext& Ctx, bool bSource)
{
	UIKRetargeter* Retargeter = IKRFrom(Ctx);
	if (!Retargeter)
	{
		Ctx.Entry->SetStringField(TEXT("Error"), TEXT("IKRetargeter is null"));
		return;
	}
	const FString RigPath = FNexusArgs(Op).Str(TEXT("RigPath"));
	if (RigPath.IsEmpty())
	{
		Ctx.Entry->SetStringField(TEXT("Error"), TEXT("RigPath required"));
		return;
	}
	UIKRigDefinition* IKRig = FNexusAssetUtils::LoadAssetWithFallback<UIKRigDefinition>(RigPath);
	if (!IKRig)
	{
		Ctx.Entry->SetStringField(TEXT("Error"),
			FString::Printf(TEXT("IKRig not found: %s"), *RigPath));
		return;
	}
	const FName FieldName = bSource
		? FName(TEXT("SourceIKRigAsset"))
		: FName(TEXT("TargetIKRigAsset"));
	if (FSoftObjectProperty* Prop = FindFProperty<FSoftObjectProperty>(Retargeter->GetClass(), FieldName))
	{
		Prop->SetPropertyValue_InContainer(Retargeter, FSoftObjectPtr(IKRig));
		MarkIKRDirty(Ctx);
		Ctx.Entry->SetStringField(TEXT("RigPath"), RigPath);
	}
	else
	{
		Ctx.Entry->SetStringField(TEXT("Error"), TEXT("Reflection field not found"));
	}
}

static void HandleIKR_SetSourceRig(const TSharedPtr<FJsonObject>& Op, FNexusActionContext& Ctx)
{
	HandleIKR_SetRig(Op, Ctx, true);
}

static void HandleIKR_SetTargetRig(const TSharedPtr<FJsonObject>& Op, FNexusActionContext& Ctx)
{
	HandleIKR_SetRig(Op, Ctx, false);
}

static void HandleIKR_SetChainSource(const TSharedPtr<FJsonObject>& Op, FNexusActionContext& Ctx)
{
	UIKRetargeter* Retargeter = IKRFrom(Ctx);
	if (!Retargeter)
	{
		Ctx.Entry->SetStringField(TEXT("Error"), TEXT("IKRetargeter is null"));
		return;
	}
	const FNexusArgs A(Op);
	const FString TargetChain = A.Str(TEXT("TargetChain"));
	const FString SourceChain = A.Str(TEXT("SourceChain"));
	if (TargetChain.IsEmpty())
	{
		Ctx.Entry->SetStringField(TEXT("Error"), TEXT("SetChainSource requires TargetChain"));
		return;
	}
#if WITH_EDITOR && NX_UE_HAS_IK_RETARGETER_SET_SOURCE_CHAIN
	UIKRetargeterController* Ctrl = UIKRetargeterController::GetController(Retargeter);
	if (!Ctrl)
	{
		Ctx.Entry->SetStringField(TEXT("Error"), TEXT("Unable to get IKRetargeterController"));
		return;
	}
	if (!Ctrl->SetSourceChain(FName(*SourceChain), FName(*TargetChain)))
	{
		Ctx.Entry->SetStringField(TEXT("Error"),
			FString::Printf(TEXT("Chain not found: %s"), *TargetChain));
		return;
	}
#else
#if NX_UE_HAS_IK_RETARGETER_GET_CHAIN_MAP_BY_NAME
	const TObjectPtr<URetargetChainSettings> CS = Retargeter->GetChainMapByName(FName(*TargetChain));
#else
	// 5.0 无 GetChainMapByName，按 TargetChain 遍历 ChainSettings
	TObjectPtr<URetargetChainSettings> CS = nullptr;
	for (const TObjectPtr<URetargetChainSettings>& Candidate : Retargeter->GetAllChainSettings())
	{
		if (Candidate && Candidate->TargetChain == FName(*TargetChain)) { CS = Candidate; break; }
	}
#endif
	if (!CS)
	{
		Ctx.Entry->SetStringField(TEXT("Error"),
			FString::Printf(TEXT("Chain not found: %s"), *TargetChain));
		return;
	}
	CS->SourceChain = FName(*SourceChain);
#endif
	MarkIKRDirty(Ctx);
	Ctx.Entry->SetStringField(TEXT("TargetChain"), TargetChain);
	Ctx.Entry->SetStringField(TEXT("SourceChain"), SourceChain);
}

bool FNexusManageAssetIKRetargeterCapability::PrepareTarget(
	const TSharedPtr<FJsonObject>& Args,
	TSharedPtr<FJsonObject>& Entry,
	void*& OutTarget,
	FString& OutError) const
{
	const FString AssetPath = FNexusArgs(Args).Str(TEXT("AssetPath"));
	Entry->SetStringField(TEXT("Path"), AssetPath);
	UIKRetargeter* Retargeter = FNexusAssetUtils::LoadAssetWithFallback<UIKRetargeter>(AssetPath);
	if (!Retargeter)
	{
		OutError = FString::Printf(TEXT("IKRetargeter not found: %s"), *AssetPath);
		return false;
	}
	FIKRetargeterActionState* State = new FIKRetargeterActionState();
	State->Retargeter = Retargeter;
	OutTarget = State;
	return true;
}

void FNexusManageAssetIKRetargeterCapability::AfterPrepareTarget(
	void* Target,
	const TSharedPtr<FJsonObject>& Args,
	TSharedPtr<FJsonObject>& OutTop) const
{
	(void)Args;
	if (FIKRetargeterActionState* State = static_cast<FIKRetargeterActionState*>(Target))
	{
		State->OutTop = OutTop;
	}
}

void FNexusManageAssetIKRetargeterCapability::FinalizeTarget(void* Target) const
{
	FIKRetargeterActionState* State = static_cast<FIKRetargeterActionState*>(Target);
	if (!State) return;
	if (State->bDirty && State->Retargeter)
	{
		State->Retargeter->MarkPackageDirty();
		if (State->OutTop.IsValid())
		{
			State->OutTop->SetStringField(TEXT("Note"), TEXT("Modified; persist with SaveAsset"));
		}
	}
	delete State;
}

void FNexusManageAssetIKRetargeterCapability::RegisterActions(TMap<FString, FNexusActionHandler>& OutHandlers) const
{
	OutHandlers.Add(TEXT("SetSourceRig"),   &HandleIKR_SetSourceRig);
	OutHandlers.Add(TEXT("SetTargetRig"),   &HandleIKR_SetTargetRig);
	OutHandlers.Add(TEXT("SetChainSource"), &HandleIKR_SetChainSource);
}

REGISTER_MCP_CAPABILITY(FNexusManageAssetIKRetargeterCapability)

#endif // WITH_IK_RIG
