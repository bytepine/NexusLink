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

void FNexusCreateAssetCurveCapability::BuildDefinition(FNexusCapabilityDefinition& Out) const
{
	Out.Name = TEXT("CreateAssetCurve");
	Out.Description = TEXT("Create curve asset: CurveFloat/CurveVector/CurveLinearColor/CurveTable.");
	Out.InputSchema = FNexusSchema::Object()
		.Prop(TEXT("AssetPath"), FNexusSchema::Str(TEXT("Asset package path")))
		.Prop(TEXT("CurveType"), FNexusSchema::Str(TEXT("float (default)/ vector / LinearColor / CurveTable"), TEXT("float")))
		.Required({ TEXT("AssetPath") })
		.Build();
	Out.Tags = { FNexusMcpTags::Write, FNexusMcpTags::Data };
	Out.ExtraSearchKeywords = { TEXT("curve"), TEXT("float"), TEXT("timeline"), TEXT("gradient"), TEXT("table") };
	Out.RelatedCapabilities = { TEXT("GetAssetCurve"), TEXT("ManageAssetCurve") };
	Out.WhenToUse = TEXT("Create empty curve asset; write keys via manage");
}

FCapabilityResult FNexusCreateAssetCurveCapability::Execute(const TSharedPtr<FJsonObject>& Arguments) const
{
	return FNexusCapabilityResultBuilder::Build([&](auto& OutEntries, auto& OutTop, auto& OutError)
	{
		const FNexusArgs A(Arguments);

		const FString AssetPath = A.Str(TEXT("AssetPath"));
		FString CurveType = TEXT("float");
		if (Arguments->HasField(TEXT("CurveType")))
			CurveType = A.Str(TEXT("CurveType")).ToLower();

		UClass* CurveClass = UCurveFloat::StaticClass();
		if (CurveType == TEXT("Vector"))
		{
			CurveClass = UCurveVector::StaticClass();
		}
		else if (CurveType == TEXT("LinearColor") || CurveType == TEXT("linearcolor"))
		{
			CurveClass = UCurveLinearColor::StaticClass();
		}
		else if (CurveType == TEXT("CurveTable") || CurveType == TEXT("curvetable"))
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

		TSharedPtr<FJsonObject> Entry = FNexusCapabilityResultBuilder::MakeCreatedEntry(NewAsset);
		Entry->SetStringField(TEXT("CurveType"), CurveType);
		OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
	});
}

REGISTER_MCP_CAPABILITY(FNexusCreateAssetCurveCapability)
