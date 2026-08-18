// Copyright byteyang. All Rights Reserved.

#include "Capabilities/Asset/Audio/NexusManageAssetSoundAttenuationCapability.h"
#include "NexusCapabilityRegistry.h"
#include "NexusMcpSchemaBuilder.h"
#include "Utils/NexusCapabilityResultBuilder.h"
#include "Utils/NexusJsonUtils.h"
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

FCapabilityResult FManageAssetSoundAttenuationCapability::Execute(const TSharedPtr<FJsonObject>& Arguments) const
{
	return FNexusCapabilityResultBuilder::Build([&](auto& OutEntries, auto& OutTop, auto& OutError)
	{
		const FNexusArgs A(Arguments);

		const FString AssetPath = A.Str(TEXT("assetPath"));
		USoundAttenuation* SA = LoadObject<USoundAttenuation>(nullptr, *AssetPath);
		if (!SA)
		{
			OutError = FString::Printf(TEXT("Failed to load SoundAttenuation: %s"), *AssetPath);
			return;
		}

		const TArray<TSharedPtr<FJsonValue>> Ops = FNexusJsonUtils::ExtractOperations(Arguments);
		if (Ops.Num() == 0)
		{
			OutError = TEXT("Missing or empty operations");
			return;
		}

		for (const TSharedPtr<FJsonValue>& OpVal : Ops)
		{
			TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
			const TSharedPtr<FJsonObject>* OpPtr = nullptr;
			if (!OpVal.IsValid() || !OpVal->TryGetObject(OpPtr) || !OpPtr)
			{
				Entry->SetStringField(TEXT("error"), TEXT("Invalid operation item"));
				OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
				continue;
			}
			const TSharedPtr<FJsonObject>& Op = *OpPtr;

			const FString Action = FNexusArgs(Op).Str(TEXT("action")).ToLower();
			Entry->SetStringField(TEXT("action"), Action);
			if (Action != TEXT("set"))
			{
				Entry->SetStringField(TEXT("error"), FString::Printf(TEXT("Unsupported operation: '%s' (set only)"), *Action));
				OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
				continue;
			}

			if (Op->HasField(TEXT("innerRadius")))
				SA->Attenuation.AttenuationShapeExtents.X = (float)Op->GetNumberField(TEXT("innerRadius"));
			if (Op->HasField(TEXT("falloffDistance")))
				SA->Attenuation.FalloffDistance = (float)Op->GetNumberField(TEXT("falloffDistance"));
			if (Op->HasField(TEXT("shapeValue")))
				SA->Attenuation.AttenuationShape = EAttenuationShape::Type((int32)Op->GetNumberField(TEXT("shapeValue")));
			if (Op->HasField(TEXT("bAttenuate")))
				SA->Attenuation.bAttenuate = Op->GetBoolField(TEXT("bAttenuate")) ? 1 : 0;
			if (Op->HasField(TEXT("bSpatialize")))
				SA->Attenuation.bSpatialize = Op->GetBoolField(TEXT("bSpatialize")) ? 1 : 0;
			if (Op->HasField(TEXT("dBAtMax")))
				SA->Attenuation.dBAttenuationAtMax = (float)Op->GetNumberField(TEXT("dBAtMax"));

			SA->MarkPackageDirty();

			Entry->SetStringField(TEXT("name"),            SA->GetName());
			Entry->SetNumberField(TEXT("innerRadius"),     SA->Attenuation.AttenuationShapeExtents.X);
			Entry->SetNumberField(TEXT("falloffDistance"), SA->Attenuation.FalloffDistance);
			OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
		}
	});
}

REGISTER_MCP_CAPABILITY(FManageAssetSoundAttenuationCapability)
