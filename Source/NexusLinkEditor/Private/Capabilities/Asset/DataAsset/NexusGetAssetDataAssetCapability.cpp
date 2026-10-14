// Copyright byteyang. All Rights Reserved.

#include "Capabilities/Asset/DataAsset/NexusGetAssetDataAssetCapability.h"
#include "Utils/NexusCapabilityResultBuilder.h"
#include "Utils/NexusArgs.h"
#include "NexusCapabilityRegistry.h"
#include "NexusMcpTool.h"
#include "NexusMcpSchemaBuilder.h"
#include "Utils/NexusAssetUtils.h"
#include "Utils/NexusPropertyReportUtils.h"
#include "Engine/DataAsset.h"

static TSharedPtr<FJsonObject> HandleDataAsset(UDataAsset* DA, const FString& NameFilter, const TArray<FString>& PropertyPaths, int32 Offset, int32 Limit)
{
	UClass* AssetClass = DA->GetClass();
	int32 Total = 0;
	TArray<TSharedPtr<FJsonValue>> Page = FNexusPropertyReportUtils::BuildEditablePropsPage(
		AssetClass, DA, AssetClass, NameFilter, PropertyPaths, Offset, Limit, Total);
	// ComputeSlice 内部计算了 Start，这里重新取以填 offset 字段
	const int32 Start = FMath::Min(Offset, Total);
	TSharedPtr<FJsonObject> Info = MakeShared<FJsonObject>();
	Info->SetStringField(TEXT("Class"), AssetClass->GetName());
	Info->SetNumberField(TEXT("TotalCount"), Total);
	Info->SetNumberField(TEXT("Offset"),     Start);
	Info->SetNumberField(TEXT("Limit"),      Limit);
	Info->SetArrayField(TEXT("properties"),  Page);
	return Info;
}

void FNexusGetAssetDataAssetCapability::BuildDefinition(FNexusCapabilityDefinition& Out) const
{
	Out.Name = TEXT("GetAssetDataAsset");
	Out.SearchAssetTypes = {TEXT("DataAsset")};
	Out.Description = TEXT("Read DataAsset properties. Includes type/value/inherited; path filter supported.");
	Out.InputSchema = FNexusSchema::Object()
		.Prop(TEXT("AssetPath"), FNexusSchema::Str(TEXT("DataAsset asset path")))
		.Prop(TEXT("NameFilter"),     FNexusSchema::Str(TEXT("Property name filter (/regex/ ^prefix suffix$)")))
		.Prop(TEXT("PropertyPaths"), FNexusSchema::StrArr(TEXT("Exact property name filter (first path segment), e.g. [\"Health\",\"Damage\"]")))
		.Prop(TEXT("Offset"),     FNexusSchema::Int(TEXT("Pagination offset"), 0, 0))
		.Prop(TEXT("Limit"),      FNexusSchema::Int(TEXT("Max items per page"), 100, 1, 500))
		.Required({ TEXT("AssetPath") })
		.Build();
	Out.Tags = {FNexusMcpTags::Readonly, FNexusMcpTags::Data };
	Out.ExtraSearchKeywords = { TEXT("dataasset"), TEXT("udataasset"), TEXT("properties"), TEXT("config") };
	Out.RelatedCapabilities = { TEXT("ManageAssetDataAsset"), TEXT("CreateAssetDataAsset") };
	Out.WhenToUse = TEXT("Read DataAsset properties; no edits");
}

FCapabilityResult FNexusGetAssetDataAssetCapability::Execute(const TSharedPtr<FJsonObject>& Arguments) const
{

	return FNexusCapabilityResultBuilder::Build([&](auto& OutEntries, auto& OutTop, auto& OutError)
	{
		const FNexusArgs A(Arguments);

		const FString Path = A.Str(TEXT("AssetPath"));

		FString NameFilter;
		int32   Offset     = 0;
		int32   Limit      = 100;
		if (Arguments->HasField(TEXT("NameFilter"))) NameFilter = A.Str(TEXT("NameFilter"));
		if (Arguments->HasField(TEXT("Offset")))     Offset     = FMath::Max(0, static_cast<int32>(A.Num(TEXT("Offset"))));
		if (Arguments->HasField(TEXT("Limit")))      Limit      = FMath::Clamp(static_cast<int32>(A.Num(TEXT("Limit"))), 1, 500);

		TArray<FString> PropertyPaths;
		const TArray<TSharedPtr<FJsonValue>>* PPArr = nullptr;
		if (Arguments->TryGetArrayField(TEXT("PropertyPaths"), PPArr) && PPArr)
		{
			for (const TSharedPtr<FJsonValue>& V : *PPArr)
			{
				FString S;
				if (V.IsValid() && V->TryGetString(S) && !S.IsEmpty()) { PropertyPaths.Add(S); }
			}
		}

		TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();

		UObject* Obj = FNexusAssetUtils::LoadAssetWithFallback<UObject>(Path);
		if (!Obj) { Entry->SetStringField(TEXT("Error"), FString::Printf(TEXT("Asset not found: %s"), *Path)); OutEntries.Add(MakeShared<FJsonValueObject>(Entry)); return; }

		UDataAsset* DA = Cast<UDataAsset>(Obj);
		if (!DA) { Entry->SetStringField(TEXT("Error"), FString::Printf(TEXT("Asset is not a DataAsset: %s"), *Path)); OutEntries.Add(MakeShared<FJsonValueObject>(Entry)); return; }

		TSharedPtr<FJsonObject> One = HandleDataAsset(DA, NameFilter, PropertyPaths, Offset, Limit);
		for (const auto& Pair : One->Values) { Entry->SetField(Pair.Key, Pair.Value); }

		OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
	
	});
}

REGISTER_MCP_CAPABILITY(FNexusGetAssetDataAssetCapability)

