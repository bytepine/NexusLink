// Copyright byteyang. All Rights Reserved.

#include "Capabilities/Asset/WorldPartition/NexusGetAssetDataLayerCapability.h"
#include "Utils/NexusVersionCompat.h"

#if NX_UE_HAS_DATA_LAYER_ASSET

#include "Utils/NexusCapabilityResultBuilder.h"
#include "Utils/NexusArgs.h"
#include "NexusCapabilityRegistry.h"
#include "NexusMcpSchemaBuilder.h"
#include "Utils/NexusAssetUtils.h"
#include "NexusMcpTool.h"
#include "WorldPartition/DataLayer/DataLayerAsset.h"

void FNexusGetAssetDataLayerCapability::BuildDefinition(FNexusCapabilityDefinition& Out) const
{
	Out.Name        = TEXT("GetAssetDataLayer");
	Out.SearchAssetTypes = {TEXT("DataLayerAsset")};
	Out.Description = TEXT("Read DataLayer asset props: type (Runtime/Editor), debug color (>=UE5.1). Use ManageAssetDataLayer for writes.");
	Out.InputSchema = FNexusSchema::Object()
		.Prop(TEXT("AssetPath"), FNexusSchema::Str(TEXT("DataLayerAsset path")))
		.Required({ TEXT("AssetPath") })
		.Build();
	Out.Tags = { FNexusMcpTags::Readonly, FNexusMcpTags::Editor };
	Out.ExtraSearchKeywords = { TEXT("datalayer"), TEXT("data layer"), TEXT("world partition"), TEXT("streaming") };
	Out.RelatedCapabilities = { TEXT("ManageAssetDataLayer"), TEXT("CreateAssetDataLayer"), TEXT("SearchAsset") };
	Out.WhenToUse = TEXT("Read DataLayerAsset type and debug color (>=UE5.1)");
}

FCapabilityResult FNexusGetAssetDataLayerCapability::Execute(const TSharedPtr<FJsonObject>& Arguments) const
{
	return FNexusCapabilityResultBuilder::Build([&](auto& OutEntries, auto& OutTop, auto& OutError)
	{
		const FNexusArgs A(Arguments);
		const FString AssetPath = A.Str(TEXT("AssetPath"));

		UDataLayerAsset* DLA = FNexusAssetUtils::LoadAssetWithFallback<UDataLayerAsset>(AssetPath);
		if (!DLA)
		{
			TSharedPtr<FJsonObject> ErrObj = MakeShared<FJsonObject>();
			ErrObj->SetStringField(TEXT("Path"), AssetPath);
			ErrObj->SetStringField(TEXT("Error"), TEXT("DataLayerAsset not found"));
			OutEntries.Add(MakeShared<FJsonValueObject>(ErrObj));
			return;
		}

		TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
		Entry->SetStringField(TEXT("Path"), DLA->GetPathName());
		Entry->SetStringField(TEXT("AssetType"), TEXT("DataLayerAsset"));
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
		Entry->SetNumberField(TEXT("TypeValue"),   static_cast<int32>(LayerType));
		Entry->SetBoolField(TEXT("bIsRuntime"),     DLA->IsRuntime());

		// 调试颜色
		const FColor DebugColor = DLA->GetDebugColor();
		Entry->SetStringField(TEXT("DebugColor"), DebugColor.ToHex());

		OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
	});
}

REGISTER_MCP_CAPABILITY(FNexusGetAssetDataLayerCapability)

#else // NX_UE_HAS_DATA_LAYER_ASSET

#include "Utils/NexusCapabilityResultBuilder.h"
#include "NexusCapabilityRegistry.h"
#include "NexusMcpSchemaBuilder.h"
#include "NexusMcpTool.h"

void FNexusGetAssetDataLayerCapability::BuildDefinition(FNexusCapabilityDefinition& Out) const
{
	Out.Name        = TEXT("GetAssetDataLayer");
	Out.Description = TEXT("(DataLayerAsset requires UE5.1+ on this engine)");
	Out.InputSchema = FNexusSchema::Object().Build();
	Out.Tags = { FNexusMcpTags::Readonly, FNexusMcpTags::Editor };
}

FCapabilityResult FNexusGetAssetDataLayerCapability::Execute(const TSharedPtr<FJsonObject>&) const
{
	return FNexusCapabilityResultBuilder::Build([](auto& OutEntries, auto&, auto& OutError)
	{
		OutError = TEXT("GetAssetDataLayer requires UE5.1+");
	});
}

REGISTER_MCP_CAPABILITY(FNexusGetAssetDataLayerCapability)

#endif // NX_UE_HAS_DATA_LAYER_ASSET
