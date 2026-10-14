// Copyright byteyang. All Rights Reserved.

#include "Capabilities/Asset/Level/NexusCreateAssetLevelCapability.h"
#include "NexusCapabilityRegistry.h"
#include "NexusMcpSchemaBuilder.h"
#include "Utils/NexusAssetUtils.h"
#include "Utils/NexusCapabilityResultBuilder.h"
#include "Utils/NexusArgs.h"
#include "Engine/World.h"
#include "NexusMcpTool.h"
#if WITH_EDITOR
#include "Factories/WorldFactory.h"
#include "Editor.h"
#endif

void FNexusCreateAssetLevelCapability::BuildDefinition(FNexusCapabilityDefinition& Out) const
{
	Out.Name = TEXT("CreateAssetLevel");
	Out.Description = TEXT("Create empty level (UWorld). Editor only.");
	Out.InputSchema = FNexusSchema::Object()
		.Prop(TEXT("AssetPath"), FNexusSchema::Str(TEXT("Level package path")))
		.Required({ TEXT("AssetPath") })
		.Build();
	Out.Tags = { FNexusMcpTags::Write, FNexusMcpTags::Editor };
	Out.ExtraSearchKeywords = { TEXT("Level"), TEXT("map"), TEXT("World"), TEXT("umap") };
	Out.RelatedCapabilities = { TEXT("GetAssetLevel"), TEXT("ManageAssetLevel") };
	Out.WhenToUse = TEXT("Create empty map; Actors via ManageAssetLevel");
	Out.Prerequisites = { TEXT("EditorOnly") };
}

FCapabilityResult FNexusCreateAssetLevelCapability::Execute(const TSharedPtr<FJsonObject>& Arguments) const
{
	return FNexusCapabilityResultBuilder::Build([&](auto& OutEntries, auto& OutTop, auto& OutError)
	{
		const FNexusArgs A(Arguments);
#if !WITH_EDITOR
		OutError = TEXT("CreateAssetLevel only available in editor builds");
		return;
#else
		const FString AssetPath = A.Str(TEXT("AssetPath"));
		if (LoadObject<UWorld>(nullptr, *AssetPath))
		{
			FNexusCapabilityResultBuilder::AddEntryError(OutEntries,
				FString::Printf(TEXT("Level already exists: %s"), *AssetPath));
			return;
		}
		UPackage* Package = CreatePackage(*AssetPath);
		if (!Package) { FNexusCapabilityResultBuilder::AddEntryError(OutEntries, TEXT("Failed to create package")); return; }
		const FString AssetName = FPaths::GetBaseFilename(AssetPath);
		UWorldFactory* Factory = NewObject<UWorldFactory>();
		UObject* NewWorld = Factory->FactoryCreateNew(UWorld::StaticClass(), Package, FName(*AssetName),
			RF_Public | RF_Standalone, nullptr, GWarn);
		if (!NewWorld) { FNexusCapabilityResultBuilder::AddEntryError(OutEntries, TEXT("WorldFactory Createfailed")); return; }
		FNexusAssetUtils::NotifyAndSaveCreated(Package, NewWorld, AssetPath);
		TSharedPtr<FJsonObject> Entry = FNexusCapabilityResultBuilder::MakeCreatedEntry(NewWorld);
		Entry->SetStringField(TEXT("AssetType"), TEXT("World"));
		OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
#endif
	});
}

REGISTER_MCP_CAPABILITY(FNexusCreateAssetLevelCapability)
