// Copyright byteyang. All Rights Reserved.

#include "Capabilities/Asset/WorldPartition/NexusCreateAssetDataLayerCapability.h"
#include "Utils/NexusVersionCompat.h"

#if NX_UE_HAS_DATA_LAYER_ASSET

#include "Utils/NexusCapabilityResultBuilder.h"
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
	Out.Description = TEXT("创建 DataLayer 资产（UDataLayerAsset，≥UE5.1）。type: Runtime 或 Editor。读用 get_asset_data_layer。");
	Out.InputSchema = FNexusSchema::Object()
		.Prop(TEXT("packagePath"), FNexusSchema::Str(TEXT("资产包路径，如 /Game/WorldData")))
		.Prop(TEXT("assetName"),   FNexusSchema::Str(TEXT("资产名称")))
		.Prop(TEXT("type"),        FNexusSchema::Str(TEXT("Runtime 或 Editor（默认 Runtime）")))
		.Prop(TEXT("debugColor"),  FNexusSchema::Str(TEXT("调试颜色（十六进制 #RRGGBB 或颜色名，可选）")))
		.Required({ TEXT("packagePath"), TEXT("assetName") })
		.Build();
	Out.Tags = { FNexusMcpTags::Editor };
	Out.ExtraSearchKeywords = { TEXT("datalayer"), TEXT("data layer"), TEXT("world partition"), TEXT("streaming"), TEXT("level") };
	Out.RelatedCapabilities = { TEXT("get_asset_data_layer"), TEXT("manage_asset_data_layer"), TEXT("search_asset") };
	Out.WhenToUse = TEXT("新建 World Partition DataLayer 资产（≥UE5.1），设置 Runtime/Editor 类型及调试颜色");
}

FCapabilityResult FCreateAssetDataLayerCapability::Execute(const TSharedPtr<FJsonObject>& Arguments) const
{
	return FNexusCapabilityResultBuilder::Build([&](auto& OutEntries, auto& OutTop, auto& OutError)
	{
		FString PackagePath, AssetName;
		if (!FNexusCapability::RequireString(Arguments, TEXT("packagePath"), PackagePath, OutEntries, {})) return;
		if (!FNexusCapability::RequireString(Arguments, TEXT("assetName"),   AssetName,   OutEntries, {})) return;

		if (!PackagePath.EndsWith(TEXT("/")))
			PackagePath += TEXT("/");
		const FString FullPath = PackagePath + AssetName;

		if (FNexusAssetUtils::LoadAssetWithFallback<UDataLayerAsset>(FullPath))
		{
			TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
			Entry->SetStringField(TEXT("assetPath"),   FullPath);
			Entry->SetStringField(TEXT("assetType"),   TEXT("DataLayerAsset"));
			Entry->SetBoolField(TEXT("alreadyExists"), true);
			OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
			return;
		}

		UPackage* Package = CreatePackage(*FullPath);
		if (!Package)
		{
			FNexusCapability::EmitError(OutEntries, {{TEXT("packagePath"), PackagePath}}, TEXT("无法创建 Package"));
			return;
		}

		UDataLayerAsset* DLA = NewObject<UDataLayerAsset>(Package, *AssetName,
			RF_Public | RF_Standalone | RF_Transactional);
		if (!DLA)
		{
			FNexusCapability::EmitError(OutEntries, {{TEXT("assetName"), AssetName}}, TEXT("NewObject<UDataLayerAsset> 失败"));
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
		Entry->SetStringField(TEXT("assetPath"), DLA->GetPathName());
		Entry->SetStringField(TEXT("assetType"), TEXT("DataLayerAsset"));
		Entry->SetStringField(TEXT("type"),      DLA->GetType() == EDataLayerType::Runtime ? TEXT("Runtime") : TEXT("Editor"));
		Entry->SetBoolField(TEXT("created"),     true);
		OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
	});
}

REGISTER_MCP_CAPABILITY(FCreateAssetDataLayerCapability)

#else // NX_UE_HAS_DATA_LAYER_ASSET

void FCreateAssetDataLayerCapability::BuildDefinition(FNexusCapabilityDefinition& Out) const
{
	Out.Name        = TEXT("create_asset_data_layer");
	Out.Description = TEXT("（当前引擎版本不支持 DataLayerAsset，需要 UE5.1+）");
	Out.InputSchema = FNexusSchema::Object().Build();
	Out.Tags = { FNexusMcpTags::Editor };
}

FCapabilityResult FCreateAssetDataLayerCapability::Execute(const TSharedPtr<FJsonObject>&) const
{
	return FNexusCapabilityResultBuilder::Build([](auto& OutEntries, auto&, auto& OutError)
	{
		OutError = TEXT("create_asset_data_layer 需要 UE5.1+");
	});
}

REGISTER_MCP_CAPABILITY(FCreateAssetDataLayerCapability)

#endif // NX_UE_HAS_DATA_LAYER_ASSET
