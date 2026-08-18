// Copyright byteyang. All Rights Reserved.

#include "Capabilities/Asset/GAS/NexusManageAssetGameplayCueNotifyCapability.h"

#if WITH_GAS

#include "NexusCapabilityRegistry.h"
#include "NexusMcpSchemaBuilder.h"
#include "Utils/NexusAssetUtils.h"
#include "Utils/NexusArgs.h"
#include "GameplayCueNotify_Static.h"
#include "NexusMcpTool.h"

void FManageAssetGameplayCueNotifyCapability::BuildDefinition(FNexusCapabilityDefinition& Out) const
{
	Out.Name = TEXT("manage_asset_gameplay_cue_notify");
	Out.SearchAssetTypes = {TEXT("GameplayCueNotify_Static")};
	Out.Description = TEXT("Batch edit GameplayCueNotify_Static. Actor BP graph via blueprint.");
	TSharedPtr<FJsonObject> OpSchema = FNexusSchema::Object()
		.Prop(TEXT("action"), FNexusSchema::Enum(TEXT("Action"), { TEXT("set_cue_name") }))
		.Prop(TEXT("cueName"), FNexusSchema::Str(TEXT("GameplayCue Tag")))
		.Required({ TEXT("action") })
		.Build();
	Out.InputSchema = FNexusSchema::Object()
		.Prop(TEXT("assetPath"), FNexusSchema::Str(TEXT("Cue Notify asset path")))
		.Prop(TEXT("operations"), FNexusSchema::ArrayOf(TEXT("Batch ops (at least one)"), OpSchema.ToSharedRef()))
		.Required({ TEXT("assetPath"), TEXT("operations") })
		.Build();
	Out.Tags = { FNexusMcpTags::Write, FNexusMcpTags::Gas };
	Out.ExtraSearchKeywords = { TEXT("cue"), TEXT("notify"), TEXT("tag") };
	Out.RelatedCapabilities = {
		TEXT("get_asset_gameplay_cue_notify"), TEXT("create_asset_gameplay_cue_notify"),
		TEXT("manage_asset_blueprint")
	};
}

struct FCueNotifyActionState
{
	UGameplayCueNotify_Static* Notify = nullptr;
	bool bDirty = false;
};

static FCueNotifyActionState* CueState(FNexusActionContext& Ctx)
{
	return static_cast<FCueNotifyActionState*>(Ctx.Target);
}

static void HandleCue_SetCueName(const TSharedPtr<FJsonObject>& Op, FNexusActionContext& Ctx)
{
	UGameplayCueNotify_Static* Notify = CueState(Ctx)->Notify;
	FString CueName;
	if (!Op->TryGetStringField(TEXT("cueName"), CueName) || CueName.IsEmpty())
	{
		Ctx.Entry->SetStringField(TEXT("error"), TEXT("set_cue_name requires cueName"));
		return;
	}
	Notify->GameplayCueName = FName(*CueName);
	CueState(Ctx)->bDirty = true;
	Ctx.Entry->SetStringField(TEXT("cueName"), Notify->GameplayCueName.ToString());
}

bool FManageAssetGameplayCueNotifyCapability::PrepareTarget(
	const TSharedPtr<FJsonObject>& Args,
	TSharedPtr<FJsonObject>& Entry,
	void*& OutTarget,
	FString& OutError) const
{
	const FString AssetPath = FNexusArgs(Args).Str(TEXT("assetPath"));
	Entry->SetStringField(TEXT("path"), AssetPath);
	UGameplayCueNotify_Static* Notify = FNexusAssetUtils::LoadAssetWithFallback<UGameplayCueNotify_Static>(AssetPath);
	if (!Notify)
	{
		OutError = TEXT("Only supports GameplayCueNotify_Static; use manage_asset_blueprint for Actor BP");
		return false;
	}
	FCueNotifyActionState* State = new FCueNotifyActionState();
	State->Notify = Notify;
	OutTarget = State;
	return true;
}

void FManageAssetGameplayCueNotifyCapability::FinalizeTarget(void* Target) const
{
	FCueNotifyActionState* State = static_cast<FCueNotifyActionState*>(Target);
	if (!State) return;
	if (State->bDirty && State->Notify) State->Notify->MarkPackageDirty();
	delete State;
}

void FManageAssetGameplayCueNotifyCapability::RegisterActions(TMap<FString, FNexusActionHandler>& OutHandlers) const
{
	OutHandlers.Add(TEXT("set_cue_name"), &HandleCue_SetCueName);
}

REGISTER_MCP_CAPABILITY(FManageAssetGameplayCueNotifyCapability)

#endif
