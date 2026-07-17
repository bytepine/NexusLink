// Copyright byteyang. All Rights Reserved.

#include "NexusMultiSectionCapability.h"
#include "NexusMcpSchemaBuilder.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"

// ─────────────────────────────────────────────────────────────────────────────

TSharedPtr<FJsonObject> FNexusMultiSectionCapability::BuildSchemaWithSections() const
{
	TSharedPtr<FJsonObject> Schema = BuildCapabilitySchema();
	if (!Schema.IsValid())
	{
		Schema = MakeShared<FJsonObject>();
	}

	const TArray<FString> AllSections = GetSectionNames();
	if (AllSections.Num() == 0)
	{
		return Schema;
	}

	// 枚举值：已有 section 名 + "all"
	TArray<FString> EnumValues = AllSections;
	EnumValues.AddUnique(TEXT("all"));

	// description：说明默认列表
	const TArray<FString> DefaultSections = GetDefaultSectionNames();
	FString DefaultHint = DefaultSections.Num() > 0
		? FString::Printf(TEXT("default: %s"), *FString::Join(DefaultSections, TEXT("+")))
		: TEXT("default: all");
	FString Desc = FString::Printf(TEXT("Sections to fetch (%s)"), *DefaultHint);

	TSharedRef<FJsonObject> SectionsSchema = FNexusSchema::EnumArr(*Desc, EnumValues);

	// 注入到 properties
	const TSharedPtr<FJsonObject>* ConstPtr = nullptr;
	if (Schema->TryGetObjectField(TEXT("properties"), ConstPtr) && ConstPtr)
	{
		(*ConstPtr)->SetObjectField(TEXT("sections"), SectionsSchema);
	}
	else
	{
		TSharedPtr<FJsonObject> Props = MakeShared<FJsonObject>();
		Props->SetObjectField(TEXT("sections"), SectionsSchema);
		Schema->SetObjectField(TEXT("properties"), Props);
	}

	return Schema;
}

FCapabilityResult FNexusMultiSectionCapability::Execute(const TSharedPtr<FJsonObject>& Arguments) const
{
	return RunMultiSection(Arguments);
}

FCapabilityResult FNexusMultiSectionCapability::RunMultiSection(const TSharedPtr<FJsonObject>& Args) const
{
	FCapabilityResult Result;

	const TArray<FString> AllSections = GetSectionNames();

	// 解析请求的 section 列表
	auto ResolveSections = [&](const TSharedPtr<FJsonObject>& EntryArgs) -> TArray<FString>
	{
		TArray<FString> Requested;
		const TArray<TSharedPtr<FJsonValue>>* SectionsArr = nullptr;
		if (EntryArgs->TryGetArrayField(TEXT("sections"), SectionsArr) && SectionsArr && SectionsArr->Num() > 0)
		{
			for (const TSharedPtr<FJsonValue>& V : *SectionsArr)
			{
				FString S;
				if (V.IsValid() && V->TryGetString(S) && !S.IsEmpty())
				{
					Requested.AddUnique(S.ToLower());
				}
			}
		}

		if (Requested.Num() == 0)
		{
			return GetDefaultSectionNames();
		}
		if (Requested.Contains(TEXT("all")))
		{
			return AllSections;
		}
		return Requested;
	};

	// 多 entry 展开
	TArray<TSharedPtr<FJsonObject>> PerEntryArgsList = ExpandPerEntry(Args);
	if (PerEntryArgsList.Num() == 0)
	{
		PerEntryArgsList.Add(Args);
	}

	for (const TSharedPtr<FJsonObject>& EntryArgs : PerEntryArgsList)
	{
		TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();

		// PrepareEntry：写 locator / 获取 Target
		void* TargetOpaque = nullptr;
		FString PrepareError;
		if (!PrepareEntry(EntryArgs, Entry, TargetOpaque, PrepareError))
		{
			if (!PrepareError.IsEmpty())
			{
				Entry->SetStringField(TEXT("error"), PrepareError);
			}
			Result.Entries.Add(MakeShared<FJsonValueObject>(Entry));
			continue;
		}

		// 解析 section 列表
		const TArray<FString> SectionList = ResolveSections(EntryArgs);

		TArray<TSharedPtr<FJsonValue>> SectionErrors;
		TArray<TSharedPtr<FJsonValue>> CoveredArr;

		for (const FString& SectionName : SectionList)
		{
			// 校验 section 是否合法
			if (!AllSections.Contains(SectionName))
			{
				TSharedPtr<FJsonObject> ErrObj = MakeShared<FJsonObject>();
				ErrObj->SetStringField(TEXT("section"), SectionName);
				ErrObj->SetStringField(TEXT("error"), FString::Printf(
					TEXT("Unknown section '%s' (supported: %s)"), *SectionName, *FString::Join(AllSections, TEXT(", "))));
				SectionErrors.Add(MakeShared<FJsonValueObject>(ErrObj));
				continue;
			}

			FString SecError;
			ExecuteSection(SectionName, EntryArgs, TargetOpaque, Entry, SecError);

			if (!SecError.IsEmpty())
			{
				TSharedPtr<FJsonObject> ErrObj = MakeShared<FJsonObject>();
				ErrObj->SetStringField(TEXT("section"), SectionName);
				ErrObj->SetStringField(TEXT("error"), SecError);
				SectionErrors.Add(MakeShared<FJsonValueObject>(ErrObj));
			}
			else
			{
				CoveredArr.Add(MakeShared<FJsonValueString>(SectionName));
			}
		}

		// 写覆盖列表：仅部分失败 / 有 sectionErrors 时回显（全成功则省略，省 token）
		if (SectionErrors.Num() > 0 || CoveredArr.Num() != SectionList.Num())
		{
			Entry->SetArrayField(TEXT("sections"), CoveredArr);
		}
		if (SectionErrors.Num() > 0)
		{
			Entry->SetArrayField(TEXT("sectionErrors"), SectionErrors);
		}

		Result.Entries.Add(MakeShared<FJsonValueObject>(Entry));
	}

	return Result;
}
