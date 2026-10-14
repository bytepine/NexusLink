// Copyright byteyang. All Rights Reserved.

#include "Capabilities/Asset/Audio/NexusManageAssetSoundClassCapability.h"
#include "NexusCapabilityRegistry.h"
#include "NexusMcpSchemaBuilder.h"
#include "Utils/NexusArgs.h"
#include "Sound/SoundClass.h"
#include "NexusMcpTool.h"

void FNexusManageAssetSoundClassCapability::BuildDefinition(FNexusCapabilityDefinition& Out) const
{
	Out.Name = TEXT("ManageAssetSoundClass");
	Out.SearchAssetTypes = {TEXT("SoundClass")};
	Out.Description = TEXT("Set SoundClass volume/pitch/LowPassFilter/AttenuationScale.");
	TSharedPtr<FJsonObject> OpSchema = FNexusSchema::Object()
		.Prop(TEXT("Action"),           FNexusSchema::Enum(TEXT("Action"), { TEXT("Set") }))
		.Prop(TEXT("Volume"),           FNexusSchema::Num(TEXT("Volume multiplier [0,∞)")))
		.Prop(TEXT("Pitch"),            FNexusSchema::Num(TEXT("Pitch multiplier [0,∞)")))
		.Prop(TEXT("LowPassFilter"),    FNexusSchema::Num(TEXT("Low-pass cutoff (Hz)")))
		.Prop(TEXT("AttenuationScale"), FNexusSchema::Num(TEXT("Attenuation distance scale")))
		.Required({ TEXT("Action") })
		.Build();
	Out.InputSchema = FNexusSchema::Object()
		.Prop(TEXT("AssetPath"),  FNexusSchema::Str(TEXT("SoundClass asset path")))
		.Prop(TEXT("Operations"), FNexusSchema::ArrayOf(TEXT("Batch ops (at least one)"), OpSchema.ToSharedRef()))
		.Required({ TEXT("AssetPath"), TEXT("Operations") })
		.Build();
	Out.Tags = { FNexusMcpTags::Write, FNexusMcpTags::Data };
	Out.ExtraSearchKeywords = { TEXT("Sound"), TEXT("Class"), TEXT("Volume"), TEXT("Pitch"), TEXT("filter") };
	Out.RelatedCapabilities = { TEXT("GetAssetSoundClass"), TEXT("CreateAssetSoundClass") };
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
	if (Op->HasField(TEXT("Volume")))           SC->Properties.Volume                   = static_cast<float>(Op->GetNumberField(TEXT("Volume")));
	if (Op->HasField(TEXT("Pitch")))            SC->Properties.Pitch                    = static_cast<float>(Op->GetNumberField(TEXT("Pitch")));
	if (Op->HasField(TEXT("LowPassFilter")))    SC->Properties.LowPassFilterFrequency   = static_cast<float>(Op->GetNumberField(TEXT("LowPassFilter")));
	if (Op->HasField(TEXT("AttenuationScale"))) SC->Properties.AttenuationDistanceScale = static_cast<float>(Op->GetNumberField(TEXT("AttenuationScale")));
	SClassState(Ctx)->bDirty = true;
	Ctx.Entry->SetStringField(TEXT("name"),          SC->GetName());
	Ctx.Entry->SetNumberField(TEXT("Volume"),        SC->Properties.Volume);
	Ctx.Entry->SetNumberField(TEXT("Pitch"),         SC->Properties.Pitch);
	Ctx.Entry->SetNumberField(TEXT("LowPassFilter"), SC->Properties.LowPassFilterFrequency);
}

bool FNexusManageAssetSoundClassCapability::PrepareTarget(
	const TSharedPtr<FJsonObject>& Args,
	TSharedPtr<FJsonObject>& Entry,
	void*& OutTarget,
	FString& OutError) const
{
	const FString AssetPath = FNexusArgs(Args).Str(TEXT("AssetPath"));
	Entry->SetStringField(TEXT("Path"), AssetPath);
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

void FNexusManageAssetSoundClassCapability::FinalizeTarget(void* Target) const
{
	FSoundClassActionState* State = static_cast<FSoundClassActionState*>(Target);
	if (!State) return;
	if (State->bDirty && State->SC) State->SC->MarkPackageDirty();
	delete State;
}

void FNexusManageAssetSoundClassCapability::RegisterActions(TMap<FString, FNexusActionHandler>& OutHandlers) const
{
	OutHandlers.Add(TEXT("Set"), &HandleSClass_Set);
}

REGISTER_MCP_CAPABILITY(FNexusManageAssetSoundClassCapability)
