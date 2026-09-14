// Copyright byteyang. All Rights Reserved.

#include "Capabilities/Asset/Audio/NexusManageAssetSoundConcurrencyCapability.h"
#include "NexusCapabilityRegistry.h"
#include "NexusMcpSchemaBuilder.h"
#include "Utils/NexusArgs.h"
#include "Sound/SoundConcurrency.h"
#include "NexusMcpTool.h"

void FManageAssetSoundConcurrencyCapability::BuildDefinition(FNexusCapabilityDefinition& Out) const
{
	Out.Name = TEXT("manage_asset_sound_concurrency");
	Out.SearchAssetTypes = {TEXT("SoundConcurrency")};
	Out.Description = TEXT("Set SoundConcurrency: maxCount/resolutionRuleValue/retriggerTime/limitToOwner.");
	TSharedPtr<FJsonObject> OpSchema = FNexusSchema::Object()
		.Prop(TEXT("action"),              FNexusSchema::Enum(TEXT("Action"), { TEXT("set") }))
		.Prop(TEXT("maxCount"),            FNexusSchema::Int(TEXT("Max concurrent instances (≥1)")))
		.Prop(TEXT("resolutionRuleValue"), FNexusSchema::Int(TEXT("EMaxConcurrentResolutionRule int: 0=PreventNew,1=StopOldest…")))
		.Prop(TEXT("retriggerTime"),       FNexusSchema::Num(TEXT("Retrigger time (sec); min interval before same sound retriggers")))
		.Prop(TEXT("limitToOwner"),        FNexusSchema::Bool(TEXT("Limit concurrency per Owner")))
		.Required({ TEXT("action") })
		.Build();
	Out.InputSchema = FNexusSchema::Object()
		.Prop(TEXT("assetPath"),  FNexusSchema::Str(TEXT("SoundConcurrency asset path")))
		.Prop(TEXT("operations"), FNexusSchema::ArrayOf(TEXT("Batch ops (at least one)"), OpSchema.ToSharedRef()))
		.Required({ TEXT("assetPath"), TEXT("operations") })
		.Build();
	Out.Tags = { FNexusMcpTags::Write, FNexusMcpTags::Data };
	Out.ExtraSearchKeywords = { TEXT("concurrency"), TEXT("maxcount"), TEXT("audio"), TEXT("limit") };
	Out.RelatedCapabilities = { TEXT("get_asset_sound_concurrency"), TEXT("create_asset_sound_concurrency") };
}

struct FConcurrencyActionState
{
	USoundConcurrency* SC = nullptr;
	bool bDirty = false;
};

static FConcurrencyActionState* ConcState(FNexusActionContext& Ctx)
{
	return static_cast<FConcurrencyActionState*>(Ctx.Target);
}

static void HandleConc_Set(const TSharedPtr<FJsonObject>& Op, FNexusActionContext& Ctx)
{
	USoundConcurrency* SC = ConcState(Ctx)->SC;
	if (Op->HasField(TEXT("maxCount")))
		SC->Concurrency.MaxCount = FMath::Max(1, static_cast<int32>(Op->GetNumberField(TEXT("maxCount"))));
	if (Op->HasField(TEXT("resolutionRuleValue")))
		SC->Concurrency.ResolutionRule = EMaxConcurrentResolutionRule::Type(static_cast<int32>(Op->GetNumberField(TEXT("resolutionRuleValue"))));
	if (Op->HasField(TEXT("retriggerTime")))
		SC->Concurrency.RetriggerTime = static_cast<float>(Op->GetNumberField(TEXT("retriggerTime")));
	if (Op->HasField(TEXT("limitToOwner")))
		SC->Concurrency.bLimitToOwner = Op->GetBoolField(TEXT("limitToOwner")) ? 1 : 0;
	ConcState(Ctx)->bDirty = true;
	Ctx.Entry->SetStringField(TEXT("name"),     SC->GetName());
	Ctx.Entry->SetNumberField(TEXT("maxCount"), SC->Concurrency.MaxCount);
}

bool FManageAssetSoundConcurrencyCapability::PrepareTarget(
	const TSharedPtr<FJsonObject>& Args,
	TSharedPtr<FJsonObject>& Entry,
	void*& OutTarget,
	FString& OutError) const
{
	const FString AssetPath = FNexusArgs(Args).Str(TEXT("assetPath"));
	Entry->SetStringField(TEXT("path"), AssetPath);
	USoundConcurrency* SC = LoadObject<USoundConcurrency>(nullptr, *AssetPath);
	if (!SC)
	{
		OutError = FString::Printf(TEXT("Failed to load SoundConcurrency: %s"), *AssetPath);
		return false;
	}
	FConcurrencyActionState* State = new FConcurrencyActionState();
	State->SC = SC;
	OutTarget = State;
	return true;
}

void FManageAssetSoundConcurrencyCapability::FinalizeTarget(void* Target) const
{
	FConcurrencyActionState* State = static_cast<FConcurrencyActionState*>(Target);
	if (!State) return;
	if (State->bDirty && State->SC) State->SC->MarkPackageDirty();
	delete State;
}

void FManageAssetSoundConcurrencyCapability::RegisterActions(TMap<FString, FNexusActionHandler>& OutHandlers) const
{
	OutHandlers.Add(TEXT("set"), &HandleConc_Set);
}

REGISTER_MCP_CAPABILITY(FManageAssetSoundConcurrencyCapability)
