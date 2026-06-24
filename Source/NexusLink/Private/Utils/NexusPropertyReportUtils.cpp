// Copyright byteyang. All Rights Reserved.

#include "Utils/NexusPropertyReportUtils.h"
#include "Utils/NexusStringMatchUtils.h"
#include "Utils/NexusAssetUtils.h"
#include "Utils/NexusPropertyUtils.h"
#include "Utils/NexusJsonUtils.h"
#include "UObject/UnrealType.h"

TArray<TSharedPtr<FJsonValue>> FNexusPropertyReportUtils::BuildEditablePropsPage(
	UClass*                      Class,
	void*                        Instance,
	UClass*                      LeafClass,
	const FString&               NameFilter,
	const TArray<FString>&       PropertyPaths,
	int32                        Offset,
	int32                        Limit,
	int32&                       OutTotal)
{
	// 先收集满足过滤条件的全部属性
	TArray<TSharedPtr<FJsonObject>> All;
	for (TFieldIterator<FProperty> It(Class); It; ++It)
	{
		FProperty* Prop = *It;
		if (!Prop->HasAnyPropertyFlags(CPF_Edit)) continue;
		const FString PN = Prop->GetName();
		if (!NameFilter.IsEmpty() && !FNexusStringMatchUtils::Matches(PN, NameFilter)) continue;
		if (!FNexusAssetUtils::MatchesPropertyPathsFilter(PropertyPaths, PN)) continue;

		TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
		Entry->SetStringField(TEXT("name"), PN);
		Entry->SetStringField(TEXT("type"), Prop->GetCPPType());

		if (Instance)
		{
			FString Val;
			FNexusPropertyUtils::ExportText(Prop, Val, Prop->ContainerPtrToValuePtr<void>(Instance));
			if (!Val.IsEmpty()) Entry->SetStringField(TEXT("value"), Val);
		}

		// 继承标记：属性的 OwnerClass 与叶类不同时标记
		if (LeafClass)
		{
			if (UClass* OwnerCls = Prop->GetOwnerClass())
				if (OwnerCls != LeafClass) Entry->SetBoolField(TEXT("inherited"), true);
		}

		All.Add(Entry);
	}

	OutTotal = All.Num();

	// 分页切片
	int32 Start, End;
	FNexusJsonUtils::ComputeSlice(OutTotal, Offset, Limit, Start, End);

	TArray<TSharedPtr<FJsonValue>> Page;
	for (int32 i = Start; i < End; ++i)
		Page.Add(MakeShared<FJsonValueObject>(All[i]));
	return Page;
}
