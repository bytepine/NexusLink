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
	OutEntry->SetStringField(TEXT("widgetName"),  Widget->GetName());
	OutEntry->SetStringField(TEXT("widgetClass"), Widget->GetClass()->GetName());

	if (PropertyPath.IsEmpty())
	{
		TArray<TSharedPtr<FJsonValue>> Children;
		FNexusPropertyUtils::CollectEditableProperties(Widget, Children);
		OutEntry->SetArrayField(TEXT("children"), Children);
		FNexusWidgetLayoutUtils::AppendUmgLayoutFields(Widget, OutEntry);
		return;
	}

	OutEntry->SetStringField(TEXT("propertyPath"), PropertyPath);

	TArray<FString> Segs;
	PropertyPath.ParseIntoArray(Segs, TEXT("."), true);
	if (Segs.Num() == 0)
	{
		OutEntry->SetStringField(TEXT("error"), TEXT("propertyPath is empty"));
		return;
	}
	TArray<FString> Remaining;
	for (int32 i = 1; i < Segs.Num(); ++i) Remaining.Add(Segs[i]);

	FString Error;
	if (!FNexusPropertyUtils::ResolvePropertyRead(Widget, Segs[0], Remaining, OutEntry, Error))
	{
		OutEntry->SetStringField(TEXT("error"), Error);
	}
}

void FGetRuntimeWidgetPropertyCapability::BuildDefinition(FNexusCapabilityDefinition& Out) const
{
	Out.Name = TEXT("get_runtime_widget_property");
	Out.Description = TEXT("Read runtime UMG element fields. Locate via widgetName+ownerClass; includes layout without propertyPaths.");
	Out.InputSchema = FNexusSchema::Object()
		.Prop(TEXT("widgetName"),    FNexusSchema::Str(TEXT("Runtime Widget name")))
		.Prop(TEXT("ownerClass"),    FNexusSchema::Str(TEXT("UserWidget filter")))
		.Prop(TEXT("propertyPaths"), FNexusSchema::StrArr(TEXT("Dot-separated paths (batch)")))
		.Build();
	Out.Tags = {FNexusMcpTags::Readonly, FNexusMcpTags::Runtime };
	Out.ExtraSearchKeywords = { TEXT("umg"), TEXT("field"), TEXT("brush"), TEXT("slot"), TEXT("value") };
	Out.RelatedCapabilities = { TEXT("set_runtime_widget_property"), TEXT("list_runtime_widgets") };
	Out.Prerequisites = { TEXT("pie") };
	Out.WhenToUse = TEXT("Read-only UMG fields; no modifications");
}

FCapabilityResult FGetRuntimeWidgetPropertyCapability::Execute(const TSharedPtr<FJsonObject>& Arguments) const
{

	return FNexusCapabilityResultBuilder::Build([&](auto& OutEntries, auto& OutTop, auto& OutError)
	{
		const FNexusArgs A(Arguments);

		const FString WN = A.Str(TEXT("widgetName"));

		FString OwnerClass;
		Arguments->TryGetStringField(TEXT("ownerClass"), OwnerClass);

		// 仅接受 propertyPaths[]；缺省则展开 children + layout
		TArray<FString> PropertyPaths;
		FNexusPropertyUtils::ReadStringArray(Arguments, TEXT("propertyPaths"), PropertyPaths);

		UWidget* Widget = FNexusRuntimeUtils::FindRuntimeWidget(OwnerClass, WN);
		if (!Widget)
		{
			TSharedPtr<FJsonObject> OutEntry = MakeShared<FJsonObject>();
			OutEntry->SetStringField(TEXT("widgetName"), WN);
			OutEntry->SetStringField(TEXT("error"), FString::Printf(TEXT("Runtime Widget '%s' not found"), *WN));
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

REGISTER_MCP_CAPABILITY(FGetRuntimeWidgetPropertyCapability)
