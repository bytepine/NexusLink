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
	OutEntry->SetStringField(TEXT("WidgetName"),   WidgetName);
	OutEntry->SetStringField(TEXT("PropertyPath"), PropertyPath);
	if (!OwnerClass.IsEmpty()) { OutEntry->SetStringField(TEXT("OwnerClass"), OwnerClass); }

	if (WidgetName.IsEmpty())   { OutEntry->SetStringField(TEXT("Error"), TEXT("Missing WidgetName")); return; }
	if (PropertyPath.IsEmpty()) { OutEntry->SetStringField(TEXT("Error"), TEXT("Missing PropertyPath")); return; }

	UWidget* Widget = FNexusRuntimeUtils::FindRuntimeWidget(OwnerClass, WidgetName);
	if (!Widget) { OutEntry->SetStringField(TEXT("Error"), FString::Printf(TEXT("Runtime Widget '%s' not found"), *WidgetName)); return; }

	TArray<FString> Segs;
	PropertyPath.ParseIntoArray(Segs, TEXT("."), true);
	if (Segs.Num() == 0) { OutEntry->SetStringField(TEXT("Error"), TEXT("PropertyPath is empty")); return; }

	FString OldVal, ActualVal, Error;
	if (!FNexusPropertyUtils::WritePropertyAndEcho(Widget, Segs, 0, NewValue, OldVal, ActualVal, Error))
	{
		OutEntry->SetStringField(TEXT("Error"), Error);
		return;
	}
	OutEntry->SetStringField(TEXT("ResolvedWidget"), Widget->GetName());
	if (!OldVal.IsEmpty())    OutEntry->SetStringField(TEXT("OldValue"),    OldVal);
	if (!ActualVal.IsEmpty()) OutEntry->SetStringField(TEXT("NewValue"), ActualVal);
}

void FNexusSetRuntimeWidgetPropertyCapability::BuildDefinition(FNexusCapabilityDefinition& Out) const
{
	Out.Name = TEXT("SetRuntimeWidgetProperty");
	Out.Description = TEXT("Batch modify runtime UMG fields. updates[] has widget name/path/value.");
	Out.InputSchema = [this]() -> TSharedPtr<FJsonObject>
	{
		TSharedPtr<FJsonObject> ItemSchema = FNexusSchema::Object()
			.Prop(TEXT("PropertyPath"), FNexusSchema::Str(TEXT("Dot-separated path")))
			.Prop(TEXT("Value"),        FNexusSchema::Str(TEXT("New value string")))
			.Prop(TEXT("WidgetName"),   FNexusSchema::Str(TEXT("Widget name")))
			.Prop(TEXT("OwnerClass"),   FNexusSchema::Str(TEXT("UserWidget filter")))
			.Required({ TEXT("PropertyPath"), TEXT("Value") })
			.Build();

		return FNexusSchema::Object()
			.Prop(TEXT("Updates"), FNexusSchema::ArrayOf(TEXT("Batch update"), ItemSchema.ToSharedRef()))
			.Required({ TEXT("Updates") })
			.Build();
	}();
	Out.Tags = {FNexusMcpTags::Write, FNexusMcpTags::Runtime };
	Out.ExtraSearchKeywords = { TEXT("umg"), TEXT("field"), TEXT("brush"), TEXT("Value"), TEXT("mutate") };
	Out.RelatedCapabilities = { TEXT("GetRuntimeWidgetProperty") };
	Out.Prerequisites = { TEXT("PIE") };
	Out.WhenToUse = TEXT("Modify runtime UMG live fields");
}

FCapabilityResult FNexusSetRuntimeWidgetPropertyCapability::Execute(const TSharedPtr<FJsonObject>& Arguments) const
{

	return FNexusCapabilityResultBuilder::Build([&](auto& OutEntries, auto& OutTop, auto& OutError)
	{

		const TArray<TSharedPtr<FJsonValue>>* UpdatesArr = nullptr;
		if (!Arguments->TryGetArrayField(TEXT("Updates"), UpdatesArr) || !UpdatesArr)
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
				OutEntry->SetStringField(TEXT("Error"), TEXT("Invalid update item"));
				OutEntries.Add(MakeShared<FJsonValueObject>(OutEntry));
				continue;
			}

			FString PropertyPath, NewValue, WidgetName, OwnerClass;
			Item->TryGetStringField(TEXT("PropertyPath"), PropertyPath);
			Item->TryGetStringField(TEXT("Value"),        NewValue);
			Item->TryGetStringField(TEXT("WidgetName"),   WidgetName);
			Item->TryGetStringField(TEXT("OwnerClass"),   OwnerClass);

			if (PropertyPath.IsEmpty())
			{
				OutEntry->SetStringField(TEXT("Error"), TEXT("Each update requires PropertyPath"));
				OutEntries.Add(MakeShared<FJsonValueObject>(OutEntry));
				continue;
			}

			WriteRuntimeWidgetPropertyImpl(WidgetName, OwnerClass, PropertyPath, NewValue, OutEntry);
			OutEntries.Add(MakeShared<FJsonValueObject>(OutEntry));
		}
	
	});
}

REGISTER_MCP_CAPABILITY(FNexusSetRuntimeWidgetPropertyCapability)
