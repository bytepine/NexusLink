// Copyright byteyang. All Rights Reserved.

#include "Capabilities/Asset/DataAsset/NexusGetAssetDataAssetCapability.h"
#include "Utils/NexusCapabilityResultBuilder.h"
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
	Info->SetStringField(TEXT("class"), AssetClass->GetName());
	Info->SetNumberField(TEXT("totalCount"), Total);
	Info->SetNumberField(TEXT("offset"),     Start);
	Info->SetNumberField(TEXT("limit"),      Limit);
	Info->SetArrayField(TEXT("properties"),  Page);
	return Info;
}

void FGetAssetDataAssetCapability::BuildDefinition(FNexusCapabilityDefinition& Out) const
{
	Out.Name = TEXT("get_asset_data_asset");
	Out.Description = TEXT("读 DataAsset 属性。含类型/值/是否继承；可路径过滤。");
	Out.InputSchema = FNexusSchema::Object()
		.Prop(TEXT("assetPath"), FNexusSchema::Str(TEXT("DataAsset 资产路径")))
		.Prop(TEXT("nameFilter"),     FNexusSchema::Str(TEXT("属性名过滤（/regex/ ^前缀 后缀$）")))
		.Prop(TEXT("propertyPaths"), FNexusSchema::StrArr(TEXT("精确属性名过滤（首段路径），如 [\"Health\",\"Damage\"]")))
		.Prop(TEXT("offset"),     FNexusSchema::Int(TEXT("分页偏移"), 0, 0))
		.Prop(TEXT("limit"),      FNexusSchema::Int(TEXT("每页最大条数"), 100, 1, 500))
		.Required({ TEXT("assetPath") })
		.Build();
	Out.Tags = {FNexusMcpTags::Readonly, FNexusMcpTags::Data };
	Out.ExtraSearchKeywords = { TEXT("dataasset"), TEXT("udataasset"), TEXT("properties"), TEXT("config") };
	Out.RelatedCapabilities = { TEXT("manage_asset_data_asset"), TEXT("create_asset_data_asset") };
	Out.WhenToUse = TEXT("读 DataAsset 属性；不含编辑");
}

FCapabilityResult FGetAssetDataAssetCapability::Execute(const TSharedPtr<FJsonObject>& Arguments) const
{

	return FNexusCapabilityResultBuilder::Build([&](auto& OutEntries, auto& OutTop, auto& OutError)
	{

		FString Path;
		if (!Arguments.IsValid() || !Arguments->TryGetStringField(TEXT("assetPath"), Path) || Path.IsEmpty())
		{
			OutError = TEXT("缺少 assetPath");
			return;
		}

		FString NameFilter;
		int32   Offset     = 0;
		int32   Limit      = 100;
		if (Arguments->HasField(TEXT("nameFilter"))) NameFilter = Arguments->GetStringField(TEXT("nameFilter"));
		if (Arguments->HasField(TEXT("offset")))     Offset     = FMath::Max(0, static_cast<int32>(Arguments->GetNumberField(TEXT("offset"))));
		if (Arguments->HasField(TEXT("limit")))      Limit      = FMath::Clamp(static_cast<int32>(Arguments->GetNumberField(TEXT("limit"))), 1, 500);

		TArray<FString> PropertyPaths;
		const TArray<TSharedPtr<FJsonValue>>* PPArr = nullptr;
		if (Arguments->TryGetArrayField(TEXT("propertyPaths"), PPArr) && PPArr)
		{
			for (const TSharedPtr<FJsonValue>& V : *PPArr)
			{
				FString S;
				if (V.IsValid() && V->TryGetString(S) && !S.IsEmpty()) { PropertyPaths.Add(S); }
			}
		}

		TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();

		UObject* Obj = FNexusAssetUtils::LoadAssetWithFallback<UObject>(Path);
		if (!Obj) { Entry->SetStringField(TEXT("error"), FString::Printf(TEXT("资产未找到: %s"), *Path)); OutEntries.Add(MakeShared<FJsonValueObject>(Entry)); return; }

		UDataAsset* DA = Cast<UDataAsset>(Obj);
		if (!DA) { Entry->SetStringField(TEXT("error"), FString::Printf(TEXT("资产不是 DataAsset: %s"), *Path)); OutEntries.Add(MakeShared<FJsonValueObject>(Entry)); return; }

		TSharedPtr<FJsonObject> One = HandleDataAsset(DA, NameFilter, PropertyPaths, Offset, Limit);
		for (const auto& Pair : One->Values) { Entry->SetField(Pair.Key, Pair.Value); }

		OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
	
	});
}

REGISTER_MCP_CAPABILITY(FGetAssetDataAssetCapability)

