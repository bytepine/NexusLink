// Copyright byteyang. All Rights Reserved.

#include "Capabilities/Asset/Audio/NexusManageAssetSoundClassCapability.h"
#include "NexusCapabilityRegistry.h"
#include "NexusMcpSchemaBuilder.h"
#include "Utils/NexusArgs.h"
#include "Sound/SoundClass.h"
#include "NexusMcpTool.h"

void FManageAssetSoundClassCapability::BuildDefinition(FNexusCapabilityDefinition& Out) const
{
	Out.Name = TEXT("manage_asset_sound_class");
	Out.SearchAssetTypes = {TEXT("SoundClass")};
	Out.Description = TEXT("Set SoundClass volume/pitch/lowPassFilter/attenuationScale.");
	TSharedPtr<FJsonObject> OpSchema = FNexusSchema::Object()
		.Prop(TEXT("action"),           FNexusSchema::Enum(TEXT("Action"), { TEXT("set") }))
		.Prop(TEXT("volume"),           FNexusSchema::Num(TEXT("Volume multiplier [0,∞)")))
		.Prop(TEXT("pitch"),            FNexusSchema::Num(TEXT("Pitch multiplier [0,∞)")))
		.Prop(TEXT("lowPassFilter"),    FNexusSchema::Num(TEXT("Low-pass cutoff (Hz)")))
		.Prop(TEXT("attenuationScale"), FNexusSchema::Num(TEXT("Attenuation distance scale")))
		.Required({ TEXT("action") })
		.Build();
	Out.InputSchema = FNexusSchema::Object()
		.Prop(TEXT("assetPath"),  FNexusSchema::Str(TEXT("SoundClass asset path")))
		.Prop(TEXT("operations"), FNexusSchema::ArrayOf(TEXT("Batch ops (at least one)"), OpSchema.ToSharedRef()))
		.Required({ TEXT("assetPath"), TEXT("operations") })
		.Build();
	Out.Tags = { FNexusMcpTags::Write, FNexusMcpTags::Data };
	Out.ExtraSearchKeywords = { TEXT("sound"), TEXT("class"), TEXT("volume"), TEXT("pitch"), TEXT("filter") };
	Out.RelatedCapabilities = { TEXT("get_asset_sound_class"), TEXT("create_asset_sound_class") };
}

struct FSoundClassActionState
{
	USoundClass* SC = nullptr;
	bool bDirty = false;
};

static FSoundClassActionState* SClassState(FNexusActionContext& Ctx)
{
	return static_cast<FSoundClassActionState*>(Ctx.Target);
}

static void HandleSClass_Set(const TSharedPtr<FJsonObject>& Op, FNexusActionContext& Ctx)
{
	USoundClass* SC = SClassState(Ctx)->SC;
	if (Op->HasField(TEXT("volume")))           SC->Properties.Volume                   = static_cast<float>(Op->GetNumberField(TEXT("volume")));
	if (Op->HasField(TEXT("pitch")))            SC->Properties.Pitch                    = static_cast<float>(Op->GetNumberField(TEXT("pitch")));
	if (Op->HasField(TEXT("lowPassFilter")))    SC->Properties.LowPassFilterFrequency   = static_cast<float>(Op->GetNumberField(TEXT("lowPassFilter")));
	if (Op->HasField(TEXT("attenuationScale"))) SC->Properties.AttenuationDistanceScale = static_cast<float>(Op->GetNumberField(TEXT("attenuationScale")));
	SClassState(Ctx)->bDirty = true;
	Ctx.Entry->SetStringField(TEXT("name"),          SC->GetName());
	Ctx.Entry->SetNumberField(TEXT("volume"),        SC->Properties.Volume);
	Ctx.Entry->SetNumberField(TEXT("pitch"),         SC->Properties.Pitch);
	Ctx.Entry->SetNumberField(TEXT("lowPassFilter"), SC->Properties.LowPassFilterFrequency);
}

bool FManageAssetSoundClassCapability::PrepareTarget(
	const TSharedPtr<FJsonObject>& Args,
	TSharedPtr<FJsonObject>& Entry,
	void*& OutTarget,
	FString& OutError) const
{
	const FString AssetPath = FNexusArgs(Args).Str(TEXT("assetPath"));
	Entry->SetStringField(TEXT("path"), AssetPath);
	USoundClass* SC = LoadObject<USoundClass>(nullptr, *AssetPath);
	if (!SC)
	{
		OutError = FString::Printf(TEXT("Failed to load SoundClass: %s"), *AssetPath);
		return false;
	}
	FSoundClassActionState* State = new FSoundClassActionState();
	State->SC = SC;
	OutTarget = State;
	return true;
}

void FManageAssetSoundClassCapability::FinalizeTarget(void* Target) const
{
	FSoundClassActionState* State = static_cast<FSoundClassActionState*>(Target);
	if (!State) return;
	if (State->bDirty && State->SC) State->SC->MarkPackageDirty();
	delete State;
}

void FManageAssetSoundClassCapability::RegisterActions(TMap<FString, FNexusActionHandler>& OutHandlers) const
{
	OutHandlers.Add(TEXT("set"), &HandleSClass_Set);
}

REGISTER_MCP_CAPABILITY(FManageAssetSoundClassCapability)
