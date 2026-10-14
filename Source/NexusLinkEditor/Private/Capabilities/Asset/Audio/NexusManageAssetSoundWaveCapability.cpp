// Copyright byteyang. All Rights Reserved.

#include "Capabilities/Asset/Audio/NexusManageAssetSoundWaveCapability.h"
#include "NexusCapabilityRegistry.h"
#include "NexusMcpSchemaBuilder.h"
#include "Utils/NexusAssetUtils.h"
#include "Utils/NexusArgs.h"
#include "Utils/NexusPropertyUtils.h"
#include "Sound/SoundWave.h"
#include "NexusMcpTool.h"

void FNexusManageAssetSoundWaveCapability::BuildDefinition(FNexusCapabilityDefinition& Out) const
{
	Out.Name = TEXT("ManageAssetSoundWave");
	Out.SearchAssetTypes = {TEXT("SoundWave")};
	Out.Description = TEXT("Batch edit SoundWave properties. Volume/looping etc.");
	TSharedPtr<FJsonObject> OpSchema = FNexusSchema::Object()
		.Prop(TEXT("Action"),       FNexusSchema::Enum(TEXT("Action"), { TEXT("SetProperty") }))
		.Prop(TEXT("PropertyPath"), FNexusSchema::Str(TEXT("propertypath (e.g. Volume/Looping)")))
		.Prop(TEXT("Value"),        FNexusSchema::Str(TEXT("New property value string")))
		.Required({ TEXT("Action") })
		.Build();
	Out.InputSchema = FNexusSchema::Object()
		.Prop(TEXT("AssetPath"),  FNexusSchema::Str(TEXT("SoundWave asset path")))
		.Prop(TEXT("Operations"), FNexusSchema::ArrayOf(TEXT("Batch property ops (at least one)"), OpSchema.ToSharedRef()))
		.Required({ TEXT("AssetPath"), TEXT("Operations") })
		.Build();
	Out.Tags = { FNexusMcpTags::Write, FNexusMcpTags::Editor };
	Out.ExtraSearchKeywords = { TEXT("audio"), TEXT("wave"), TEXT("Volume"), TEXT("bLoop"), TEXT("Sound") };
	Out.RelatedCapabilities = { TEXT("GetAssetSoundWave"), TEXT("GetAssetSoundCue") };
	Out.Prerequisites = { TEXT("EditorOnly") };
	Out.WhenToUse = TEXT("Edit SoundWave volume/loop/attenuation; persist with SaveAsset");
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
	Op->TryGetStringField(TEXT("PropertyPath"), PropPath);
	Op->TryGetStringField(TEXT("Value"), Value);
	if (PropPath.IsEmpty() || Value.IsEmpty())
	{
		Ctx.Entry->SetStringField(TEXT("Error"), TEXT("SetProperty requires PropertyPath and value"));
		return;
	}
	FString OldVal, ActualVal, Err;
	if (!FNexusPropertyUtils::WritePropertyAndEcho(Wave, { PropPath }, 0, Value, OldVal, ActualVal, Err))
	{
		Ctx.Entry->SetStringField(TEXT("Error"), Err);
		return;
	}
	SWState(Ctx)->bDirty = true;
	Ctx.Entry->SetStringField(TEXT("PropertyPath"), PropPath);
	if (!OldVal.IsEmpty()) Ctx.Entry->SetStringField(TEXT("OldValue"), OldVal);
	if (!ActualVal.IsEmpty()) Ctx.Entry->SetStringField(TEXT("NewValue"), ActualVal);
	Ctx.Entry->SetStringField(TEXT("Note"), TEXT("persist with SaveAsset"));
}

bool FNexusManageAssetSoundWaveCapability::PrepareTarget(
	const TSharedPtr<FJsonObject>& Args,
	TSharedPtr<FJsonObject>& Entry,
	void*& OutTarget,
	FString& OutError) const
{
	const FString AssetPath = FNexusArgs(Args).Str(TEXT("AssetPath"));
	Entry->SetStringField(TEXT("Path"), AssetPath);
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

void FNexusManageAssetSoundWaveCapability::FinalizeTarget(void* Target) const
{
	FSoundWaveActionState* State = static_cast<FSoundWaveActionState*>(Target);
	if (!State) return;
	if (State->bDirty && State->Wave) State->Wave->MarkPackageDirty();
	delete State;
}

void FNexusManageAssetSoundWaveCapability::RegisterActions(TMap<FString, FNexusActionHandler>& OutHandlers) const
{
	OutHandlers.Add(TEXT("SetProperty"), &HandleSW_SetProperty);
}

REGISTER_MCP_CAPABILITY(FNexusManageAssetSoundWaveCapability)
