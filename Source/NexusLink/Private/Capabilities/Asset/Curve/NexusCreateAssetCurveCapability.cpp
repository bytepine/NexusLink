// Copyright byteyang. All Rights Reserved.

#include "Capabilities/Asset/Curve/NexusCreateAssetCurveCapability.h"
#include "NexusCapabilityRegistry.h"
#include "NexusMcpSchemaBuilder.h"
#include "Utils/NexusAssetUtils.h"
#include "Utils/NexusCapabilityResultBuilder.h"
#include "Utils/NexusArgs.h"
#include "Curves/CurveFloat.h"
#include "Curves/CurveVector.h"
#include "Curves/CurveLinearColor.h"
#include "Engine/CurveTable.h"
#include "NexusMcpTool.h"

void FCreateAssetCurveCapability::BuildDefinition(FNexusCapabilityDefinition& Out) const
{
	Out.Name = TEXT("create_asset_curve");
	Out.Description = TEXT("Create curve asset: CurveFloat/CurveVector/CurveLinearColor/CurveTable.");
	Out.InputSchema = FNexusSchema::Object()
		.Prop(TEXT("assetPath"), FNexusSchema::Str(TEXT("Asset package path")))
		.Prop(TEXT("curveType"), FNexusSchema::Str(TEXT("float (default)/ vector / linear_color / curve_table"), TEXT("float")))
		.Required({ TEXT("assetPath") })
		.Build();
	Out.Tags = { FNexusMcpTags::Write, FNexusMcpTags::Data };
	Out.ExtraSearchKeywords = { TEXT("curve"), TEXT("float"), TEXT("timeline"), TEXT("gradient"), TEXT("table") };
	Out.RelatedCapabilities = { TEXT("get_asset_curve"), TEXT("manage_asset_curve") };
	Out.WhenToUse = TEXT("Create empty curve asset; write keys via manage");
}

FCapabilityResult FCreateAssetCurveCapability::Execute(const TSharedPtr<FJsonObject>& Arguments) const
{
	return FNexusCapabilityResultBuilder::Build([&](auto& OutEntries, auto& OutTop, auto& OutError)
	{
		const FNexusArgs A(Arguments);

		const FString AssetPath = A.Str(TEXT("assetPath"));
		FString CurveType = TEXT("float");
		if (Arguments->HasField(TEXT("curveType")))
			CurveType = A.Str(TEXT("curveType")).ToLower();

		UClass* CurveClass = UCurveFloat::StaticClass();
		if (CurveType == TEXT("vector"))
		{
			CurveClass = UCurveVector::StaticClass();
		}
		else if (CurveType == TEXT("linear_color") || CurveType == TEXT("linearcolor"))
		{
			CurveClass = UCurveLinearColor::StaticClass();
		}
		else if (CurveType == TEXT("curve_table") || CurveType == TEXT("curvetable"))
		{
			CurveClass = UCurveTable::StaticClass();
		}

		const FNexusAssetUtils::FAssetCreateOutcome Created =
			FNexusAssetUtils::CreatePlainAsset(AssetPath, CurveClass);
		if (!Created.Ok())
		{
			FNexusCapabilityResultBuilder::AddEntryError(OutEntries, Created.Error);
			return;
		}
		UObject* NewAsset = Created.Asset;

		TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
		Entry->SetStringField(TEXT("name"), NewAsset->GetName());
		Entry->SetStringField(TEXT("path"), NewAsset->GetPathName());
		Entry->SetStringField(TEXT("curveType"), CurveType);
		OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
	});
}

REGISTER_MCP_CAPABILITY(FCreateAssetCurveCapability)
