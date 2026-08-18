// Copyright byteyang. All Rights Reserved.

#include "Capabilities/Asset/WorldPartition/NexusManageAssetDataLayerCapability.h"
#include "Utils/NexusVersionCompat.h"

#if NX_UE_HAS_DATA_LAYER_ASSET

#include "Utils/NexusCapabilityResultBuilder.h"
#include "Utils/NexusJsonUtils.h"
#include "NexusCapabilityRegistry.h"
#include "NexusMcpSchemaBuilder.h"
#include "Utils/NexusAssetUtils.h"
#include "NexusMcpTool.h"
#include "WorldPartition/DataLayer/DataLayerAsset.h"

void FManageAssetDataLayerCapability::BuildDefinition(FNexusCapabilityDefinition& Out) const
{
	Out.Name        = TEXT("manage_asset_data_layer");
	Out.SearchAssetTypes = {TEXT("DataLayerAsset")};
	Out.Description = TEXT("Edit DataLayer asset (≥UE5.1, editor): set_type/set_debug_color.");
	Out.InputSchema = FNexusSchema::Object()
		.Prop(TEXT("assetPath"),  FNexusSchema::Str(TEXT("DataLayerAsset path")))
		.Prop(TEXT("operations"), FNexusSchema::ArrOfObj(TEXT("Operation list; each item requires action")))
		.Required({ TEXT("assetPath"), TEXT("operations") })
		.Build();
	Out.Tags = { FNexusMcpTags::Write, FNexusMcpTags::Editor };
	Out.ExtraSearchKeywords = { TEXT("datalayer"), TEXT("data layer"), TEXT("world partition"), TEXT("streaming"), TEXT("color"), TEXT("type") };
	Out.RelatedCapabilities = { TEXT("get_asset_data_layer"), TEXT("create_asset_data_layer") };
	Out.WhenToUse = TEXT("Edit DataLayerAsset type (Runtime/Editor) or debug color (≥UE5.1)");
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
			FNexusCapability::EmitError(OutEntries, {{TEXT("path"), AssetPath}},
				FString::Printf(TEXT("DataLayerAsset not found: %s"), *AssetPath));
			return;
		}

		const TArray<TSharedPtr<FJsonValue>> OpsArr = FNexusJsonUtils::ExtractOperations(Arguments);
		if (OpsArr.Num() == 0)
		{
			FNexusCapability::EmitError(OutEntries, {{TEXT("path"), AssetPath}}, TEXT("operations array is empty"));
			return;
		}

		for (const TSharedPtr<FJsonValue>& Val : OpsArr)
		{
			const TSharedPtr<FJsonObject>* OpObjPtr = nullptr;
			if (!Val->TryGetObject(OpObjPtr) || !OpObjPtr) continue;
			const TSharedPtr<FJsonObject>& Op = *OpObjPtr;

			FString Action;
			Op->TryGetStringField(TEXT("action"), Action);

			TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
			Result->SetStringField(TEXT("path"), AssetPath);
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
				Result->SetStringField(TEXT("type"), TypeStr.IsEmpty() ? TEXT("Runtime") : TypeStr);
			}
			else if (Action == TEXT("set_debug_color"))
			{
				FString ColorStr;
				Op->TryGetStringField(TEXT("color"), ColorStr);
				if (ColorStr.IsEmpty())
				{
					Result->SetStringField(TEXT("error"), TEXT("set_debug_color requires color field (#RRGGBB)"));
				}
				else
				{
					FColor Color = FColor::FromHex(ColorStr);
					DLA->SetDebugColor(Color);
					Result->SetStringField(TEXT("color"), Color.ToHex());
				}
			}
			else
#endif // WITH_EDITOR
			{
				Result->SetStringField(TEXT("error"), FString::Printf(
					TEXT("Unknown action '%s'; supported: set_type / set_debug_color (editor required)"),
					*Action));
			}

			OutEntries.Add(MakeShared<FJsonValueObject>(Result));
		}

		DLA->MarkPackageDirty();
	});
}

REGISTER_MCP_CAPABILITY(FManageAssetDataLayerCapability)

#else // NX_UE_HAS_DATA_LAYER_ASSET

#include "Utils/NexusCapabilityResultBuilder.h"
#include "Utils/NexusJsonUtils.h"
#include "NexusCapabilityRegistry.h"
#include "NexusMcpSchemaBuilder.h"
#include "NexusMcpTool.h"

void FManageAssetDataLayerCapability::BuildDefinition(FNexusCapabilityDefinition& Out) const
{
	Out.Name        = TEXT("manage_asset_data_layer");
	Out.Description = TEXT("(DataLayerAsset requires UE5.1+ on this engine)");
	Out.InputSchema = FNexusSchema::Object().Build();
	Out.Tags = { FNexusMcpTags::Write, FNexusMcpTags::Editor };
}

FCapabilityResult FManageAssetDataLayerCapability::Execute(const TSharedPtr<FJsonObject>&) const
{
	return FNexusCapabilityResultBuilder::Build([](auto& OutEntries, auto&, auto& OutError)
	{
		OutError = TEXT("manage_asset_data_layer requires UE5.1+");
	});
}

REGISTER_MCP_CAPABILITY(FManageAssetDataLayerCapability)

#endif // NX_UE_HAS_DATA_LAYER_ASSET
