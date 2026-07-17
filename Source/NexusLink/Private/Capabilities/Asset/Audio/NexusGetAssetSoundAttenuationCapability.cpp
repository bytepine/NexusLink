// Copyright byteyang. All Rights Reserved.

#include "Capabilities/Asset/Audio/NexusGetAssetSoundAttenuationCapability.h"
#include "NexusCapabilityRegistry.h"
#include "NexusMcpSchemaBuilder.h"
#include "Utils/NexusCapabilityResultBuilder.h"
#include "Sound/SoundAttenuation.h"
#include "NexusMcpTool.h"

void FGetAssetSoundAttenuationCapability::BuildDefinition(FNexusCapabilityDefinition& Out) const
{
	Out.Name = TEXT("get_asset_sound_attenuation");
	Out.SearchAssetTypes = {TEXT("SoundAttenuation")};
	Out.Description = TEXT("读取 SoundAttenuation：shape/innerRadius/falloffDistance/bAttenuate/bSpatialize。");
	Out.InputSchema = FNexusSchema::Object()
		.Prop(TEXT("assetPath"), FNexusSchema::Str(TEXT("SoundAttenuation 资产路径")))
		.Required({ TEXT("assetPath") })
		.Build();
	Out.Tags = { FNexusMcpTags::Readonly, FNexusMcpTags::Data };
	Out.ExtraSearchKeywords = { TEXT("attenuation"), TEXT("sound"), TEXT("distance"), TEXT("radius"), TEXT("shape") };
	Out.RelatedCapabilities = { TEXT("create_asset_sound_attenuation"), TEXT("manage_asset_sound_attenuation") };
}

FCapabilityResult FGetAssetSoundAttenuationCapability::Execute(const TSharedPtr<FJsonObject>& Arguments) const
{
	return FNexusCapabilityResultBuilder::Build([&](auto& OutEntries, auto& OutTop, auto& OutError)
	{
		if (!Arguments.IsValid() || !Arguments->HasField(TEXT("assetPath")))
		{
			OutError = TEXT("缺少 assetPath");
			return;
		}

		const FString AssetPath = Arguments->GetStringField(TEXT("assetPath"));
		USoundAttenuation* SA = LoadObject<USoundAttenuation>(nullptr, *AssetPath);
		if (!SA)
		{
			FNexusCapabilityResultBuilder::AddEntryError(OutEntries,
				FString::Printf(TEXT("加载 SoundAttenuation 失败: %s"), *AssetPath));
			return;
		}

		const UEnum* ShapeEnum = StaticEnum<EAttenuationShape::Type>();
		const FString ShapeStr = ShapeEnum
			? ShapeEnum->GetNameStringByValue((int64)SA->Attenuation.AttenuationShape.GetValue())
			: FString::FromInt((int32)SA->Attenuation.AttenuationShape.GetValue());

		TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
		Entry->SetStringField(TEXT("name"),             SA->GetName());
		Entry->SetStringField(TEXT("path"),             SA->GetPathName());
		Entry->SetStringField(TEXT("shape"),            ShapeStr);
		Entry->SetNumberField(TEXT("shapeValue"),       (double)(int32)SA->Attenuation.AttenuationShape.GetValue());
		Entry->SetNumberField(TEXT("innerRadius"),      SA->Attenuation.AttenuationShapeExtents.X);
		Entry->SetNumberField(TEXT("falloffDistance"),  SA->Attenuation.FalloffDistance);
		Entry->SetBoolField(TEXT("bAttenuate"),         SA->Attenuation.bAttenuate != 0);
		Entry->SetBoolField(TEXT("bSpatialize"),        SA->Attenuation.bSpatialize != 0);
		Entry->SetNumberField(TEXT("dBAtMax"),          SA->Attenuation.dBAttenuationAtMax);
		OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
	});
}

REGISTER_MCP_CAPABILITY(FGetAssetSoundAttenuationCapability)
