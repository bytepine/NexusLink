// Copyright byteyang. All Rights Reserved.

#include "Capabilities/Runtime/Widget/NexusGetRuntimeWidgetPropertyCapability.h"
#include "Utils/NexusWidgetLayoutUtils.h"
#include "Utils/NexusCapabilityResultBuilder.h"
#include "Utils/NexusArgs.h"
#include "NexusCapabilityRegistry.h"
#include "NexusMcpSchemaBuilder.h"
#include "Utils/NexusRuntimeUtils.h"
#include "Utils/NexusPropertyUtils.h"
#include "Components/Widget.h"
#include "NexusMcpTool.h"

// ??????? runtime widget ?????��????Path ?????? children??
static void ReadRuntimeWidgetPropertyImpl(UWidget* Widget, const FString& PropertyPath, TSharedPtr<FJsonObject>& OutEntry)
{
	OutEntry->SetStringField(TEXT("WidgetName"),  Widget->GetName());
	OutEntry->SetStringField(TEXT("WidgetClass"), Widget->GetClass()->GetName());

	if (PropertyPath.IsEmpty())
	{
		TArray<TSharedPtr<FJsonValue>> Children;
		FNexusPropertyUtils::CollectEditableProperties(Widget, Children);
		OutEntry->SetArrayField(TEXT("Children"), Children);
		FNexusWidgetLayoutUtils::AppendUmgLayoutFields(Widget, OutEntry);
		return;
	}

	OutEntry->SetStringField(TEXT("PropertyPath"), PropertyPath);

	TArray<FString> Segs;
	PropertyPath.ParseIntoArray(Segs, TEXT("."), true);
	if (Segs.Num() == 0)
	{
		OutEntry->SetStringField(TEXT("Error"), TEXT("PropertyPath is empty"));
		return;
	}
	TArray<FString> Remaining;
	for (int32 SegmentIndex = 1; SegmentIndex < Segs.Num(); ++SegmentIndex) Remaining.Add(Segs[SegmentIndex]);

	FString Error;
	if (!FNexusPropertyUtils::ResolvePropertyRead(Widget, Segs[0], Remaining, OutEntry, Error))
	{
		OutEntry->SetStringField(TEXT("Error"), Error);
	}
}

void FNexusGetRuntimeWidgetPropertyCapability::BuildDefinition(FNexusCapabilityDefinition& Out) const
{
	Out.Name = TEXT("GetRuntimeWidgetProperty");
	Out.Description = TEXT("Read runtime UMG element fields. Locate via WidgetName+OwnerClass; includes layout without PropertyPaths.");
	Out.InputSchema = FNexusSchema::Object()
		.Prop(TEXT("WidgetName"),    FNexusSchema::Str(TEXT("Runtime Widget name")))
		.Prop(TEXT("OwnerClass"),    FNexusSchema::Str(TEXT("UserWidget filter")))
		.Prop(TEXT("PropertyPaths"), FNexusSchema::StrArr(TEXT("Dot-separated paths (batch)")))
		.Build();
	Out.Tags = {FNexusMcpTags::Readonly, FNexusMcpTags::Runtime };
	Out.ExtraSearchKeywords = { TEXT("umg"), TEXT("field"), TEXT("brush"), TEXT("slot"), TEXT("Value") };
	Out.RelatedCapabilities = { TEXT("SetRuntimeWidgetProperty"), TEXT("ListRuntimeWidgets") };
	Out.Prerequisites = { TEXT("PIE") };
	Out.WhenToUse = TEXT("Read-only UMG fields; no modifications");
}

FCapabilityResult FNexusGetRuntimeWidgetPropertyCapability::Execute(const TSharedPtr<FJsonObject>& Arguments) const
{

	return FNexusCapabilityResultBuilder::Build([&](auto& OutEntries, auto& OutTop, auto& OutError)
	{
		const FNexusArgs A(Arguments);

		const FString WN = A.Str(TEXT("WidgetName"));

		FString OwnerClass;
		OwnerClass = FNexusArgs(Arguments).Str(TEXT("OwnerClass"), OwnerClass);

		// 仅接受 PropertyPaths[]；缺省则展开 children + layout
		TArray<FString> PropertyPaths;
		FNexusPropertyUtils::ReadStringArray(Arguments, TEXT("PropertyPaths"), PropertyPaths);

		UWidget* Widget = FNexusRuntimeUtils::FindRuntimeWidget(OwnerClass, WN);
		if (!Widget)
		{
			TSharedPtr<FJsonObject> OutEntry = MakeShared<FJsonObject>();
			OutEntry->SetStringField(TEXT("WidgetName"), WN);
			OutEntry->SetStringField(TEXT("Error"), FString::Printf(TEXT("Runtime Widget '%s' not found"), *WN));
			OutEntries.Add(MakeShared<FJsonValueObject>(OutEntry));
			return;
		}

		if (PropertyPaths.Num() == 0)
		{
			TSharedPtr<FJsonObject> OutEntry = MakeShared<FJsonObject>();
			ReadRuntimeWidgetPropertyImpl(Widget, FString(), OutEntry);
			OutEntries.Add(MakeShared<FJsonValueObject>(OutEntry));
		}
		else
		{
			for (const FString& Path : PropertyPaths)
			{
				TSharedPtr<FJsonObject> OutEntry = MakeShared<FJsonObject>();
				ReadRuntimeWidgetPropertyImpl(Widget, Path, OutEntry);
				OutEntries.Add(MakeShared<FJsonValueObject>(OutEntry));
			}
		}
	
	});
}

REGISTER_MCP_CAPABILITY(FNexusGetRuntimeWidgetPropertyCapability)
