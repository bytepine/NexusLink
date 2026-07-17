// Copyright byteyang. All Rights Reserved.

#include "Capabilities/Asset/WorldPartition/NexusGetAssetDataLayerCapability.h"
#include "Utils/NexusVersionCompat.h"

#if NX_UE_HAS_DATA_LAYER_ASSET

#include "Utils/NexusCapabilityResultBuilder.h"
#include "NexusCapabilityRegistry.h"
#include "NexusMcpSchemaBuilder.h"
#include "Utils/NexusAssetUtils.h"
#include "NexusMcpTool.h"
#include "WorldPartition/DataLayer/DataLayerAsset.h"

void FGetAssetDataLayerCapability::BuildDefinition(FNexusCapabilityDefinition& Out) const
{
	Out.Name        = TEXT("get_asset_data_layer");
	Out.SearchAssetTypes = {TEXT("DataLayerAsset")};
	Out.Description = TEXT("读取 DataLayer 资产属性：类型（Runtime/Editor）、调试颜色（≥UE5.1）。写用 manage_asset_data_layer。");
	Out.InputSchema = FNexusSchema::Object()
		.Prop(TEXT("assetPath"),  FNexusSchema::Str(TEXT("DataLayerAsset 路径")))
		.Prop(TEXT("assetPaths"), FNexusSchema::StrArr(TEXT("批量路径")))
		.Required({ TEXT("assetPath") })
		.Build();
	Out.Tags = { FNexusMcpTags::Readonly, FNexusMcpTags::Editor };
	Out.ExtraSearchKeywords = { TEXT("datalayer"), TEXT("data layer"), TEXT("world partition"), TEXT("streaming") };
	Out.RelatedCapabilities = { TEXT("manage_asset_data_layer"), TEXT("create_asset_data_layer"), TEXT("search_asset") };
	Out.WhenToUse = TEXT("读取 DataLayerAsset 的类型与调试颜色（≥UE5.1）");
}

FCapabilityResult FGetAssetDataLayerCapability::Execute(const TSharedPtr<FJsonObject>& Arguments) const
{
	return FNexusCapabilityResultBuilder::Build([&](auto& OutEntries, auto& OutTop, auto& OutError)
	{
		TArray<FString> Paths;
		FString Single;
		if (Arguments->TryGetStringField(TEXT("assetPath"), Single) && !Single.IsEmpty())
			Paths.Add(Single);
		const TArray<TSharedPtr<FJsonValue>>* Arr;
		if (Arguments->TryGetArrayField(TEXT("assetPaths"), Arr))
			for (auto& V : *Arr) { FString S; if (V->TryGetString(S) && !S.IsEmpty()) Paths.AddUnique(S); }

		if (Paths.IsEmpty())
		{
			FNexusCapability::EmitError(OutEntries, {}, TEXT("assetPath 为空"));
			return;
		}

		for (const FString& AssetPath : Paths)
		{
			UDataLayerAsset* DLA = FNexusAssetUtils::LoadAssetWithFallback<UDataLayerAsset>(AssetPath);
			if (!DLA)
			{
				TSharedPtr<FJsonObject> ErrObj = MakeShared<FJsonObject>();
				ErrObj->SetStringField(TEXT("assetPath"), AssetPath);
				ErrObj->SetStringField(TEXT("error"), TEXT("DataLayerAsset 未找到"));
				OutEntries.Add(MakeShared<FJsonValueObject>(ErrObj));
				continue;
			}

			TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
			Entry->SetStringField(TEXT("assetPath"), DLA->GetPathName());
			Entry->SetStringField(TEXT("assetType"), TEXT("DataLayerAsset"));
			Entry->SetStringField(TEXT("name"),      DLA->GetName());

			// 类型
			const EDataLayerType LayerType = DLA->GetType();
			FString TypeStr;
			switch (LayerType)
			{
			case EDataLayerType::Runtime: TypeStr = TEXT("Runtime"); break;
			case EDataLayerType::Editor:  TypeStr = TEXT("Editor");  break;
			default:                      TypeStr = TEXT("Unknown"); break;
			}
			Entry->SetStringField(TEXT("type"),       TypeStr);
			Entry->SetNumberField(TEXT("typeValue"),   static_cast<int32>(LayerType));
			Entry->SetBoolField(TEXT("isRuntime"),     DLA->IsRuntime());

			// 调试颜色
			const FColor DebugColor = DLA->GetDebugColor();
			Entry->SetStringField(TEXT("debugColor"), DebugColor.ToHex());

			OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
		}
	});
}

REGISTER_MCP_CAPABILITY(FGetAssetDataLayerCapability)

#else // NX_UE_HAS_DATA_LAYER_ASSET

void FGetAssetDataLayerCapability::BuildDefinition(FNexusCapabilityDefinition& Out) const
{
	Out.Name        = TEXT("get_asset_data_layer");
	Out.Description = TEXT("（当前引擎版本不支持 DataLayerAsset，需要 UE5.1+）");
	Out.InputSchema = FNexusSchema::Object().Build();
	Out.Tags = { FNexusMcpTags::Readonly, FNexusMcpTags::Editor };
}

FCapabilityResult FGetAssetDataLayerCapability::Execute(const TSharedPtr<FJsonObject>&) const
{
	return FNexusCapabilityResultBuilder::Build([](auto& OutEntries, auto&, auto& OutError)
	{
		OutError = TEXT("get_asset_data_layer 需要 UE5.1+");
	});
}

REGISTER_MCP_CAPABILITY(FGetAssetDataLayerCapability)

#endif // NX_UE_HAS_DATA_LAYER_ASSET
