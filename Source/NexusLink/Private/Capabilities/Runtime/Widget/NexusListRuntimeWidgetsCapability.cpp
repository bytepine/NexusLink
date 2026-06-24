// Copyright byteyang. All Rights Reserved.

#include "Capabilities/Runtime/Widget/NexusListRuntimeWidgetsCapability.h"
#include "Utils/NexusJsonUtils.h"
#include "Utils/NexusCapabilityResultBuilder.h"
#include "NexusCapabilityRegistry.h"
#include "NexusMcpSchemaBuilder.h"
#include "Utils/NexusRuntimeUtils.h"
#include "Utils/NexusStringMatchUtils.h"
#include "Blueprint/UserWidget.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Widget.h"
#include "Components/PanelWidget.h"
#include "NexusMcpTool.h"

void FListRuntimeWidgetsCapability::BuildDefinition(FNexusCapabilityDefinition& Out) const
{
	Out.Name = TEXT("list_runtime_widgets");
	Out.Description = TEXT("枚举 PIE/Game 视口 UMG 实例。按类/名/displayText 过滤。");
	Out.InputSchema = FNexusSchema::Object()
		.Prop(TEXT("classFilter"), FNexusSchema::Str(TEXT("UserWidget 类名过滤")))
		.Prop(TEXT("nameFilter"),  FNexusSchema::Str(TEXT("子 Widget 名过滤")))
		.Prop(TEXT("textFilter"),  FNexusSchema::Str(TEXT("可见显示文本子串过滤")))
		.Prop(TEXT("offset"),      FNexusSchema::Int(TEXT("分页偏移（默认 0）")))
		.Prop(TEXT("limit"),       FNexusSchema::Int(TEXT("最大条数 1~500（默认 100）")))
		.Build();
	Out.Tags = {FNexusMcpTags::Readonly, FNexusMcpTags::Runtime };
	Out.ExtraSearchKeywords = { TEXT("umg"), TEXT("viewport"), TEXT("hud"), TEXT("screen"), TEXT("enumerate") };
	Out.RelatedCapabilities = { TEXT("get_runtime_widget_property"), TEXT("interact_runtime_widget") };
	Out.Prerequisites = { TEXT("pie") };
}

FCapabilityResult FListRuntimeWidgetsCapability::Execute(const TSharedPtr<FJsonObject>& Arguments) const
{
	return FNexusCapabilityResultBuilder::Build([&](auto& OutEntries, auto& OutTop, auto& OutError)
	{
		FString ClassFilter, NameFilter, TextFilter;
		int32 Offset = 0, Limit = 100;

		if (Arguments.IsValid())
		{
			if (Arguments->HasField(TEXT("classFilter"))) ClassFilter = Arguments->GetStringField(TEXT("classFilter"));
			if (Arguments->HasField(TEXT("nameFilter")))  NameFilter  = Arguments->GetStringField(TEXT("nameFilter"));
			if (Arguments->HasField(TEXT("textFilter")))  TextFilter  = Arguments->GetStringField(TEXT("textFilter"));
			if (Arguments->HasField(TEXT("offset")))      Offset = FMath::Max(0, static_cast<int32>(Arguments->GetNumberField(TEXT("offset"))));
			if (Arguments->HasField(TEXT("limit")))       Limit  = FMath::Clamp(static_cast<int32>(Arguments->GetNumberField(TEXT("limit"))), 1, 500);
		}

		struct FEntry { FString OwnerClass; FString Name; FString Class; FString Parent; FString DisplayText; };
		TArray<FEntry> All;

		for (UUserWidget* UW : FNexusRuntimeUtils::GetActiveUserWidgets())
		{
			const FString OwnerClass = UW->GetClass()->GetName();
			if (!ClassFilter.IsEmpty() && !FNexusStringMatchUtils::Matches(OwnerClass, ClassFilter)) continue;

			FNexusRuntimeUtils::ForEachWidgetRecursive(UW, [&](UWidget* Child)
			{
				if (!Child) return;
				FEntry E;
				E.OwnerClass  = OwnerClass;
				E.Name        = Child->GetName();
				E.Class       = Child->GetClass()->GetName();
				E.DisplayText = FNexusRuntimeUtils::GetWidgetDisplayText(Child);
				if (UWidget* P = Child->GetParent()) E.Parent = P->GetName();
				All.Add(E);
			});
		}

		All = All.FilterByPredicate([&](const FEntry& E)
		{
			if (!NameFilter.IsEmpty() && !FNexusStringMatchUtils::Matches(E.Name, NameFilter)) return false;
			if (!TextFilter.IsEmpty() && !FNexusStringMatchUtils::Matches(E.DisplayText, TextFilter)) return false;
			return true;
		});

		const int32 Total = All.Num();
		int32 Start, End; FNexusJsonUtils::ComputeSlice(Total, Offset, Limit, Start, End);

		TArray<TSharedPtr<FJsonValue>> Page;
		for (int32 i = Start; i < End; ++i)
		{
			TSharedPtr<FJsonObject> O = MakeShared<FJsonObject>();
			O->SetStringField(TEXT("widgetClass"), All[i].OwnerClass);
			O->SetStringField(TEXT("name"),  All[i].Name);
			O->SetStringField(TEXT("class"), All[i].Class);
			if (!All[i].Parent.IsEmpty())      O->SetStringField(TEXT("parent"),      All[i].Parent);
			if (!All[i].DisplayText.IsEmpty()) O->SetStringField(TEXT("displayText"), All[i].DisplayText);
			Page.Add(MakeShared<FJsonValueObject>(O));
		}

	TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
	Entry->SetNumberField(TEXT("totalCount"), Total);
	Entry->SetNumberField(TEXT("offset"),     Start);
	Entry->SetNumberField(TEXT("limit"),      Limit);
	Entry->SetArrayField(TEXT("widgets"),     Page);
		OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
	});
}

REGISTER_MCP_CAPABILITY(FListRuntimeWidgetsCapability)

