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
#include "Utils/NexusArgs.h"
#include "Engine/DataTable.h"

static TSharedPtr<FJsonObject> HandleDataTable(UDataTable* DT, const FString& NameFilter, const TArray<FString>& PropertyPaths, int32 Offset, int32 Limit)
{
	TSharedPtr<FJsonObject> Info = MakeShared<FJsonObject>();
	if (DT->GetRowStruct())
	{
		Info->SetStringField(TEXT("RowStruct"), DT->GetRowStruct()->GetName());
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
		Info->SetArrayField(TEXT("Columns"), Cols);
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
	for (int32 RowIndex = Start; RowIndex < End; ++RowIndex) Page.Add(MakeShared<FJsonValueString>(Filtered[RowIndex]));
	Info->SetNumberField(TEXT("TotalRowCount"), Total);
	Info->SetNumberField(TEXT("Offset"), Start);
	Info->SetNumberField(TEXT("Limit"),  Limit);
	Info->SetArrayField(TEXT("RowNames"), Page);
	return Info;
}

void FNexusGetAssetDataTableCapability::BuildDefinition(FNexusCapabilityDefinition& Out) const
{
	Out.Name = TEXT("GetAssetDataTable");
	Out.SearchAssetTypes = {TEXT("DataTable")};
	Out.Description = TEXT("Inspect DataTable rows or schema. Mode=schema|rows; optional PropertyPaths.");
	Out.InputSchema = FNexusSchema::Object()
		.Prop(TEXT("AssetPath"),  FNexusSchema::Str(TEXT("DataTable asset path")))
		.Prop(TEXT("Mode"),       FNexusSchema::Enum(TEXT("auto: rows if RowNames non-empty else schema; schema ignores RowNames/NameFilter for row export; rows requires RowNames or NameFilter"),
			{ TEXT("Auto"), TEXT("Schema"), TEXT("Rows") }, TEXT("Auto")))
		.Prop(TEXT("RowNames"),   FNexusSchema::StrArr(TEXT("Row name (rows mode or auto with non-empty RowNames); paginated by offset/limit")))
		.Prop(TEXT("NameFilter"), FNexusSchema::Str(TEXT("Row name filter (/regex/ ^prefix suffix$); in Mode=rows without RowNames, resolves+exports matching rows (paginated)")))
		.Prop(TEXT("PropertyPaths"), FNexusSchema::StrArr(TEXT("Column/field filter (first segment); schema list and row export")))
		.Prop(TEXT("Offset"),     FNexusSchema::Int(TEXT("Pagination offset"), 0, 0))
		.Prop(TEXT("Limit"),      FNexusSchema::Int(TEXT("Max rows per page"), 100, 1, 500))
		.Required({ TEXT("AssetPath") })
		.Build();
	Out.Tags = {FNexusMcpTags::Readonly, FNexusMcpTags::Data };
	Out.ExtraSearchKeywords = { TEXT("dt"), TEXT("Schema"), TEXT("rowstruct"), TEXT("Columns"), TEXT("Rows") };
	Out.RelatedCapabilities = { TEXT("ManageAssetDataTable"), TEXT("CreateAssetDataTable") };
	Out.WhenToUse = TEXT("Read DT schema or row values; no edits");
}

FCapabilityResult FNexusGetAssetDataTableCapability::Execute(const TSharedPtr<FJsonObject>& Arguments) const
{

	return FNexusCapabilityResultBuilder::Build([&](auto& OutEntries, auto& OutTop, auto& OutError)
	{
		const FNexusArgs A(Arguments);

		const FString Path = A.Str(TEXT("AssetPath"));

		UDataTable* DT = FNexusAssetUtils::LoadAssetWithFallback<UDataTable>(Path);
		if (!DT) { OutError = FString::Printf(TEXT("DataTable not found: %s"), *Path); return; }

		TArray<FString> PropertyPaths;
		const TArray<TSharedPtr<FJsonValue>>* PPArr = nullptr;
		if (Arguments->TryGetArrayField(TEXT("PropertyPaths"), PPArr) && PPArr)
		{
			for (const TSharedPtr<FJsonValue>& V : *PPArr)
			{
				FString S;
				if (V.IsValid() && V->TryGetString(S) && !S.IsEmpty()) PropertyPaths.Add(S);
			}
		}

		FString Mode = TEXT("Auto");
		if (Arguments->HasField(TEXT("Mode"))) Mode = A.Str(TEXT("Mode")).ToLower();

		TArray<FString> RowNames;
		const TArray<TSharedPtr<FJsonValue>>* RowNamesArr = nullptr;
		if (Arguments->TryGetArrayField(TEXT("RowNames"), RowNamesArr) && RowNamesArr)
		{
			for (const TSharedPtr<FJsonValue>& V : *RowNamesArr)
			{
				FString S;
				if (V.IsValid() && V->TryGetString(S) && !S.IsEmpty()) RowNames.Add(S);
			}
		}
		const bool bRowNamesNonEmpty = RowNames.Num() > 0;

		FString NameFilter;
		if (Arguments->HasField(TEXT("NameFilter"))) NameFilter = A.Str(TEXT("NameFilter"));
		const bool bNameFilterNonEmpty = !NameFilter.IsEmpty();

		int32 Offset = 0;
		int32 Limit  = 100;
		if (Arguments->HasField(TEXT("Offset"))) Offset = FMath::Max(0, static_cast<int32>(A.Num(TEXT("Offset"))));
		if (Arguments->HasField(TEXT("Limit")))  Limit  = FMath::Clamp(static_cast<int32>(A.Num(TEXT("Limit"))), 1, 500);

		bool bUseRowMode = false;
		if (Mode == TEXT("Rows"))
		{
			// RowNames 或 NameFilter 任一非空即可；NameFilter 单独出现时按过滤条件解出行名
			if (!bRowNamesNonEmpty && !bNameFilterNonEmpty)
			{
				OutError = TEXT("Mode=rows requires RowNames or NameFilter");
				return;
			}
			bUseRowMode = true;
		}
		else if (Mode == TEXT("Schema")) { bUseRowMode = false; }
		else if (Mode == TEXT("Auto") || Mode.IsEmpty())
		{
			// auto 语义不变：仅显式 RowNames 非空才导出整行值；单独 NameFilter 仍走下方 schema 分页行名（更省 token）
			bUseRowMode = bRowNamesNonEmpty;
		}
		else { OutError = FString::Printf(TEXT("Invalid mode '%s' (auto|schema|rows)"), *Mode); return; }

		// ── Row data mode：rows / auto 且 RowNames 非空 ───────────────────────────
		if (bUseRowMode)
		{
			const UScriptStruct* RowStruct = DT->GetRowStruct();
			if (!RowStruct) { OutError = TEXT("DataTable has no row struct"); return; }

			TArray<FString> TargetRowNames = RowNames;
			if (TargetRowNames.Num() == 0 && bNameFilterNonEmpty)
			{
				for (const FName& RN : DT->GetRowNames())
				{
					FString S = RN.ToString();
					if (FNexusStringMatchUtils::Matches(S, NameFilter)) TargetRowNames.Add(S);
				}
			}

			// 两条路径统一分页（默认 100 / 上限 500），避免超大 RowNames/NameFilter 命中一次性导出全表值
			const int32 TotalTargets = TargetRowNames.Num();
			int32 Start, End;
			FNexusJsonUtils::ComputeSlice(TotalTargets, Offset, Limit, Start, End);
			OutTop->SetNumberField(TEXT("TotalRowCount"), TotalTargets);
			OutTop->SetNumberField(TEXT("Offset"), Start);
			OutTop->SetNumberField(TEXT("Limit"),  Limit);

			for (int32 i = Start; i < End; ++i)
			{
				const FString& RowName = TargetRowNames[i];
				TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
				Entry->SetStringField(TEXT("RowName"), RowName);

				const uint8* RowData = DT->FindRowUnchecked(FName(*RowName));
				if (!RowData)
				{
					Entry->SetStringField(TEXT("Error"), FString::Printf(TEXT("Row '%s' does not exist"), *RowName));
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
					if (!ValueStr.IsEmpty()) Field->SetStringField(TEXT("Value"), ValueStr);
					Fields.Add(MakeShared<FJsonValueObject>(Field));
				}
				Entry->SetArrayField(TEXT("Fields"), Fields);
				OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
			}
			return;
		}

		// ── Schema + 分页行名模式 ─────────────────────────────────────────────────
		TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
		TSharedPtr<FJsonObject> One = HandleDataTable(DT, NameFilter, PropertyPaths, Offset, Limit);
		for (const auto& Pair : One->Values) Entry->SetField(Pair.Key, Pair.Value);

		OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
	
	});
}

REGISTER_MCP_CAPABILITY(FNexusGetAssetDataTableCapability)

