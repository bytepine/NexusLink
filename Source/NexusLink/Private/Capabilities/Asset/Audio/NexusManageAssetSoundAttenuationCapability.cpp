// Copyright byteyang. All Rights Reserved.

#include "Capabilities/Asset/Audio/NexusManageAssetSoundAttenuationCapability.h"
#include "NexusCapabilityRegistry.h"
#include "NexusMcpSchemaBuilder.h"
#include "Utils/NexusCapabilityResultBuilder.h"
#include "Sound/SoundAttenuation.h"
#include "NexusMcpTool.h"

void FManageAssetSoundAttenuationCapability::BuildDefinition(FNexusCapabilityDefinition& Out) const
{
	Out.Name = TEXT("manage_asset_sound_attenuation");
	Out.SearchAssetTypes = {TEXT("SoundAttenuation")};
	Out.Description = TEXT("设置 SoundAttenuation：innerRadius/falloffDistance/shapeValue/bAttenuate/bSpatialize。");
	Out.InputSchema = FNexusSchema::Object()
		.Required(TEXT("assetPath"),       FNexusSchema::Str(TEXT("SoundAttenuation 资产路径")))
		.Prop(TEXT("innerRadius"),         FNexusSchema::Num(TEXT("内半径（球形 = Sphere Radius，cm）")))
		.Prop(TEXT("falloffDistance"),     FNexusSchema::Num(TEXT("衰减距离（cm）")))
		.Prop(TEXT("shapeValue"),          FNexusSchema::Int(TEXT("形状枚举值：0=Sphere,1=Capsule,2=Box,3=Cone")))
		.Prop(TEXT("bAttenuate"),          FNexusSchema::Bool(TEXT("启用距离衰减")))
		.Prop(TEXT("bSpatialize"),         FNexusSchema::Bool(TEXT("启用空间化")))
		.Prop(TEXT("dBAtMax"),             FNexusSchema::Num(TEXT("最大衰减量（dB，Natural Sound 算法）")))
		.Build();
	Out.Tags = { FNexusMcpTags::Write, FNexusMcpTags::Data };
	Out.ExtraSearchKeywords = { TEXT("attenuation"), TEXT("sound"), TEXT("radius"), TEXT("distance"), TEXT("shape") };
	Out.RelatedCapabilities = { TEXT("get_asset_sound_attenuation"), TEXT("create_asset_sound_attenuation") };
}

FCapabilityResult FManageAssetSoundAttenuationCapability::Execute(const TSharedPtr<FJsonObject>& Arguments) const
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
			OutError = FString::Printf(TEXT("加载 SoundAttenuation 失败: %s"), *AssetPath);
			return;
		}

		if (Arguments->HasField(TEXT("innerRadius")))
			SA->Attenuation.AttenuationShapeExtents.X = (float)Arguments->GetNumberField(TEXT("innerRadius"));
		if (Arguments->HasField(TEXT("falloffDistance")))
			SA->Attenuation.FalloffDistance = (float)Arguments->GetNumberField(TEXT("falloffDistance"));
		if (Arguments->HasField(TEXT("shapeValue")))
			SA->Attenuation.AttenuationShape = EAttenuationShape::Type((int32)Arguments->GetNumberField(TEXT("shapeValue")));
		if (Arguments->HasField(TEXT("bAttenuate")))
			SA->Attenuation.bAttenuate = Arguments->GetBoolField(TEXT("bAttenuate")) ? 1 : 0;
		if (Arguments->HasField(TEXT("bSpatialize")))
			SA->Attenuation.bSpatialize = Arguments->GetBoolField(TEXT("bSpatialize")) ? 1 : 0;
		if (Arguments->HasField(TEXT("dBAtMax")))
			SA->Attenuation.dBAttenuationAtMax = (float)Arguments->GetNumberField(TEXT("dBAtMax"));

		SA->MarkPackageDirty();

		TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
		Entry->SetStringField(TEXT("name"),           SA->GetName());
		Entry->SetNumberField(TEXT("innerRadius"),    SA->Attenuation.AttenuationShapeExtents.X);
		Entry->SetNumberField(TEXT("falloffDistance"),SA->Attenuation.FalloffDistance);
		Entry->SetBoolField(TEXT("success"),          true);
		OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
	});
}

REGISTER_MCP_CAPABILITY(FManageAssetSoundAttenuationCapability)
