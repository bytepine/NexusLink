// Copyright byteyang. All Rights Reserved.

#include "Capabilities/Runtime/Widget/NexusSetRuntimeWidgetPropertyCapability.h"
#include "Utils/NexusCapabilityResultBuilder.h"
#include "NexusCapabilityRegistry.h"
#include "NexusMcpSchemaBuilder.h"
#include "Utils/NexusRuntimeUtils.h"
#include "Utils/NexusPropertyUtils.h"
#include "Components/Widget.h"
#include "NexusMcpTool.h"

static void WriteRuntimeWidgetPropertyImpl(
	const FString& WidgetName,
	const FString& OwnerClass,
	const FString& PropertyPath,
	const FString& NewValue,
	TSharedPtr<FJsonObject>& OutEntry)
{
	OutEntry->SetStringField(TEXT("widgetName"),   WidgetName);
	OutEntry->SetStringField(TEXT("propertyPath"), PropertyPath);
	if (!OwnerClass.IsEmpty()) { OutEntry->SetStringField(TEXT("ownerClass"), OwnerClass); }

	if (WidgetName.IsEmpty())   { OutEntry->SetStringField(TEXT("error"), TEXT("Missing widgetName")); return; }
	if (PropertyPath.IsEmpty()) { OutEntry->SetStringField(TEXT("error"), TEXT("Missing propertyPath")); return; }

	UWidget* Widget = FNexusRuntimeUtils::FindRuntimeWidget(OwnerClass, WidgetName);
	if (!Widget) { OutEntry->SetStringField(TEXT("error"), FString::Printf(TEXT("Runtime Widget '%s' not found"), *WidgetName)); return; }

	TArray<FString> Segs;
	PropertyPath.ParseIntoArray(Segs, TEXT("."), true);
	if (Segs.Num() == 0) { OutEntry->SetStringField(TEXT("error"), TEXT("propertyPath is empty")); return; }

	FString OldVal, ActualVal, Error;
	if (!FNexusPropertyUtils::WritePropertyAndEcho(Widget, Segs, 0, NewValue, OldVal, ActualVal, Error))
	{
		OutEntry->SetStringField(TEXT("error"), Error);
		return;
	}
	OutEntry->SetStringField(TEXT("resolvedWidget"), Widget->GetName());
	if (!OldVal.IsEmpty())    OutEntry->SetStringField(TEXT("oldValue"),    OldVal);
	if (!ActualVal.IsEmpty()) OutEntry->SetStringField(TEXT("newValue"), ActualVal);
}

void FSetRuntimeWidgetPropertyCapability::BuildDefinition(FNexusCapabilityDefinition& Out) const
{
	Out.Name = TEXT("set_runtime_widget_property");
	Out.Description = TEXT("Batch modify runtime UMG fields. updates[] has widget name/path/value.");
	Out.InputSchema = [this]() -> TSharedPtr<FJsonObject>
	{
		TSharedPtr<FJsonObject> ItemSchema = FNexusSchema::Object()
			.Prop(TEXT("propertyPath"), FNexusSchema::Str(TEXT("Dot-separated path")))
			.Prop(TEXT("value"),        FNexusSchema::Str(TEXT("New value string")))
			.Prop(TEXT("widgetName"),   FNexusSchema::Str(TEXT("Widget name")))
			.Prop(TEXT("ownerClass"),   FNexusSchema::Str(TEXT("UserWidget filter")))
			.Required({ TEXT("propertyPath"), TEXT("value") })
			.Build();

		return FNexusSchema::Object()
			.Prop(TEXT("updates"), FNexusSchema::ArrayOf(TEXT("Batch update"), ItemSchema.ToSharedRef()))
			.Required({ TEXT("updates") })
			.Build();
	}();
	Out.Tags = {FNexusMcpTags::Write, FNexusMcpTags::Runtime };
	Out.ExtraSearchKeywords = { TEXT("umg"), TEXT("field"), TEXT("brush"), TEXT("value"), TEXT("mutate") };
	Out.RelatedCapabilities = { TEXT("get_runtime_widget_property") };
	Out.Prerequisites = { TEXT("pie") };
	Out.WhenToUse = TEXT("Modify runtime UMG live fields");
}

FCapabilityResult FSetRuntimeWidgetPropertyCapability::Execute(const TSharedPtr<FJsonObject>& Arguments) const
{

	return FNexusCapabilityResultBuilder::Build([&](auto& OutEntries, auto& OutTop, auto& OutError)
	{

		const TArray<TSharedPtr<FJsonValue>>* UpdatesArr = nullptr;
		if (!Arguments->TryGetArrayField(TEXT("updates"), UpdatesArr) || !UpdatesArr)
		{
			OutError = TEXT("Missing updates");
			return;
		}

		for (const TSharedPtr<FJsonValue>& Val : *UpdatesArr)
		{
			TSharedPtr<FJsonObject> Item = Val->AsObject();
			TSharedPtr<FJsonObject> OutEntry = MakeShared<FJsonObject>();

			if (!Item.IsValid())
			{
				OutEntry->SetStringField(TEXT("error"), TEXT("Invalid update item"));
				OutEntries.Add(MakeShared<FJsonValueObject>(OutEntry));
				continue;
			}

			FString PropertyPath, NewValue, WidgetName, OwnerClass;
			Item->TryGetStringField(TEXT("propertyPath"), PropertyPath);
			Item->TryGetStringField(TEXT("value"),        NewValue);
			Item->TryGetStringField(TEXT("widgetName"),   WidgetName);
			Item->TryGetStringField(TEXT("ownerClass"),   OwnerClass);

			if (PropertyPath.IsEmpty())
			{
				OutEntry->SetStringField(TEXT("error"), TEXT("Each update requires propertyPath"));
				OutEntries.Add(MakeShared<FJsonValueObject>(OutEntry));
				continue;
			}

			WriteRuntimeWidgetPropertyImpl(WidgetName, OwnerClass, PropertyPath, NewValue, OutEntry);
			OutEntries.Add(MakeShared<FJsonValueObject>(OutEntry));
		}
	
	});
}

REGISTER_MCP_CAPABILITY(FSetRuntimeWidgetPropertyCapability)
