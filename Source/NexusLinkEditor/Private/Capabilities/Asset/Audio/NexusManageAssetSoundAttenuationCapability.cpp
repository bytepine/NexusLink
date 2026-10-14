// Copyright byteyang. All Rights Reserved.

#include "Capabilities/Asset/Audio/NexusManageAssetSoundAttenuationCapability.h"
#include "NexusCapabilityRegistry.h"
#include "NexusMcpSchemaBuilder.h"
#include "Utils/NexusArgs.h"
#include "Sound/SoundAttenuation.h"
#include "NexusMcpTool.h"

void FNexusManageAssetSoundAttenuationCapability::BuildDefinition(FNexusCapabilityDefinition& Out) const
{
	Out.Name = TEXT("ManageAssetSoundAttenuation");
	Out.SearchAssetTypes = {TEXT("SoundAttenuation")};
	Out.Description = TEXT("Set SoundAttenuation: InnerRadius/FalloffDistance/ShapeValue/bAttenuate/bSpatialize.");
	TSharedPtr<FJsonObject> OpSchema = FNexusSchema::Object()
		.Prop(TEXT("Action"),          FNexusSchema::Enum(TEXT("Action"), { TEXT("Set") }))
		.Prop(TEXT("InnerRadius"),     FNexusSchema::Num(TEXT("Inner radius (sphere = Sphere Radius, cm)")))
		.Prop(TEXT("FalloffDistance"), FNexusSchema::Num(TEXT("Falloff distance (cm)")))
		.Prop(TEXT("ShapeValue"),      FNexusSchema::Int(TEXT("Shape enum: 0=Sphere,1=Capsule,2=Box,3=Cone")))
		.Prop(TEXT("bAttenuate"),      FNexusSchema::Bool(TEXT("Enable distance attenuation")))
		.Prop(TEXT("bSpatialize"),     FNexusSchema::Bool(TEXT("Enable spatialization")))
		.Prop(TEXT("DBAtMax"),         FNexusSchema::Num(TEXT("Max attenuation (dB, Natural Sound)")))
		.Required({ TEXT("Action") })
		.Build();
	Out.InputSchema = FNexusSchema::Object()
		.Prop(TEXT("AssetPath"),  FNexusSchema::Str(TEXT("SoundAttenuation asset path")))
		.Prop(TEXT("Operations"), FNexusSchema::ArrayOf(TEXT("Batch ops (at least one)"), OpSchema.ToSharedRef()))
		.Required({ TEXT("AssetPath"), TEXT("Operations") })
		.Build();
	Out.Tags = { FNexusMcpTags::Write, FNexusMcpTags::Data };
	Out.ExtraSearchKeywords = { TEXT("attenuation"), TEXT("Sound"), TEXT("Radius"), TEXT("distance"), TEXT("Shape") };
	Out.RelatedCapabilities = { TEXT("GetAssetSoundAttenuation"), TEXT("CreateAssetSoundAttenuation") };
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
	if (Op->HasField(TEXT("InnerRadius")))
		SA->Attenuation.AttenuationShapeExtents.X = static_cast<float>(Op->GetNumberField(TEXT("InnerRadius")));
	if (Op->HasField(TEXT("FalloffDistance")))
		SA->Attenuation.FalloffDistance = static_cast<float>(Op->GetNumberField(TEXT("FalloffDistance")));
	if (Op->HasField(TEXT("ShapeValue")))
		SA->Attenuation.AttenuationShape = EAttenuationShape::Type(static_cast<int32>(Op->GetNumberField(TEXT("ShapeValue"))));
	if (Op->HasField(TEXT("bAttenuate")))
		SA->Attenuation.bAttenuate = Op->GetBoolField(TEXT("bAttenuate")) ? 1 : 0;
	if (Op->HasField(TEXT("bSpatialize")))
		SA->Attenuation.bSpatialize = Op->GetBoolField(TEXT("bSpatialize")) ? 1 : 0;
	if (Op->HasField(TEXT("DBAtMax")))
		SA->Attenuation.dBAttenuationAtMax = static_cast<float>(Op->GetNumberField(TEXT("DBAtMax")));
	AttenState(Ctx)->bDirty = true;
	Ctx.Entry->SetStringField(TEXT("name"),            SA->GetName());
	Ctx.Entry->SetNumberField(TEXT("InnerRadius"),     SA->Attenuation.AttenuationShapeExtents.X);
	Ctx.Entry->SetNumberField(TEXT("FalloffDistance"), SA->Attenuation.FalloffDistance);
}

bool FNexusManageAssetSoundAttenuationCapability::PrepareTarget(
	const TSharedPtr<FJsonObject>& Args,
	TSharedPtr<FJsonObject>& Entry,
	void*& OutTarget,
	FString& OutError) const
{
	const FString AssetPath = FNexusArgs(Args).Str(TEXT("AssetPath"));
	Entry->SetStringField(TEXT("Path"), AssetPath);
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

void FNexusManageAssetSoundAttenuationCapability::FinalizeTarget(void* Target) const
{
	FAttenActionState* State = static_cast<FAttenActionState*>(Target);
	if (!State) return;
	if (State->bDirty && State->SA) State->SA->MarkPackageDirty();
	delete State;
}

void FNexusManageAssetSoundAttenuationCapability::RegisterActions(TMap<FString, FNexusActionHandler>& OutHandlers) const
{
	OutHandlers.Add(TEXT("Set"), &HandleAtten_Set);
}

REGISTER_MCP_CAPABILITY(FNexusManageAssetSoundAttenuationCapability)
