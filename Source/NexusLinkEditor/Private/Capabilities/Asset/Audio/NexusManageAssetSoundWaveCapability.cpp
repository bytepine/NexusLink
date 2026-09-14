// Copyright byteyang. All Rights Reserved.

#include "Capabilities/Asset/Audio/NexusManageAssetSoundWaveCapability.h"
#include "NexusCapabilityRegistry.h"
#include "NexusMcpSchemaBuilder.h"
#include "Utils/NexusAssetUtils.h"
#include "Utils/NexusArgs.h"
#include "Utils/NexusPropertyUtils.h"
#include "Sound/SoundWave.h"
#include "NexusMcpTool.h"

void FManageAssetSoundWaveCapability::BuildDefinition(FNexusCapabilityDefinition& Out) const
{
	Out.Name = TEXT("manage_asset_sound_wave");
	Out.SearchAssetTypes = {TEXT("SoundWave")};
	Out.Description = TEXT("Batch edit SoundWave properties. Volume/looping etc.");
	TSharedPtr<FJsonObject> OpSchema = FNexusSchema::Object()
		.Prop(TEXT("action"),       FNexusSchema::Enum(TEXT("Action"), { TEXT("set_property") }))
		.Prop(TEXT("propertyPath"), FNexusSchema::Str(TEXT("propertypath (e.g. Volume/Looping)")))
		.Prop(TEXT("value"),        FNexusSchema::Str(TEXT("New property value string")))
		.Required({ TEXT("action") })
		.Build();
	Out.InputSchema = FNexusSchema::Object()
		.Prop(TEXT("assetPath"),  FNexusSchema::Str(TEXT("SoundWave asset path")))
		.Prop(TEXT("operations"), FNexusSchema::ArrayOf(TEXT("Batch property ops (at least one)"), OpSchema.ToSharedRef()))
		.Required({ TEXT("assetPath"), TEXT("operations") })
		.Build();
	Out.Tags = { FNexusMcpTags::Write, FNexusMcpTags::Editor };
	Out.ExtraSearchKeywords = { TEXT("audio"), TEXT("wave"), TEXT("volume"), TEXT("loop"), TEXT("sound") };
	Out.RelatedCapabilities = { TEXT("get_asset_sound_wave"), TEXT("get_asset_sound_cue") };
	Out.Prerequisites = { TEXT("editor_only") };
	Out.WhenToUse = TEXT("Edit SoundWave volume/loop/attenuation; persist with save_asset");
}

struct FSoundWaveActionState
{
	USoundWave* Wave = nullptr;
	bool bDirty = false;
};

static FSoundWaveActionState* SWState(FNexusActionContext& Ctx)
{
	return static_cast<FSoundWaveActionState*>(Ctx.Target);
}

static void HandleSW_SetProperty(const TSharedPtr<FJsonObject>& Op, FNexusActionContext& Ctx)
{
	USoundWave* Wave = SWState(Ctx)->Wave;
	FString PropPath, Value;
	Op->TryGetStringField(TEXT("propertyPath"), PropPath);
	Op->TryGetStringField(TEXT("value"), Value);
	if (PropPath.IsEmpty() || Value.IsEmpty())
	{
		Ctx.Entry->SetStringField(TEXT("error"), TEXT("set_property requires propertyPath and value"));
		return;
	}
	FString OldVal, ActualVal, Err;
	if (!FNexusPropertyUtils::WritePropertyAndEcho(Wave, { PropPath }, 0, Value, OldVal, ActualVal, Err))
	{
		Ctx.Entry->SetStringField(TEXT("error"), Err);
		return;
	}
	SWState(Ctx)->bDirty = true;
	Ctx.Entry->SetStringField(TEXT("propertyPath"), PropPath);
	if (!OldVal.IsEmpty()) Ctx.Entry->SetStringField(TEXT("oldValue"), OldVal);
	if (!ActualVal.IsEmpty()) Ctx.Entry->SetStringField(TEXT("newValue"), ActualVal);
	Ctx.Entry->SetStringField(TEXT("note"), TEXT("persist with save_asset"));
}

bool FManageAssetSoundWaveCapability::PrepareTarget(
	const TSharedPtr<FJsonObject>& Args,
	TSharedPtr<FJsonObject>& Entry,
	void*& OutTarget,
	FString& OutError) const
{
	const FString AssetPath = FNexusArgs(Args).Str(TEXT("assetPath"));
	Entry->SetStringField(TEXT("path"), AssetPath);
	USoundWave* Wave = FNexusAssetUtils::LoadAssetWithFallback<USoundWave>(AssetPath);
	if (!Wave)
	{
		OutError = FString::Printf(TEXT("SoundWave not found: %s"), *AssetPath);
		return false;
	}
	FSoundWaveActionState* State = new FSoundWaveActionState();
	State->Wave = Wave;
	OutTarget = State;
	return true;
}

void FManageAssetSoundWaveCapability::FinalizeTarget(void* Target) const
{
	FSoundWaveActionState* State = static_cast<FSoundWaveActionState*>(Target);
	if (!State) return;
	if (State->bDirty && State->Wave) State->Wave->MarkPackageDirty();
	delete State;
}

void FManageAssetSoundWaveCapability::RegisterActions(TMap<FString, FNexusActionHandler>& OutHandlers) const
{
	OutHandlers.Add(TEXT("set_property"), &HandleSW_SetProperty);
}

REGISTER_MCP_CAPABILITY(FManageAssetSoundWaveCapability)
