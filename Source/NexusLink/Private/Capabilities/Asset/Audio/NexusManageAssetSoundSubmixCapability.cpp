// Copyright byteyang. All Rights Reserved.

#include "Capabilities/Asset/Audio/NexusManageAssetSoundSubmixCapability.h"
#include "NexusCapabilityRegistry.h"
#include "NexusMcpSchemaBuilder.h"
#include "Utils/NexusArgs.h"
#include "Utils/NexusVersionCompat.h"
#include "Sound/SoundSubmix.h"
#include "NexusMcpTool.h"

void FManageAssetSoundSubmixCapability::BuildDefinition(FNexusCapabilityDefinition& Out) const
{
	Out.Name = TEXT("manage_asset_sound_submix");
	Out.SearchAssetTypes = {TEXT("SoundSubmix")};
	Out.Description = TEXT("Set SoundSubmix volume (UE5.1+ uses dB fields; see InputSchema).");
	TSharedPtr<FJsonObject> OpSchema = FNexusSchema::Object()
		.Prop(TEXT("action"),         FNexusSchema::Enum(TEXT("Action"), { TEXT("set") }))
		.Prop(TEXT("outputVolume"),   FNexusSchema::Num(TEXT("Output volume linear [0,1] (UE4/5.0)")))
		.Prop(TEXT("wetLevel"),       FNexusSchema::Num(TEXT("Wet level linear [0,1] (UE4/5.0)")))
		.Prop(TEXT("dryLevel"),       FNexusSchema::Num(TEXT("Dry level linear [0,1] (UE4/5.0)")))
		.Prop(TEXT("outputVolumeDB"), FNexusSchema::Num(TEXT("Output volume dB [-96,0] (UE5.1+)")))
		.Prop(TEXT("wetLevelDB"),     FNexusSchema::Num(TEXT("Wet level dB [-96,0] (UE5.1+)")))
		.Prop(TEXT("dryLevelDB"),     FNexusSchema::Num(TEXT("Dry level dB [-96,0] (UE5.1+)")))
		.Required({ TEXT("action") })
		.Build();
	Out.InputSchema = FNexusSchema::Object()
		.Prop(TEXT("assetPath"),  FNexusSchema::Str(TEXT("SoundSubmix asset path")))
		.Prop(TEXT("operations"), FNexusSchema::ArrayOf(TEXT("Batch ops (at least one)"), OpSchema.ToSharedRef()))
		.Required({ TEXT("assetPath"), TEXT("operations") })
		.Build();
	Out.Tags = { FNexusMcpTags::Write, FNexusMcpTags::Data };
	Out.ExtraSearchKeywords = { TEXT("submix"), TEXT("sound"), TEXT("audio"), TEXT("volume"), TEXT("wet"), TEXT("dry") };
	Out.RelatedCapabilities = { TEXT("get_asset_sound_submix"), TEXT("create_asset_sound_submix") };
}

struct FSubmixActionState
{
	USoundSubmix* SM = nullptr;
	bool bDirty = false;
};

static FSubmixActionState* SubmixState(FNexusActionContext& Ctx)
{
	return static_cast<FSubmixActionState*>(Ctx.Target);
}

static void HandleSubmix_Set(const TSharedPtr<FJsonObject>& Op, FNexusActionContext& Ctx)
{
	USoundSubmix* SM = SubmixState(Ctx)->SM;
#if NX_UE_HAS_SUBMIX_LINEAR_VOLUME_FIELDS
	if (Op->HasField(TEXT("outputVolume"))) SM->OutputVolume = FMath::Clamp(static_cast<float>(Op->GetNumberField(TEXT("outputVolume"))), 0.f, 1.f);
	if (Op->HasField(TEXT("wetLevel")))     SM->WetLevel     = FMath::Clamp(static_cast<float>(Op->GetNumberField(TEXT("wetLevel"))),     0.f, 1.f);
	if (Op->HasField(TEXT("dryLevel")))     SM->DryLevel     = FMath::Clamp(static_cast<float>(Op->GetNumberField(TEXT("dryLevel"))),     0.f, 1.f);
#else
	if (Op->HasField(TEXT("outputVolumeDB"))) SM->OutputVolumeModulation.Value = FMath::Clamp(static_cast<float>(Op->GetNumberField(TEXT("outputVolumeDB"))), -96.f, 0.f);
	if (Op->HasField(TEXT("wetLevelDB")))     SM->WetLevelModulation.Value     = FMath::Clamp(static_cast<float>(Op->GetNumberField(TEXT("wetLevelDB"))),     -96.f, 0.f);
	if (Op->HasField(TEXT("dryLevelDB")))     SM->DryLevelModulation.Value     = FMath::Clamp(static_cast<float>(Op->GetNumberField(TEXT("dryLevelDB"))),     -96.f, 0.f);
#endif
	SubmixState(Ctx)->bDirty = true;
	Ctx.Entry->SetStringField(TEXT("name"), SM->GetName());
#if NX_UE_HAS_SUBMIX_LINEAR_VOLUME_FIELDS
	Ctx.Entry->SetNumberField(TEXT("outputVolume"), SM->OutputVolume);
	Ctx.Entry->SetNumberField(TEXT("wetLevel"),     SM->WetLevel);
	Ctx.Entry->SetNumberField(TEXT("dryLevel"),     SM->DryLevel);
#else
	Ctx.Entry->SetNumberField(TEXT("outputVolumeDB"), SM->OutputVolumeModulation.Value);
	Ctx.Entry->SetNumberField(TEXT("wetLevelDB"),     SM->WetLevelModulation.Value);
	Ctx.Entry->SetNumberField(TEXT("dryLevelDB"),     SM->DryLevelModulation.Value);
#endif
}

bool FManageAssetSoundSubmixCapability::PrepareTarget(
	const TSharedPtr<FJsonObject>& Args,
	TSharedPtr<FJsonObject>& Entry,
	void*& OutTarget,
	FString& OutError) const
{
	const FString AssetPath = FNexusArgs(Args).Str(TEXT("assetPath"));
	Entry->SetStringField(TEXT("path"), AssetPath);
	USoundSubmix* SM = LoadObject<USoundSubmix>(nullptr, *AssetPath);
	if (!SM)
	{
		OutError = FString::Printf(TEXT("Failed to load SoundSubmix: %s"), *AssetPath);
		return false;
	}
	FSubmixActionState* State = new FSubmixActionState();
	State->SM = SM;
	OutTarget = State;
	return true;
}

void FManageAssetSoundSubmixCapability::FinalizeTarget(void* Target) const
{
	FSubmixActionState* State = static_cast<FSubmixActionState*>(Target);
	if (!State) return;
	if (State->bDirty && State->SM) State->SM->MarkPackageDirty();
	delete State;
}

void FManageAssetSoundSubmixCapability::RegisterActions(TMap<FString, FNexusActionHandler>& OutHandlers) const
{
	OutHandlers.Add(TEXT("set"), &HandleSubmix_Set);
}

REGISTER_MCP_CAPABILITY(FManageAssetSoundSubmixCapability)
