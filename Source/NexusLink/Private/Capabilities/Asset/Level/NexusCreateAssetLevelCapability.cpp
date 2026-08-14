// Copyright byteyang. All Rights Reserved.

#include "Capabilities/Asset/Level/NexusCreateAssetLevelCapability.h"
#include "NexusCapabilityRegistry.h"
#include "NexusMcpSchemaBuilder.h"
#include "Utils/NexusAssetUtils.h"
#include "Utils/NexusCapabilityResultBuilder.h"
#include "Engine/World.h"
#include "NexusMcpTool.h"
#if WITH_EDITOR
#include "Factories/WorldFactory.h"
#include "Editor.h"
#endif

void FCreateAssetLevelCapability::BuildDefinition(FNexusCapabilityDefinition& Out) const
{
	Out.Name = TEXT("create_asset_level");
	Out.Description = TEXT("创建空白关卡（UWorld）。仅 Editor。");
	Out.InputSchema = FNexusSchema::Object()
		.Prop(TEXT("assetPath"), FNexusSchema::Str(TEXT("关卡包路径")))
		.Required({ TEXT("assetPath") })
		.Build();
	Out.Tags = { FNexusMcpTags::Write, FNexusMcpTags::Editor };
	Out.ExtraSearchKeywords = { TEXT("level"), TEXT("map"), TEXT("world"), TEXT("umap") };
	Out.RelatedCapabilities = { TEXT("get_asset_level"), TEXT("manage_asset_level") };
	Out.WhenToUse = TEXT("新建空白地图；Actor 用 manage_asset_level");
	Out.Prerequisites = { TEXT("editor_only") };
}

FCapabilityResult FCreateAssetLevelCapability::Execute(const TSharedPtr<FJsonObject>& Arguments) const
{
	return FNexusCapabilityResultBuilder::Build([&](auto& OutEntries, auto& OutTop, auto& OutError)
	{
#if !WITH_EDITOR
		OutError = TEXT("create_asset_level 仅在编辑器构建可用");
		return;
#else
		if (!Arguments.IsValid() || !Arguments->HasField(TEXT("assetPath")))
		{
			OutError = TEXT("缺少 assetPath");
			return;
		}
		const FString AssetPath = Arguments->GetStringField(TEXT("assetPath"));
		if (LoadObject<UWorld>(nullptr, *AssetPath))
		{
			FNexusCapabilityResultBuilder::AddEntryError(OutEntries,
				FString::Printf(TEXT("Level already exists: %s"), *AssetPath));
			return;
		}
		UPackage* Package = CreatePackage(*AssetPath);
		if (!Package) { FNexusCapabilityResultBuilder::AddEntryError(OutEntries, TEXT("创建包失败")); return; }
		const FString AssetName = FPaths::GetBaseFilename(AssetPath);
		UWorldFactory* Factory = NewObject<UWorldFactory>();
		UObject* NewWorld = Factory->FactoryCreateNew(UWorld::StaticClass(), Package, FName(*AssetName),
			RF_Public | RF_Standalone, nullptr, GWarn);
		if (!NewWorld) { FNexusCapabilityResultBuilder::AddEntryError(OutEntries, TEXT("WorldFactory 创建失败")); return; }
		FNexusAssetUtils::NotifyAndSaveCreated(Package, NewWorld, AssetPath);
		TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
		Entry->SetStringField(TEXT("name"), NewWorld->GetName());
		Entry->SetStringField(TEXT("path"), NewWorld->GetPathName());
		Entry->SetStringField(TEXT("assetType"), TEXT("World"));
		OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
#endif
	});
}

REGISTER_MCP_CAPABILITY(FCreateAssetLevelCapability)
