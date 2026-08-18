// Copyright byteyang. All Rights Reserved.

#include "Capabilities/Asset/WorldPartition/NexusCreateAssetDataLayerCapability.h"
#include "Utils/NexusVersionCompat.h"

#if NX_UE_HAS_DATA_LAYER_ASSET

#include "Utils/NexusCapabilityResultBuilder.h"
#include "Utils/NexusArgs.h"
#include "NexusCapabilityRegistry.h"
#include "NexusMcpSchemaBuilder.h"
#include "Utils/NexusAssetUtils.h"
#include "NexusMcpTool.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "UObject/Package.h"
#include "WorldPartition/DataLayer/DataLayerAsset.h"

void FCreateAssetDataLayerCapability::BuildDefinition(FNexusCapabilityDefinition& Out) const
{
	Out.Name        = TEXT("create_asset_data_layer");
	Out.Description = TEXT("Create DataLayer asset (UDataLayerAsset, ≥UE5.1). type: Runtime or Editor.; use get_asset_ for readsdata_layer.");
	Out.InputSchema = FNexusSchema::Object()
		.Prop(TEXT("assetPath"),   FNexusSchema::Str(TEXT("New DataLayer asset full path, e.g. /Game/WorldData/DL_New")))
		.Prop(TEXT("type"),        FNexusSchema::Str(TEXT("Runtime or Editor (default Runtime)")))
		.Prop(TEXT("debugColor"),  FNexusSchema::Str(TEXT("Debug color (#RRGGBB or color name, optional)")))
		.Required({ TEXT("assetPath") })
		.Build();
	Out.Tags = { FNexusMcpTags::Write, FNexusMcpTags::Editor };
	Out.ExtraSearchKeywords = { TEXT("datalayer"), TEXT("data layer"), TEXT("world partition"), TEXT("streaming"), TEXT("level") };
	Out.RelatedCapabilities = { TEXT("get_asset_data_layer"), TEXT("manage_asset_data_layer"), TEXT("search_asset") };
	Out.WhenToUse = TEXT("Create World Partition DataLayer (≥UE5.1) with Runtime/Editor type and debug color");
}

FCapabilityResult FCreateAssetDataLayerCapability::Execute(const TSharedPtr<FJsonObject>& Arguments) const
{
	return FNexusCapabilityResultBuilder::Build([&](auto& OutEntries, auto& OutTop, auto& OutError)
	{
		const FNexusArgs A(Arguments);
		const FString FullPath = A.Str(TEXT("assetPath"));
		const FString AssetName = FPaths::GetBaseFilename(FullPath);

		if (FNexusAssetUtils::LoadAssetWithFallback<UDataLayerAsset>(FullPath))
		{
			TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
			Entry->SetStringField(TEXT("path"),   FullPath);
			Entry->SetStringField(TEXT("assetType"),   TEXT("DataLayerAsset"));
			Entry->SetBoolField(TEXT("alreadyExists"), true);
			OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
			return;
		}

		UPackage* Package = CreatePackage(*FullPath);
		if (!Package)
		{
			FNexusCapability::EmitError(OutEntries, {{TEXT("path"), FullPath}}, TEXT("Unable to create Package"));
			return;
		}

		UDataLayerAsset* DLA = NewObject<UDataLayerAsset>(Package, *AssetName,
			RF_Public | RF_Standalone | RF_Transactional);
		if (!DLA)
		{
			FNexusCapability::EmitError(OutEntries, {{TEXT("assetName"), AssetName}}, TEXT("NewObject<UDataLayerAsset> failed"));
			return;
		}

#if WITH_EDITOR
		// 设置类型
		FString TypeStr;
		Arguments->TryGetStringField(TEXT("type"), TypeStr);
		EDataLayerType LayerType = EDataLayerType::Runtime;
		if (TypeStr.Equals(TEXT("Editor"), ESearchCase::IgnoreCase))
			LayerType = EDataLayerType::Editor;
		DLA->SetType(LayerType);

		// 设置调试颜色（可选）
		FString ColorStr;
		if (Arguments->TryGetStringField(TEXT("debugColor"), ColorStr) && !ColorStr.IsEmpty())
		{
			FColor Color = FColor::FromHex(ColorStr);
			DLA->SetDebugColor(Color);
		}
#endif

		DLA->MarkPackageDirty();
		FAssetRegistryModule::AssetCreated(DLA);
		FNexusAssetUtils::NotifyAndSaveCreated(Package, DLA, FullPath);

		TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
		Entry->SetStringField(TEXT("path"), DLA->GetPathName());
		Entry->SetStringField(TEXT("assetType"), TEXT("DataLayerAsset"));
		Entry->SetStringField(TEXT("type"),      DLA->GetType() == EDataLayerType::Runtime ? TEXT("Runtime") : TEXT("Editor"));
		Entry->SetBoolField(TEXT("created"),     true);
		OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
	});
}

REGISTER_MCP_CAPABILITY(FCreateAssetDataLayerCapability)

#else // NX_UE_HAS_DATA_LAYER_ASSET

#include "Utils/NexusCapabilityResultBuilder.h"
#include "NexusCapabilityRegistry.h"
#include "NexusMcpSchemaBuilder.h"
#include "NexusMcpTool.h"

void FCreateAssetDataLayerCapability::BuildDefinition(FNexusCapabilityDefinition& Out) const
{
	Out.Name        = TEXT("create_asset_data_layer");
	Out.Description = TEXT("(DataLayerAsset requires UE5.1+ on this engine)");
	Out.InputSchema = FNexusSchema::Object().Build();
	Out.Tags = { FNexusMcpTags::Write, FNexusMcpTags::Editor };
}

FCapabilityResult FCreateAssetDataLayerCapability::Execute(const TSharedPtr<FJsonObject>&) const
{
	return FNexusCapabilityResultBuilder::Build([](auto& OutEntries, auto&, auto& OutError)
	{
		OutError = TEXT("create_asset_data_layer requires UE5.1+");
	});
}

REGISTER_MCP_CAPABILITY(FCreateAssetDataLayerCapability)

#endif // NX_UE_HAS_DATA_LAYER_ASSET
