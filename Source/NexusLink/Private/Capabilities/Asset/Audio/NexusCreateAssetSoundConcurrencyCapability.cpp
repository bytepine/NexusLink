// Copyright byteyang. All Rights Reserved.

#include "Capabilities/Asset/Audio/NexusCreateAssetSoundConcurrencyCapability.h"
#include "NexusCapabilityRegistry.h"
#include "NexusMcpSchemaBuilder.h"
#include "Utils/NexusAssetUtils.h"
#include "Utils/NexusCapabilityResultBuilder.h"
#include "Utils/NexusArgs.h"
#include "Sound/SoundConcurrency.h"
#include "NexusMcpTool.h"

void FCreateAssetSoundConcurrencyCapability::BuildDefinition(FNexusCapabilityDefinition& Out) const
{
	Out.Name = TEXT("create_asset_sound_concurrency");
	Out.Description = TEXT("Create SoundConcurrency asset (max concurrent instances).");
	Out.InputSchema = FNexusSchema::Object()
		.Prop(TEXT("assetPath"), FNexusSchema::Str(TEXT("SoundConcurrency package path")))
		.Prop(TEXT("maxCount"),  FNexusSchema::Int(TEXT("Max concurrent instances (default 16)"), 16, 1))
		.Required({ TEXT("assetPath") })
		.Build();
	Out.Tags = { FNexusMcpTags::Write, FNexusMcpTags::Data };
	Out.ExtraSearchKeywords = { TEXT("concurrency"), TEXT("sound"), TEXT("limit"), TEXT("audio") };
	Out.RelatedCapabilities = { TEXT("get_asset_sound_concurrency"), TEXT("manage_asset_sound_concurrency") };
	Out.WhenToUse = TEXT("Create sound concurrency limit asset");
}

FCapabilityResult FCreateAssetSoundConcurrencyCapability::Execute(const TSharedPtr<FJsonObject>& Arguments) const
{
	return FNexusCapabilityResultBuilder::Build([&](auto& OutEntries, auto& OutTop, auto& OutError)
	{
		const FNexusArgs A(Arguments);

		const FString AssetPath = A.Str(TEXT("assetPath"));

		if (LoadObject<USoundConcurrency>(nullptr, *AssetPath))
		{
			FNexusCapabilityResultBuilder::AddEntryError(OutEntries,
				FString::Printf(TEXT("SoundConcurrency already exists: %s"), *AssetPath));
			return;
		}

		UPackage* Package = CreatePackage(*AssetPath);
		if (!Package) { FNexusCapabilityResultBuilder::AddEntryError(OutEntries, TEXT("Failed to create package")); return; }

		const FString AssetName = FPaths::GetBaseFilename(AssetPath);
		USoundConcurrency* SC = NewObject<USoundConcurrency>(Package, *AssetName, RF_Public | RF_Standalone);
		if (!SC) { FNexusCapabilityResultBuilder::AddEntryError(OutEntries, TEXT("SoundConcurrency Createfailed")); return; }

		if (Arguments->HasField(TEXT("maxCount")))
			SC->Concurrency.MaxCount = FMath::Max(1, (int32)A.Num(TEXT("maxCount")));

		FNexusAssetUtils::NotifyAndSaveCreated(Package, SC, AssetPath);

		TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
		Entry->SetStringField(TEXT("name"),     SC->GetName());
		Entry->SetStringField(TEXT("path"),     SC->GetPathName());
		Entry->SetNumberField(TEXT("maxCount"), SC->Concurrency.MaxCount);
		OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
	});
}

REGISTER_MCP_CAPABILITY(FCreateAssetSoundConcurrencyCapability)
