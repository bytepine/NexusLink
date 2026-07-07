// Copyright byteyang. All Rights Reserved.

#include "Capabilities/Asset/Audio/NexusGetAssetSoundClassCapability.h"
#include "NexusCapabilityRegistry.h"
#include "NexusMcpSchemaBuilder.h"
#include "Utils/NexusCapabilityResultBuilder.h"
#include "Sound/SoundClass.h"
#include "NexusMcpTool.h"

void FGetAssetSoundClassCapability::BuildDefinition(FNexusCapabilityDefinition& Out) const
{
	Out.Name = TEXT("get_asset_sound_class");
	Out.Description = TEXT("读取 SoundClass：volume/pitch/lowPassFilter/parentClass/childClasses。");
	Out.InputSchema = FNexusSchema::Object()
		.Prop(TEXT("assetPath"), FNexusSchema::Str(TEXT("SoundClass 资产路径")))
		.Required({ TEXT("assetPath") })
		.Build();
	Out.Tags = { FNexusMcpTags::Readonly, FNexusMcpTags::Data };
	Out.ExtraSearchKeywords = { TEXT("sound"), TEXT("class"), TEXT("volume"), TEXT("pitch"), TEXT("hierarchy") };
	Out.RelatedCapabilities = { TEXT("create_asset_sound_class"), TEXT("manage_asset_sound_class") };
}

FCapabilityResult FGetAssetSoundClassCapability::Execute(const TSharedPtr<FJsonObject>& Arguments) const
{
	return FNexusCapabilityResultBuilder::Build([&](auto& OutEntries, auto& OutTop, auto& OutError)
	{
		if (!Arguments.IsValid() || !Arguments->HasField(TEXT("assetPath")))
		{
			OutError = TEXT("缺少 assetPath");
			return;
		}

		const FString AssetPath = Arguments->GetStringField(TEXT("assetPath"));
		USoundClass* SC = LoadObject<USoundClass>(nullptr, *AssetPath);
		if (!SC)
		{
			FNexusCapabilityResultBuilder::AddEntryError(OutEntries,
				FString::Printf(TEXT("加载 SoundClass 失败: %s"), *AssetPath));
			return;
		}

		TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
		Entry->SetStringField(TEXT("name"),                SC->GetName());
		Entry->SetStringField(TEXT("path"),                SC->GetPathName());
		Entry->SetNumberField(TEXT("volume"),              SC->Properties.Volume);
		Entry->SetNumberField(TEXT("pitch"),               SC->Properties.Pitch);
		Entry->SetNumberField(TEXT("lowPassFilter"),       SC->Properties.LowPassFilterFrequency);
		Entry->SetNumberField(TEXT("attenuationScale"),    SC->Properties.AttenuationDistanceScale);
		Entry->SetNumberField(TEXT("childClassCount"),     SC->ChildClasses.Num());

		if (SC->ParentClass)
			Entry->SetStringField(TEXT("parentClass"), SC->ParentClass->GetPathName());

		TArray<TSharedPtr<FJsonValue>> ChildArr;
		for (USoundClass* Child : SC->ChildClasses)
		{
			if (Child)
				ChildArr.Add(MakeShared<FJsonValueString>(Child->GetPathName()));
		}
		Entry->SetArrayField(TEXT("childClasses"), ChildArr);

		OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
	});
}

REGISTER_MCP_CAPABILITY(FGetAssetSoundClassCapability)
