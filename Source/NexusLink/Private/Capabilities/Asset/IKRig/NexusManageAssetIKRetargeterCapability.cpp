// Copyright byteyang. All Rights Reserved.

#include "Capabilities/Asset/IKRig/NexusManageAssetIKRetargeterCapability.h"

#if WITH_IK_RIG

#include "Utils/NexusArgs.h"
#include "NexusCapabilityRegistry.h"
#include "NexusMcpSchemaBuilder.h"
#include "Utils/NexusAssetUtils.h"
#include "Retargeter/IKRetargeter.h"
#include "Retargeter/IKRetargetSettings.h"
#include "Rig/IKRigDefinition.h"
#include "NexusMcpTool.h"

void FManageAssetIKRetargeterCapability::BuildDefinition(FNexusCapabilityDefinition& Out) const
{
	Out.Name = TEXT("manage_asset_ik_retargeter");
	Out.SearchAssetTypes = {TEXT("IKRetargeter")};
	Out.Description = TEXT("Edit IKRetargeter: set_source_rig / set_target_rig / set_chain_source.");
	TSharedPtr<FJsonObject> OpSchema = FNexusSchema::Object()
		.Required(TEXT("action"), FNexusSchema::Enum(TEXT("Action"),
			{ TEXT("set_source_rig"), TEXT("set_target_rig"), TEXT("set_chain_source") }))
		.Prop(TEXT("rigPath"),       FNexusSchema::Str(TEXT("IKRig asset path (set_source/target_rig)")))
		.Prop(TEXT("targetChain"),   FNexusSchema::Str(TEXT("Target chain name (set_chain_source)")))
		.Prop(TEXT("sourceChain"),   FNexusSchema::Str(TEXT("Source chain name (set_chain_source)")))
		.Build();
	Out.InputSchema = FNexusSchema::Object()
		.Required(TEXT("assetPath"),  FNexusSchema::Str(TEXT("IKRetargeter asset path")))
		.Required(TEXT("operations"), FNexusSchema::ArrayOf(TEXT("Operation list"), OpSchema.ToSharedRef()))
		.Build();
	Out.Tags = { FNexusMcpTags::Write, FNexusMcpTags::Editor };
	Out.ExtraSearchKeywords = { TEXT("ikretargeter"), TEXT("retarget"), TEXT("chain"), TEXT("source"), TEXT("target") };
	Out.RelatedCapabilities = { TEXT("get_asset_ik_retargeter"), TEXT("get_asset_ik_rig"), TEXT("create_asset_ik_retargeter") };
	Out.WhenToUse = TEXT("Edit IKRetargeter bindings; persist with save_asset after changes");
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
	const FString RigPath = FNexusArgs(Op).Str(TEXT("rigPath"));
	if (RigPath.IsEmpty())
	{
		Ctx.Entry->SetStringField(TEXT("error"), TEXT("rigPath required"));
		return;
	}
	UIKRigDefinition* IKRig = FNexusAssetUtils::LoadAssetWithFallback<UIKRigDefinition>(RigPath);
	if (!IKRig)
	{
		Ctx.Entry->SetStringField(TEXT("error"),
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
		Ctx.Entry->SetStringField(TEXT("rigPath"), RigPath);
	}
	else
	{
		Ctx.Entry->SetStringField(TEXT("error"), TEXT("Reflection field not found"));
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
	const FNexusArgs A(Op);
	const FString TargetChain = A.Str(TEXT("targetChain"));
	const FString SourceChain = A.Str(TEXT("sourceChain"));
	if (TargetChain.IsEmpty())
	{
		Ctx.Entry->SetStringField(TEXT("error"), TEXT("set_chain_source requires targetChain"));
		return;
	}
	const TObjectPtr<URetargetChainSettings> CS = Retargeter->GetChainMapByName(FName(*TargetChain));
	if (!CS)
	{
		Ctx.Entry->SetStringField(TEXT("error"),
			FString::Printf(TEXT("Chain not found: %s"), *TargetChain));
		return;
	}
	CS->SourceChain = FName(*SourceChain);
	MarkIKRDirty(Ctx);
	Ctx.Entry->SetStringField(TEXT("targetChain"), TargetChain);
	Ctx.Entry->SetStringField(TEXT("sourceChain"), SourceChain);
}

bool FManageAssetIKRetargeterCapability::PrepareTarget(
	const TSharedPtr<FJsonObject>& Args,
	TSharedPtr<FJsonObject>& Entry,
	void*& OutTarget,
	FString& OutError) const
{
	const FString AssetPath = FNexusArgs(Args).Str(TEXT("assetPath"));
	Entry->SetStringField(TEXT("path"), AssetPath);
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

void FManageAssetIKRetargeterCapability::AfterPrepareTarget(
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

void FManageAssetIKRetargeterCapability::FinalizeTarget(void* Target) const
{
	FIKRetargeterActionState* State = static_cast<FIKRetargeterActionState*>(Target);
	if (!State) return;
	if (State->bDirty && State->Retargeter)
	{
		State->Retargeter->MarkPackageDirty();
		if (State->OutTop.IsValid())
		{
			State->OutTop->SetStringField(TEXT("note"), TEXT("Modified; persist with save_asset"));
		}
	}
	delete State;
}

void FManageAssetIKRetargeterCapability::RegisterActions(TMap<FString, FNexusActionHandler>& OutHandlers) const
{
	OutHandlers.Add(TEXT("set_source_rig"),   &HandleIKR_SetSourceRig);
	OutHandlers.Add(TEXT("set_target_rig"),   &HandleIKR_SetTargetRig);
	OutHandlers.Add(TEXT("set_chain_source"), &HandleIKR_SetChainSource);
}

REGISTER_MCP_CAPABILITY(FManageAssetIKRetargeterCapability)

#endif // WITH_IK_RIG
