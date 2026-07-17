// Copyright byteyang. All Rights Reserved.

#include "Capabilities/Asset/WorldPartition/NexusManageAssetDataLayerCapability.h"
#include "Utils/NexusVersionCompat.h"

#if NX_UE_HAS_DATA_LAYER_ASSET

#include "Utils/NexusCapabilityResultBuilder.h"
#include "NexusCapabilityRegistry.h"
#include "NexusMcpSchemaBuilder.h"
#include "Utils/NexusAssetUtils.h"
#include "NexusMcpTool.h"
#include "WorldPartition/DataLayer/DataLayerAsset.h"

void FManageAssetDataLayerCapability::BuildDefinition(FNexusCapabilityDefinition& Out) const
{
	Out.Name        = TEXT("manage_asset_data_layer");
	Out.SearchAssetTypes = {TEXT("DataLayerAsset")};
	Out.Description = TEXT("修改 DataLayer 资产属性（≥UE5.1，需要编辑器）：set_type（Runtime/Editor）、set_debug_color（#RRGGBB）。");
	Out.InputSchema = FNexusSchema::Object()
		.Prop(TEXT("assetPath"),  FNexusSchema::Str(TEXT("DataLayerAsset 路径")))
		.Prop(TEXT("operations"), FNexusSchema::ArrOfObj(TEXT("操作列表，每项需 action 字段")))
		.Required({ TEXT("assetPath"), TEXT("operations") })
		.Build();
	Out.Tags = { FNexusMcpTags::Editor };
	Out.ExtraSearchKeywords = { TEXT("datalayer"), TEXT("data layer"), TEXT("world partition"), TEXT("streaming"), TEXT("color"), TEXT("type") };
	Out.RelatedCapabilities = { TEXT("get_asset_data_layer"), TEXT("create_asset_data_layer") };
	Out.WhenToUse = TEXT("修改 DataLayerAsset 的类型（Runtime/Editor）或调试颜色（≥UE5.1）");
}

FCapabilityResult FManageAssetDataLayerCapability::Execute(const TSharedPtr<FJsonObject>& Arguments) const
{
	return FNexusCapabilityResultBuilder::Build([&](auto& OutEntries, auto& OutTop, auto& OutError)
	{
		FString AssetPath;
		if (!FNexusCapability::RequireString(Arguments, TEXT("assetPath"), AssetPath, OutEntries, {})) return;

		UDataLayerAsset* DLA = FNexusAssetUtils::LoadAssetWithFallback<UDataLayerAsset>(AssetPath);
		if (!DLA)
		{
			FNexusCapability::EmitError(OutEntries, {{TEXT("assetPath"), AssetPath}},
				FString::Printf(TEXT("DataLayerAsset 未找到: %s"), *AssetPath));
			return;
		}

		const TArray<TSharedPtr<FJsonValue>>* OpsArr = nullptr;
		if (!Arguments->TryGetArrayField(TEXT("operations"), OpsArr) || !OpsArr || OpsArr->IsEmpty())
		{
			FNexusCapability::EmitError(OutEntries, {{TEXT("assetPath"), AssetPath}}, TEXT("operations 数组为空"));
			return;
		}

		TArray<TSharedPtr<FJsonValue>> Results;
		for (const TSharedPtr<FJsonValue>& Val : *OpsArr)
		{
			const TSharedPtr<FJsonObject>* OpObjPtr = nullptr;
			if (!Val->TryGetObject(OpObjPtr) || !OpObjPtr) continue;
			const TSharedPtr<FJsonObject>& Op = *OpObjPtr;

			FString Action;
			Op->TryGetStringField(TEXT("action"), Action);

			TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
			Result->SetStringField(TEXT("action"), Action);

#if WITH_EDITOR
			if (Action == TEXT("set_type"))
			{
				FString TypeStr;
				Op->TryGetStringField(TEXT("type"), TypeStr);
				EDataLayerType LayerType = EDataLayerType::Runtime;
				if (TypeStr.Equals(TEXT("Editor"), ESearchCase::IgnoreCase))
					LayerType = EDataLayerType::Editor;
				DLA->SetType(LayerType);
				Result->SetBoolField(TEXT("success"), true);
				Result->SetStringField(TEXT("type"), TypeStr.IsEmpty() ? TEXT("Runtime") : TypeStr);
			}
			else if (Action == TEXT("set_debug_color"))
			{
				FString ColorStr;
				Op->TryGetStringField(TEXT("color"), ColorStr);
				if (ColorStr.IsEmpty())
				{
					Result->SetStringField(TEXT("error"), TEXT("set_debug_color 需要 color 字段（#RRGGBB）"));
				}
				else
				{
					FColor Color = FColor::FromHex(ColorStr);
					DLA->SetDebugColor(Color);
					Result->SetBoolField(TEXT("success"), true);
					Result->SetStringField(TEXT("color"), Color.ToHex());
				}
			}
			else
#endif // WITH_EDITOR
			{
				Result->SetStringField(TEXT("error"), FString::Printf(
					TEXT("未知 action '%s'，支持: set_type / set_debug_color（需编辑器）"),
					*Action));
			}

			Results.Add(MakeShared<FJsonValueObject>(Result));
		}

		DLA->MarkPackageDirty();

		TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
		Entry->SetStringField(TEXT("assetPath"), AssetPath);
		Entry->SetArrayField(TEXT("results"), Results);
		OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
	});
}

REGISTER_MCP_CAPABILITY(FManageAssetDataLayerCapability)

#else // NX_UE_HAS_DATA_LAYER_ASSET

void FManageAssetDataLayerCapability::BuildDefinition(FNexusCapabilityDefinition& Out) const
{
	Out.Name        = TEXT("manage_asset_data_layer");
	Out.Description = TEXT("（当前引擎版本不支持 DataLayerAsset，需要 UE5.1+）");
	Out.InputSchema = FNexusSchema::Object().Build();
	Out.Tags = { FNexusMcpTags::Editor };
}

FCapabilityResult FManageAssetDataLayerCapability::Execute(const TSharedPtr<FJsonObject>&) const
{
	return FNexusCapabilityResultBuilder::Build([](auto& OutEntries, auto&, auto& OutError)
	{
		OutError = TEXT("manage_asset_data_layer 需要 UE5.1+");
	});
}

REGISTER_MCP_CAPABILITY(FManageAssetDataLayerCapability)

#endif // NX_UE_HAS_DATA_LAYER_ASSET
