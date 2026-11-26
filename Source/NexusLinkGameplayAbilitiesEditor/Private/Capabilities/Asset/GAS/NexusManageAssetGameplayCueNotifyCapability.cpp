// Copyright byteyang. All Rights Reserved.

#include "Capabilities/Asset/GAS/NexusManageAssetGameplayCueNotifyCapability.h"

#if WITH_GAS

#include "NexusCapabilityRegistry.h"
#include "NexusMcpSchemaBuilder.h"
#include "Utils/NexusAssetUtils.h"
#include "Utils/NexusArgs.h"
#include "GameplayCueNotify_Static.h"
#include "NexusMcpTool.h"

void FNexusManageAssetGameplayCueNotifyCapability::BuildDefinition(FNexusCapabilityDefinition& Out) const
{
	Out.Name = TEXT("ManageAssetGameplayCueNotify");
	Out.SearchAssetTypes = {TEXT("GameplayCueNotify_Static")};
	Out.Description = TEXT("Batch edit GameplayCueNotify_Static. Actor BP graph via blueprint.");
	TSharedPtr<FJsonObject> OpSchema = FNexusSchema::Object()
		.Prop(TEXT("Action"), FNexusSchema::Enum(TEXT("Action"), { TEXT("SetCueName") }))
		.Prop(TEXT("CueName"), FNexusSchema::Str(TEXT("GameplayCue Tag")))
		.Required({ TEXT("Action") })
		.Build();
	Out.InputSchema = FNexusSchema::Object()
		.Prop(TEXT("AssetPath"), FNexusSchema::Str(TEXT("Cue Notify asset path")))
		.Prop(TEXT("Operations"), FNexusSchema::ArrayOf(TEXT("Batch ops (at least one)"), OpSchema.ToSharedRef()))
		.Required({ TEXT("AssetPath"), TEXT("Operations") })
		.Build();
	Out.Tags = { FNexusMcpTags::Write, FNexusMcpTags::Gas };
	Out.ExtraSearchKeywords = { TEXT("cue"), TEXT("notify"), TEXT("Tag") };
	Out.RelatedCapabilities = {
		TEXT("GetAssetGameplayCueNotify"), TEXT("CreateAssetGameplayCueNotify"),
		TEXT("ManageAssetBlueprint")
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
	if (!Op->TryGetStringField(TEXT("CueName"), CueName) || CueName.IsEmpty())
	{
		Ctx.Entry->SetStringField(TEXT("Error"), TEXT("SetCueName requires CueName"));
		return;
	}
	Notify->GameplayCueName = FName(*CueName);
	CueState(Ctx)->bDirty = true;
	Ctx.Entry->SetStringField(TEXT("CueName"), Notify->GameplayCueName.ToString());
}

bool FNexusManageAssetGameplayCueNotifyCapability::PrepareTarget(
	const TSharedPtr<FJsonObject>& Args,
	TSharedPtr<FJsonObject>& Entry,
	void*& OutTarget,
	FString& OutError) const
{
	const FString AssetPath = FNexusArgs(Args).Str(TEXT("AssetPath"));
	Entry->SetStringField(TEXT("Path"), AssetPath);
	UGameplayCueNotify_Static* Notify = FNexusAssetUtils::LoadAssetWithFallback<UGameplayCueNotify_Static>(AssetPath);
	if (!Notify)
	{
		OutError = TEXT("Only supports GameplayCueNotify_Static; use ManageAssetBlueprint for Actor BP");
		return false;
	}
	FCueNotifyActionState* State = new FCueNotifyActionState();
	State->Notify = Notify;
	OutTarget = State;
	return true;
}

void FNexusManageAssetGameplayCueNotifyCapability::FinalizeTarget(void* Target) const
{
	FCueNotifyActionState* State = static_cast<FCueNotifyActionState*>(Target);
	if (!State) return;
	if (State->bDirty && State->Notify) State->Notify->MarkPackageDirty();
	delete State;
}

void FNexusManageAssetGameplayCueNotifyCapability::RegisterActions(TMap<FString, FNexusActionHandler>& OutHandlers) const
{
	OutHandlers.Add(TEXT("SetCueName"), &HandleCue_SetCueName);
}

REGISTER_MCP_CAPABILITY(FNexusManageAssetGameplayCueNotifyCapability)

#endif
