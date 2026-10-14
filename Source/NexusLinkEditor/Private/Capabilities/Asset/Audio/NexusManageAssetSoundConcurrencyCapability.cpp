// Copyright byteyang. All Rights Reserved.

#include "Capabilities/Asset/Audio/NexusManageAssetSoundConcurrencyCapability.h"
#include "NexusCapabilityRegistry.h"
#include "NexusMcpSchemaBuilder.h"
#include "Utils/NexusArgs.h"
#include "Sound/SoundConcurrency.h"
#include "NexusMcpTool.h"

void FNexusManageAssetSoundConcurrencyCapability::BuildDefinition(FNexusCapabilityDefinition& Out) const
{
	Out.Name = TEXT("ManageAssetSoundConcurrency");
	Out.SearchAssetTypes = {TEXT("SoundConcurrency")};
	Out.Description = TEXT("Set SoundConcurrency: MaxCount/ResolutionRuleValue/RetriggerTime/bLimitToOwner.");
	TSharedPtr<FJsonObject> OpSchema = FNexusSchema::Object()
		.Prop(TEXT("Action"),              FNexusSchema::Enum(TEXT("Action"), { TEXT("Set") }))
		.Prop(TEXT("MaxCount"),            FNexusSchema::Int(TEXT("Max concurrent instances (≥1)")))
		.Prop(TEXT("ResolutionRuleValue"), FNexusSchema::Int(TEXT("EMaxConcurrentResolutionRule int: 0=PreventNew,1=StopOldest…")))
		.Prop(TEXT("RetriggerTime"),       FNexusSchema::Num(TEXT("Retrigger time (sec); min interval before same sound retriggers")))
		.Prop(TEXT("bLimitToOwner"),        FNexusSchema::Bool(TEXT("Limit concurrency per Owner")))
		.Required({ TEXT("Action") })
		.Build();
	Out.InputSchema = FNexusSchema::Object()
		.Prop(TEXT("AssetPath"),  FNexusSchema::Str(TEXT("SoundConcurrency asset path")))
		.Prop(TEXT("Operations"), FNexusSchema::ArrayOf(TEXT("Batch ops (at least one)"), OpSchema.ToSharedRef()))
		.Required({ TEXT("AssetPath"), TEXT("Operations") })
		.Build();
	Out.Tags = { FNexusMcpTags::Write, FNexusMcpTags::Data };
	Out.ExtraSearchKeywords = { TEXT("concurrency"), TEXT("maxcount"), TEXT("audio"), TEXT("Limit") };
	Out.RelatedCapabilities = { TEXT("GetAssetSoundConcurrency"), TEXT("CreateAssetSoundConcurrency") };
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
	if (Op->HasField(TEXT("MaxCount")))
		SC->Concurrency.MaxCount = FMath::Max(1, static_cast<int32>(Op->GetNumberField(TEXT("MaxCount"))));
	if (Op->HasField(TEXT("ResolutionRuleValue")))
		SC->Concurrency.ResolutionRule = EMaxConcurrentResolutionRule::Type(static_cast<int32>(Op->GetNumberField(TEXT("ResolutionRuleValue"))));
	if (Op->HasField(TEXT("RetriggerTime")))
		SC->Concurrency.RetriggerTime = static_cast<float>(Op->GetNumberField(TEXT("RetriggerTime")));
	if (Op->HasField(TEXT("bLimitToOwner")))
		SC->Concurrency.bLimitToOwner = Op->GetBoolField(TEXT("bLimitToOwner")) ? 1 : 0;
	ConcState(Ctx)->bDirty = true;
	Ctx.Entry->SetStringField(TEXT("name"),     SC->GetName());
	Ctx.Entry->SetNumberField(TEXT("MaxCount"), SC->Concurrency.MaxCount);
}

bool FNexusManageAssetSoundConcurrencyCapability::PrepareTarget(
	const TSharedPtr<FJsonObject>& Args,
	TSharedPtr<FJsonObject>& Entry,
	void*& OutTarget,
	FString& OutError) const
{
	const FString AssetPath = FNexusArgs(Args).Str(TEXT("AssetPath"));
	Entry->SetStringField(TEXT("Path"), AssetPath);
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

void FNexusManageAssetSoundConcurrencyCapability::FinalizeTarget(void* Target) const
{
	FConcurrencyActionState* State = static_cast<FConcurrencyActionState*>(Target);
	if (!State) return;
	if (State->bDirty && State->SC) State->SC->MarkPackageDirty();
	delete State;
}

void FNexusManageAssetSoundConcurrencyCapability::RegisterActions(TMap<FString, FNexusActionHandler>& OutHandlers) const
{
	OutHandlers.Add(TEXT("Set"), &HandleConc_Set);
}

REGISTER_MCP_CAPABILITY(FNexusManageAssetSoundConcurrencyCapability)
