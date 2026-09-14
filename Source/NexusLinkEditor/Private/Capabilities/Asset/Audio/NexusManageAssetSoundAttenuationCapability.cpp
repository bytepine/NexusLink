// Copyright byteyang. All Rights Reserved.

#include "Capabilities/Asset/Audio/NexusManageAssetSoundAttenuationCapability.h"
#include "NexusCapabilityRegistry.h"
#include "NexusMcpSchemaBuilder.h"
#include "Utils/NexusArgs.h"
#include "Sound/SoundAttenuation.h"
#include "NexusMcpTool.h"

void FManageAssetSoundAttenuationCapability::BuildDefinition(FNexusCapabilityDefinition& Out) const
{
	Out.Name = TEXT("manage_asset_sound_attenuation");
	Out.SearchAssetTypes = {TEXT("SoundAttenuation")};
	Out.Description = TEXT("Set SoundAttenuation: innerRadius/falloffDistance/shapeValue/bAttenuate/bSpatialize.");
	TSharedPtr<FJsonObject> OpSchema = FNexusSchema::Object()
		.Prop(TEXT("action"),          FNexusSchema::Enum(TEXT("Action"), { TEXT("set") }))
		.Prop(TEXT("innerRadius"),     FNexusSchema::Num(TEXT("Inner radius (sphere = Sphere Radius, cm)")))
		.Prop(TEXT("falloffDistance"), FNexusSchema::Num(TEXT("Falloff distance (cm)")))
		.Prop(TEXT("shapeValue"),      FNexusSchema::Int(TEXT("Shape enum: 0=Sphere,1=Capsule,2=Box,3=Cone")))
		.Prop(TEXT("bAttenuate"),      FNexusSchema::Bool(TEXT("Enable distance attenuation")))
		.Prop(TEXT("bSpatialize"),     FNexusSchema::Bool(TEXT("Enable spatialization")))
		.Prop(TEXT("dBAtMax"),         FNexusSchema::Num(TEXT("Max attenuation (dB, Natural Sound)")))
		.Required({ TEXT("action") })
		.Build();
	Out.InputSchema = FNexusSchema::Object()
		.Prop(TEXT("assetPath"),  FNexusSchema::Str(TEXT("SoundAttenuation asset path")))
		.Prop(TEXT("operations"), FNexusSchema::ArrayOf(TEXT("Batch ops (at least one)"), OpSchema.ToSharedRef()))
		.Required({ TEXT("assetPath"), TEXT("operations") })
		.Build();
	Out.Tags = { FNexusMcpTags::Write, FNexusMcpTags::Data };
	Out.ExtraSearchKeywords = { TEXT("attenuation"), TEXT("sound"), TEXT("radius"), TEXT("distance"), TEXT("shape") };
	Out.RelatedCapabilities = { TEXT("get_asset_sound_attenuation"), TEXT("create_asset_sound_attenuation") };
}

struct FAttenActionState
{
	USoundAttenuation* SA = nullptr;
	bool bDirty = false;
};

static FAttenActionState* AttenState(FNexusActionContext& Ctx)
{
	return static_cast<FAttenActionState*>(Ctx.Target);
}

static void HandleAtten_Set(const TSharedPtr<FJsonObject>& Op, FNexusActionContext& Ctx)
{
	USoundAttenuation* SA = AttenState(Ctx)->SA;
	if (Op->HasField(TEXT("innerRadius")))
		SA->Attenuation.AttenuationShapeExtents.X = static_cast<float>(Op->GetNumberField(TEXT("innerRadius")));
	if (Op->HasField(TEXT("falloffDistance")))
		SA->Attenuation.FalloffDistance = static_cast<float>(Op->GetNumberField(TEXT("falloffDistance")));
	if (Op->HasField(TEXT("shapeValue")))
		SA->Attenuation.AttenuationShape = EAttenuationShape::Type(static_cast<int32>(Op->GetNumberField(TEXT("shapeValue"))));
	if (Op->HasField(TEXT("bAttenuate")))
		SA->Attenuation.bAttenuate = Op->GetBoolField(TEXT("bAttenuate")) ? 1 : 0;
	if (Op->HasField(TEXT("bSpatialize")))
		SA->Attenuation.bSpatialize = Op->GetBoolField(TEXT("bSpatialize")) ? 1 : 0;
	if (Op->HasField(TEXT("dBAtMax")))
		SA->Attenuation.dBAttenuationAtMax = static_cast<float>(Op->GetNumberField(TEXT("dBAtMax")));
	AttenState(Ctx)->bDirty = true;
	Ctx.Entry->SetStringField(TEXT("name"),            SA->GetName());
	Ctx.Entry->SetNumberField(TEXT("innerRadius"),     SA->Attenuation.AttenuationShapeExtents.X);
	Ctx.Entry->SetNumberField(TEXT("falloffDistance"), SA->Attenuation.FalloffDistance);
}

bool FManageAssetSoundAttenuationCapability::PrepareTarget(
	const TSharedPtr<FJsonObject>& Args,
	TSharedPtr<FJsonObject>& Entry,
	void*& OutTarget,
	FString& OutError) const
{
	const FString AssetPath = FNexusArgs(Args).Str(TEXT("assetPath"));
	Entry->SetStringField(TEXT("path"), AssetPath);
	USoundAttenuation* SA = LoadObject<USoundAttenuation>(nullptr, *AssetPath);
	if (!SA)
	{
		OutError = FString::Printf(TEXT("Failed to load SoundAttenuation: %s"), *AssetPath);
		return false;
	}
	FAttenActionState* State = new FAttenActionState();
	State->SA = SA;
	OutTarget = State;
	return true;
}

void FManageAssetSoundAttenuationCapability::FinalizeTarget(void* Target) const
{
	FAttenActionState* State = static_cast<FAttenActionState*>(Target);
	if (!State) return;
	if (State->bDirty && State->SA) State->SA->MarkPackageDirty();
	delete State;
}

void FManageAssetSoundAttenuationCapability::RegisterActions(TMap<FString, FNexusActionHandler>& OutHandlers) const
{
	OutHandlers.Add(TEXT("set"), &HandleAtten_Set);
}

REGISTER_MCP_CAPABILITY(FManageAssetSoundAttenuationCapability)
