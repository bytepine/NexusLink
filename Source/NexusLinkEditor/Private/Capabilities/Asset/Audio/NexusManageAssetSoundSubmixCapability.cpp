// Copyright byteyang. All Rights Reserved.

#include "Capabilities/Asset/Audio/NexusManageAssetSoundSubmixCapability.h"
#include "NexusCapabilityRegistry.h"
#include "NexusMcpSchemaBuilder.h"
#include "Utils/NexusArgs.h"
#include "Utils/NexusVersionCompat.h"
#include "Sound/SoundSubmix.h"
#include "NexusMcpTool.h"

void FNexusManageAssetSoundSubmixCapability::BuildDefinition(FNexusCapabilityDefinition& Out) const
{
	Out.Name = TEXT("ManageAssetSoundSubmix");
	Out.SearchAssetTypes = {TEXT("SoundSubmix")};
	Out.Description = TEXT("Set SoundSubmix volume (UE5.1+ uses dB fields; see InputSchema).");
	TSharedPtr<FJsonObject> OpSchema = FNexusSchema::Object()
		.Prop(TEXT("Action"),         FNexusSchema::Enum(TEXT("Action"), { TEXT("Set") }))
		.Prop(TEXT("OutputVolume"),   FNexusSchema::Num(TEXT("Output volume linear [0,1] (UE4/5.0)")))
		.Prop(TEXT("WetLevel"),       FNexusSchema::Num(TEXT("Wet level linear [0,1] (UE4/5.0)")))
		.Prop(TEXT("DryLevel"),       FNexusSchema::Num(TEXT("Dry level linear [0,1] (UE4/5.0)")))
		.Prop(TEXT("OutputVolumeDB"), FNexusSchema::Num(TEXT("Output volume dB [-96,0] (UE5.1+)")))
		.Prop(TEXT("WetLevelDB"),     FNexusSchema::Num(TEXT("Wet level dB [-96,0] (UE5.1+)")))
		.Prop(TEXT("DryLevelDB"),     FNexusSchema::Num(TEXT("Dry level dB [-96,0] (UE5.1+)")))
		.Required({ TEXT("Action") })
		.Build();
	Out.InputSchema = FNexusSchema::Object()
		.Prop(TEXT("AssetPath"),  FNexusSchema::Str(TEXT("SoundSubmix asset path")))
		.Prop(TEXT("Operations"), FNexusSchema::ArrayOf(TEXT("Batch ops (at least one)"), OpSchema.ToSharedRef()))
		.Required({ TEXT("AssetPath"), TEXT("Operations") })
		.Build();
	Out.Tags = { FNexusMcpTags::Write, FNexusMcpTags::Data };
	Out.ExtraSearchKeywords = { TEXT("submix"), TEXT("Sound"), TEXT("audio"), TEXT("Volume"), TEXT("wet"), TEXT("dry") };
	Out.RelatedCapabilities = { TEXT("GetAssetSoundSubmix"), TEXT("CreateAssetSoundSubmix") };
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
	if (Op->HasField(TEXT("OutputVolume"))) SM->OutputVolume = FMath::Clamp(static_cast<float>(Op->GetNumberField(TEXT("OutputVolume"))), 0.f, 1.f);
	if (Op->HasField(TEXT("WetLevel")))     SM->WetLevel     = FMath::Clamp(static_cast<float>(Op->GetNumberField(TEXT("WetLevel"))),     0.f, 1.f);
	if (Op->HasField(TEXT("DryLevel")))     SM->DryLevel     = FMath::Clamp(static_cast<float>(Op->GetNumberField(TEXT("DryLevel"))),     0.f, 1.f);
#else
	if (Op->HasField(TEXT("OutputVolumeDB"))) SM->OutputVolumeModulation.Value = FMath::Clamp(static_cast<float>(Op->GetNumberField(TEXT("OutputVolumeDB"))), -96.f, 0.f);
	if (Op->HasField(TEXT("WetLevelDB")))     SM->WetLevelModulation.Value     = FMath::Clamp(static_cast<float>(Op->GetNumberField(TEXT("WetLevelDB"))),     -96.f, 0.f);
	if (Op->HasField(TEXT("DryLevelDB")))     SM->DryLevelModulation.Value     = FMath::Clamp(static_cast<float>(Op->GetNumberField(TEXT("DryLevelDB"))),     -96.f, 0.f);
#endif
	SubmixState(Ctx)->bDirty = true;
	Ctx.Entry->SetStringField(TEXT("name"), SM->GetName());
#if NX_UE_HAS_SUBMIX_LINEAR_VOLUME_FIELDS
	Ctx.Entry->SetNumberField(TEXT("OutputVolume"), SM->OutputVolume);
	Ctx.Entry->SetNumberField(TEXT("WetLevel"),     SM->WetLevel);
	Ctx.Entry->SetNumberField(TEXT("DryLevel"),     SM->DryLevel);
#else
	Ctx.Entry->SetNumberField(TEXT("OutputVolumeDB"), SM->OutputVolumeModulation.Value);
	Ctx.Entry->SetNumberField(TEXT("WetLevelDB"),     SM->WetLevelModulation.Value);
	Ctx.Entry->SetNumberField(TEXT("DryLevelDB"),     SM->DryLevelModulation.Value);
#endif
}

bool FNexusManageAssetSoundSubmixCapability::PrepareTarget(
	const TSharedPtr<FJsonObject>& Args,
	TSharedPtr<FJsonObject>& Entry,
	void*& OutTarget,
	FString& OutError) const
{
	const FString AssetPath = FNexusArgs(Args).Str(TEXT("AssetPath"));
	Entry->SetStringField(TEXT("Path"), AssetPath);
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

void FNexusManageAssetSoundSubmixCapability::FinalizeTarget(void* Target) const
{
	FSubmixActionState* State = static_cast<FSubmixActionState*>(Target);
	if (!State) return;
	if (State->bDirty && State->SM) State->SM->MarkPackageDirty();
	delete State;
}

void FNexusManageAssetSoundSubmixCapability::RegisterActions(TMap<FString, FNexusActionHandler>& OutHandlers) const
{
	OutHandlers.Add(TEXT("Set"), &HandleSubmix_Set);
}

REGISTER_MCP_CAPABILITY(FNexusManageAssetSoundSubmixCapability)
