// Copyright byteyang. All Rights Reserved.

#include "Capabilities/Asset/Audio/NexusCreateAssetSoundAttenuationCapability.h"
#include "NexusCapabilityRegistry.h"
#include "NexusMcpSchemaBuilder.h"
#include "Utils/NexusAssetUtils.h"
#include "Utils/NexusCapabilityResultBuilder.h"
#include "Sound/SoundAttenuation.h"
#include "NexusMcpTool.h"

void FCreateAssetSoundAttenuationCapability::BuildDefinition(FNexusCapabilityDefinition& Out) const
{
	Out.Name = TEXT("create_asset_sound_attenuation");
	Out.Description = TEXT("创建 SoundAttenuation 资产（声音衰减曲线/形状设置）。");
	Out.InputSchema = FNexusSchema::Object()
		.Prop(TEXT("assetPath"),       FNexusSchema::Str(TEXT("SoundAttenuation 包路径")))
		.Prop(TEXT("innerRadius"),     FNexusSchema::Num(TEXT("衰减球形内半径（默认400）")))
		.Prop(TEXT("falloffDistance"), FNexusSchema::Num(TEXT("衰减距离（默认3600）")))
		.Required({ TEXT("assetPath") })
		.Build();
	Out.Tags = { FNexusMcpTags::Write, FNexusMcpTags::Data };
	Out.ExtraSearchKeywords = { TEXT("attenuation"), TEXT("sound"), TEXT("audio"), TEXT("distance"), TEXT("radius") };
	Out.RelatedCapabilities = { TEXT("get_asset_sound_attenuation"), TEXT("manage_asset_sound_attenuation") };
	Out.WhenToUse = TEXT("创建声音衰减设置资产");
}

FCapabilityResult FCreateAssetSoundAttenuationCapability::Execute(const TSharedPtr<FJsonObject>& Arguments) const
{
	return FNexusCapabilityResultBuilder::Build([&](auto& OutEntries, auto& OutTop, auto& OutError)
	{
		if (!Arguments.IsValid() || !Arguments->HasField(TEXT("assetPath")))
		{
			OutError = TEXT("缺少 assetPath");
			return;
		}

		const FString AssetPath = Arguments->GetStringField(TEXT("assetPath"));

		if (LoadObject<USoundAttenuation>(nullptr, *AssetPath))
		{
			FNexusCapabilityResultBuilder::AddEntryError(OutEntries,
				FString::Printf(TEXT("SoundAttenuation already exists: %s"), *AssetPath));
			return;
		}

		UPackage* Package = CreatePackage(*AssetPath);
		if (!Package) { FNexusCapabilityResultBuilder::AddEntryError(OutEntries, TEXT("创建包失败")); return; }

		const FString AssetName = FPaths::GetBaseFilename(AssetPath);
		USoundAttenuation* SA = NewObject<USoundAttenuation>(Package, *AssetName, RF_Public | RF_Standalone);
		if (!SA) { FNexusCapabilityResultBuilder::AddEntryError(OutEntries, TEXT("SoundAttenuation 创建失败")); return; }

		if (Arguments->HasField(TEXT("innerRadius")))
			SA->Attenuation.AttenuationShapeExtents.X = (float)Arguments->GetNumberField(TEXT("innerRadius"));
		if (Arguments->HasField(TEXT("falloffDistance")))
			SA->Attenuation.FalloffDistance = (float)Arguments->GetNumberField(TEXT("falloffDistance"));

		FNexusAssetUtils::NotifyAndSaveCreated(Package, SA, AssetPath);

		TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
		Entry->SetStringField(TEXT("name"),           SA->GetName());
		Entry->SetStringField(TEXT("path"),           SA->GetPathName());
		Entry->SetNumberField(TEXT("innerRadius"),    SA->Attenuation.AttenuationShapeExtents.X);
		Entry->SetNumberField(TEXT("falloffDistance"),SA->Attenuation.FalloffDistance);
		Entry->SetBoolField(TEXT("success"),          true);
		OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
	});
}

REGISTER_MCP_CAPABILITY(FCreateAssetSoundAttenuationCapability)
