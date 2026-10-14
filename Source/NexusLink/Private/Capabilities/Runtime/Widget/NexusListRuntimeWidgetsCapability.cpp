// Copyright byteyang. All Rights Reserved.

#include "Capabilities/Runtime/Widget/NexusListRuntimeWidgetsCapability.h"
#include "Utils/NexusJsonUtils.h"
#include "Utils/NexusArgs.h"
#include "Utils/NexusCapabilityResultBuilder.h"
#include "NexusCapabilityRegistry.h"
#include "NexusMcpSchemaBuilder.h"
#include "Utils/NexusRuntimeUtils.h"
#include "Utils/NexusStringMatchUtils.h"
#include "Utils/NexusResponseCompactorUtils.h"
#include "Blueprint/UserWidget.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Widget.h"
#include "Components/PanelWidget.h"
#include "NexusMcpTool.h"

void FNexusListRuntimeWidgetsCapability::BuildDefinition(FNexusCapabilityDefinition& Out) const
{
	Out.Name = TEXT("ListRuntimeWidgets");
	Out.Description = TEXT("List PIE/Game viewport UMG instances. Filter by class/name/DisplayText.");
	Out.InputSchema = FNexusSchema::Object()
		.Prop(TEXT("ClassFilter"), FNexusSchema::Str(TEXT("UserWidget class name filter")))
		.Prop(TEXT("NameFilter"),  FNexusSchema::Str(TEXT("Child Widget name filter")))
		.Prop(TEXT("TextFilter"),  FNexusSchema::Str(TEXT("Visible display text substring filter")))
		.Prop(TEXT("Offset"),      FNexusSchema::Int(TEXT("Pagination offset (default 0)")))
		.Prop(TEXT("Limit"),       FNexusSchema::Int(TEXT("Max count 1-500 (default 100)")))
		.Build();
	Out.Tags = {FNexusMcpTags::Readonly, FNexusMcpTags::Runtime };
	Out.ExtraSearchKeywords = { TEXT("umg"), TEXT("Viewport"), TEXT("hud"), TEXT("screen"), TEXT("enumerate") };
	Out.RelatedCapabilities = { TEXT("GetRuntimeWidgetProperty"), TEXT("InteractRuntimeWidget") };
	Out.Prerequisites = { TEXT("PIE") };
}

FCapabilityResult FNexusListRuntimeWidgetsCapability::Execute(const TSharedPtr<FJsonObject>& Arguments) const
{
	return FNexusCapabilityResultBuilder::Build([&](auto& OutEntries, auto& OutTop, auto& OutError)
	{
		const FNexusArgs A(Arguments);
		FString ClassFilter, NameFilter, TextFilter;
		int32 Offset = 0, Limit = 100;

		if (Arguments.IsValid())
		{
			if (Arguments->HasField(TEXT("ClassFilter"))) ClassFilter = A.Str(TEXT("ClassFilter"));
			if (Arguments->HasField(TEXT("NameFilter")))  NameFilter  = A.Str(TEXT("NameFilter"));
			if (Arguments->HasField(TEXT("TextFilter")))  TextFilter  = A.Str(TEXT("TextFilter"));
			if (Arguments->HasField(TEXT("Offset")))      Offset = FMath::Max(0, static_cast<int32>(A.Num(TEXT("Offset"))));
			if (Arguments->HasField(TEXT("Limit")))       Limit  = FMath::Clamp(static_cast<int32>(A.Num(TEXT("Limit"))), 1, 500);
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
			O->SetStringField(TEXT("WidgetClass"), All[i].OwnerClass);
			O->SetStringField(TEXT("name"),  All[i].Name);
			O->SetStringField(TEXT("Class"), All[i].Class);
			if (!All[i].Parent.IsEmpty())      O->SetStringField(TEXT("Parent"),      All[i].Parent);
			if (!All[i].DisplayText.IsEmpty()) O->SetStringField(TEXT("DisplayText"), All[i].DisplayText);
			Page.Add(MakeShared<FJsonValueObject>(O));
		}

	TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
	Entry->SetNumberField(TEXT("TotalCount"), Total);
	Entry->SetNumberField(TEXT("Offset"),     Start);
	Entry->SetNumberField(TEXT("Limit"),      Limit);
	Entry->SetArrayField(TEXT("Widgets"),     Page);
		if (!ClassFilter.IsEmpty() && Page.Num() > 0)
		{
			FNexusResponseCompactorUtils WidgetCompactor;
			WidgetCompactor.AddForcedDefaultIfUnanimous(TEXT("WidgetClass"), Page);
			WidgetCompactor.CompactArray(Page);
			WidgetCompactor.Emit(Entry, TEXT("Widgets"));
		}
		OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
	});
}

REGISTER_MCP_CAPABILITY(FNexusListRuntimeWidgetsCapability)

