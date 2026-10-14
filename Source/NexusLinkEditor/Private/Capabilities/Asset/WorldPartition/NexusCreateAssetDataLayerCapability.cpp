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

void FNexusCreateAssetDataLayerCapability::BuildDefinition(FNexusCapabilityDefinition& Out) const
{
	Out.Name        = TEXT("CreateAssetDataLayer");
	Out.Description = TEXT("Create DataLayer asset (UDataLayerAsset, ≥UE5.1). type: Runtime or Editor.");
	Out.InputSchema = FNexusSchema::Object()
		.Prop(TEXT("AssetPath"),   FNexusSchema::Str(TEXT("New DataLayer asset full path, e.g. /Game/WorldData/DL_New")))
		.Prop(TEXT("type"),        FNexusSchema::Str(TEXT("Runtime or Editor (default Runtime)")))
		.Prop(TEXT("DebugColor"),  FNexusSchema::Str(TEXT("Debug color (#RRGGBB or color name, optional)")))
		.Required({ TEXT("AssetPath") })
		.Build();
	Out.Tags = { FNexusMcpTags::Write, FNexusMcpTags::Editor };
	Out.ExtraSearchKeywords = { TEXT("datalayer"), TEXT("data layer"), TEXT("world partition"), TEXT("streaming"), TEXT("Level") };
	Out.RelatedCapabilities = { TEXT("GetAssetDataLayer"), TEXT("ManageAssetDataLayer"), TEXT("SearchAsset") };
	Out.WhenToUse = TEXT("Create World Partition DataLayer (≥UE5.1) with Runtime/Editor type and debug color");
}

FCapabilityResult FNexusCreateAssetDataLayerCapability::Execute(const TSharedPtr<FJsonObject>& Arguments) const
{
	return FNexusCapabilityResultBuilder::Build([&](auto& OutEntries, auto& OutTop, auto& OutError)
	{
		const FNexusArgs A(Arguments);
		const FString FullPath = A.Str(TEXT("AssetPath"));
		const FString AssetName = FPaths::GetBaseFilename(FullPath);

		if (FNexusAssetUtils::LoadAssetWithFallback<UDataLayerAsset>(FullPath))
		{
			TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
			Entry->SetStringField(TEXT("Path"),   FullPath);
			Entry->SetStringField(TEXT("AssetType"),   TEXT("DataLayerAsset"));
			Entry->SetBoolField(TEXT("bAlreadyExists"), true);
			OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
			return;
		}

		UPackage* Package = CreatePackage(*FullPath);
		if (!Package)
		{
			FNexusCapability::EmitError(OutEntries, {{TEXT("Path"), FullPath}}, TEXT("Unable to create Package"));
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
		TypeStr = FNexusArgs(Arguments).Str(TEXT("type"), TypeStr);
		EDataLayerType LayerType = EDataLayerType::Runtime;
		if (TypeStr.Equals(TEXT("Editor"), ESearchCase::IgnoreCase))
			LayerType = EDataLayerType::Editor;
		DLA->SetType(LayerType);

		// 设置调试颜色（可选）
		FString ColorStr;
		if (Arguments->TryGetStringField(TEXT("DebugColor"), ColorStr) && !ColorStr.IsEmpty())
		{
			FColor Color = FColor::FromHex(ColorStr);
			DLA->SetDebugColor(Color);
		}
#endif

		DLA->MarkPackageDirty();
		FAssetRegistryModule::AssetCreated(DLA);
		FNexusAssetUtils::NotifyAndSaveCreated(Package, DLA, FullPath);

		TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
		Entry->SetStringField(TEXT("Path"), DLA->GetPathName());
		Entry->SetStringField(TEXT("AssetType"), TEXT("DataLayerAsset"));
		Entry->SetStringField(TEXT("type"),      DLA->GetType() == EDataLayerType::Runtime ? TEXT("Runtime") : TEXT("Editor"));
		Entry->SetBoolField(TEXT("bCreated"),     true);
		OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
	});
}

REGISTER_MCP_CAPABILITY(FNexusCreateAssetDataLayerCapability)

#else // NX_UE_HAS_DATA_LAYER_ASSET

#include "Utils/NexusCapabilityResultBuilder.h"
#include "NexusCapabilityRegistry.h"
#include "NexusMcpSchemaBuilder.h"
#include "NexusMcpTool.h"

void FNexusCreateAssetDataLayerCapability::BuildDefinition(FNexusCapabilityDefinition& Out) const
{
	Out.Name        = TEXT("CreateAssetDataLayer");
	Out.Description = TEXT("(DataLayerAsset requires UE5.1+ on this engine)");
	Out.InputSchema = FNexusSchema::Object().Build();
	Out.Tags = { FNexusMcpTags::Write, FNexusMcpTags::Editor };
}

FCapabilityResult FNexusCreateAssetDataLayerCapability::Execute(const TSharedPtr<FJsonObject>&) const
{
	return FNexusCapabilityResultBuilder::Build([](auto& OutEntries, auto&, auto& OutError)
	{
		OutError = TEXT("CreateAssetDataLayer requires UE5.1+");
	});
}

REGISTER_MCP_CAPABILITY(FNexusCreateAssetDataLayerCapability)

#endif // NX_UE_HAS_DATA_LAYER_ASSET
