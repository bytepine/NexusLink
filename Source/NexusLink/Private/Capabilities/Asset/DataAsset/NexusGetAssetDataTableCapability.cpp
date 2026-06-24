// Copyright byteyang. All Rights Reserved.

#include "Capabilities/Asset/DataAsset/NexusGetAssetDataTableCapability.h"
#include "Utils/NexusPropertyUtils.h"
#include "Utils/NexusCapabilityResultBuilder.h"
#include "NexusCapabilityRegistry.h"
#include "NexusMcpTool.h"
#include "NexusMcpSchemaBuilder.h"
#include "Utils/NexusStringMatchUtils.h"
#include "Utils/NexusAssetUtils.h"
#include "Utils/NexusJsonUtils.h"
#include "Engine/DataTable.h"

static TSharedPtr<FJsonObject> HandleDataTable(UDataTable* DT, const FString& NameFilter, const TArray<FString>& PropertyPaths, int32 Offset, int32 Limit)
{
	TSharedPtr<FJsonObject> Info = MakeShared<FJsonObject>();
	if (DT->GetRowStruct())
	{
		Info->SetStringField(TEXT("rowStruct"), DT->GetRowStruct()->GetName());
		TArray<TSharedPtr<FJsonValue>> Cols;
		for (TFieldIterator<FProperty> It(DT->GetRowStruct()); It; ++It)
		{
			const FString ColName = It->GetName();
			if (!FNexusAssetUtils::MatchesPropertyPathsFilter(PropertyPaths, ColName)) continue;
			TSharedPtr<FJsonObject> C = MakeShared<FJsonObject>();
			C->SetStringField(TEXT("name"), ColName);
			C->SetStringField(TEXT("type"), It->GetCPPType());
			Cols.Add(MakeShared<FJsonValueObject>(C));
		}
		Info->SetArrayField(TEXT("columns"), Cols);
	}
	TArray<FName> RowNames = DT->GetRowNames();
	TArray<FString> Filtered;
	for (const FName& RN : RowNames)
	{
		FString S = RN.ToString();
		if (!NameFilter.IsEmpty() && !FNexusStringMatchUtils::Matches(S, NameFilter)) continue;
		Filtered.Add(S);
	}
	const int32 Total = Filtered.Num();
	int32 Start, End;
	FNexusJsonUtils::ComputeSlice(Total, Offset, Limit, Start, End);
	TArray<TSharedPtr<FJsonValue>> Page;
	for (int32 i = Start; i < End; ++i) Page.Add(MakeShared<FJsonValueString>(Filtered[i]));
	Info->SetNumberField(TEXT("totalRowCount"), Total);
	Info->SetNumberField(TEXT("offset"), Start);
	Info->SetNumberField(TEXT("limit"),  Limit);
	Info->SetArrayField(TEXT("rowNames"), Page);
	return Info;
}

void FGetAssetDataTableCapability::BuildDefinition(FNexusCapabilityDefinition& Out) const
{
	Out.Name = TEXT("get_asset_data_table");
	Out.Description = TEXT("检查 DT 行或 Schema。mode=schema|rows；可 propertyPaths 过滤。");
	Out.InputSchema = FNexusSchema::Object()
		.Prop(TEXT("assetPath"),  FNexusSchema::Str(TEXT("DataTable 资产路径")))
		.Prop(TEXT("mode"),       FNexusSchema::Enum(TEXT("auto：rowNames 非空则 rows 否则 schema；schema：忽略 rowNames；rows：要求 rowNames 非空"),
			{ TEXT("auto"), TEXT("schema"), TEXT("rows") }, TEXT("auto")))
		.Prop(TEXT("rowNames"),   FNexusSchema::StrArr(TEXT("行名（rows 模式或 auto 且非空时）")))
		.Prop(TEXT("nameFilter"), FNexusSchema::Str(TEXT("行名过滤（/regex/ ^前缀 后缀$）")))
		.Prop(TEXT("propertyPaths"), FNexusSchema::StrArr(TEXT("列/字段名过滤（首段路径），schema 列表与行字段导出")))
		.Prop(TEXT("offset"),     FNexusSchema::Int(TEXT("分页偏移"), 0, 0))
		.Prop(TEXT("limit"),      FNexusSchema::Int(TEXT("每页最大行数"), 100, 1, 500))
		.Required({ TEXT("assetPath") })
		.Build();
	Out.Tags = {FNexusMcpTags::Readonly, FNexusMcpTags::Data };
	Out.ExtraSearchKeywords = { TEXT("dt"), TEXT("schema"), TEXT("rowstruct"), TEXT("columns"), TEXT("rows") };
	Out.RelatedCapabilities = { TEXT("manage_asset_data_table"), TEXT("create_asset_data_table") };
	Out.WhenToUse = TEXT("读 DT Schema 或行值；不含编辑");
}

FCapabilityResult FGetAssetDataTableCapability::Execute(const TSharedPtr<FJsonObject>& Arguments) const
{

	return FNexusCapabilityResultBuilder::Build([&](auto& OutEntries, auto& OutTop, auto& OutError)
	{

		FString Path;
		if (!Arguments.IsValid() || !Arguments->TryGetStringField(TEXT("assetPath"), Path) || Path.IsEmpty())
		{
			OutError = TEXT("缺少 assetPath");
			return;
		}

		UDataTable* DT = FNexusAssetUtils::LoadAssetWithFallback<UDataTable>(Path);
		if (!DT) { OutError = FString::Printf(TEXT("DataTable 未找到: %s"), *Path); return; }

		TArray<FString> PropertyPaths;
		const TArray<TSharedPtr<FJsonValue>>* PPArr = nullptr;
		if (Arguments->TryGetArrayField(TEXT("propertyPaths"), PPArr) && PPArr)
		{
			for (const TSharedPtr<FJsonValue>& V : *PPArr)
			{
				FString S;
				if (V.IsValid() && V->TryGetString(S) && !S.IsEmpty()) PropertyPaths.Add(S);
			}
		}

		FString Mode = TEXT("auto");
		if (Arguments->HasField(TEXT("mode"))) Mode = Arguments->GetStringField(TEXT("mode")).ToLower();

		const TArray<TSharedPtr<FJsonValue>>* RowNamesArr = nullptr;
		const bool bRowNamesPresent = Arguments->TryGetArrayField(TEXT("rowNames"), RowNamesArr) && RowNamesArr;
		const bool bRowNamesNonEmpty = bRowNamesPresent && RowNamesArr->Num() > 0;

		bool bUseRowMode = false;
		if (Mode == TEXT("rows"))
		{
			if (!bRowNamesNonEmpty) { OutError = TEXT("mode=rows 需要非空 rowNames"); return; }
			bUseRowMode = true;
		}
		else if (Mode == TEXT("schema")) { bUseRowMode = false; }
		else if (Mode == TEXT("auto") || Mode.IsEmpty()) { bUseRowMode = bRowNamesNonEmpty; }
		else { OutError = FString::Printf(TEXT("无效的 mode '%s'（auto|schema|rows）"), *Mode); return; }

		// ── Row data mode：rows / auto 且 rowNames 非空 ───────────────────────────
		if (bUseRowMode)
		{
			const UScriptStruct* RowStruct = DT->GetRowStruct();
			if (!RowStruct) { OutError = TEXT("DataTable 无行结构体"); return; }

			for (const TSharedPtr<FJsonValue>& Val : *RowNamesArr)
			{
				TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
				FString RowName;
				if (Val.IsValid()) Val->TryGetString(RowName);

				Entry->SetStringField(TEXT("rowName"), RowName);

				if (RowName.IsEmpty())
				{
					Entry->SetStringField(TEXT("error"), TEXT("rowName 必填"));
					OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
					continue;
				}

				const uint8* RowData = DT->FindRowUnchecked(FName(*RowName));
				if (!RowData)
				{
					Entry->SetStringField(TEXT("error"), FString::Printf(TEXT("行 '%s' 不存在"), *RowName));
					OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
					continue;
				}

				TArray<TSharedPtr<FJsonValue>> Fields;
				for (TFieldIterator<FProperty> It(RowStruct); It; ++It)
				{
					FProperty* Prop = *It;
					const FString PropName = Prop->GetName();
					if (!FNexusAssetUtils::MatchesPropertyPathsFilter(PropertyPaths, PropName)) continue;

					const void* ValuePtr = Prop->ContainerPtrToValuePtr<void>(RowData);
					FString ValueStr;
					FNexusPropertyUtils::ExportText(Prop, ValueStr, ValuePtr);

					TSharedPtr<FJsonObject> Field = MakeShared<FJsonObject>();
					Field->SetStringField(TEXT("name"), PropName);
					Field->SetStringField(TEXT("type"), Prop->GetCPPType());
					if (!ValueStr.IsEmpty()) Field->SetStringField(TEXT("value"), ValueStr);
					Fields.Add(MakeShared<FJsonValueObject>(Field));
				}
				Entry->SetArrayField(TEXT("fields"), Fields);
				OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
			}
			return;
		}

		// ── Schema + 分页行名模式 ─────────────────────────────────────────────────
		FString NameFilter;
		int32   Offset = 0;
		int32   Limit  = 100;
		if (Arguments->HasField(TEXT("nameFilter"))) NameFilter = Arguments->GetStringField(TEXT("nameFilter"));
		if (Arguments->HasField(TEXT("offset")))     Offset     = FMath::Max(0, static_cast<int32>(Arguments->GetNumberField(TEXT("offset"))));
		if (Arguments->HasField(TEXT("limit")))      Limit      = FMath::Clamp(static_cast<int32>(Arguments->GetNumberField(TEXT("limit"))), 1, 500);

		TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
		TSharedPtr<FJsonObject> One = HandleDataTable(DT, NameFilter, PropertyPaths, Offset, Limit);
		for (const auto& Pair : One->Values) Entry->SetField(Pair.Key, Pair.Value);

		OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
	
	});
}

REGISTER_MCP_CAPABILITY(FGetAssetDataTableCapability)

