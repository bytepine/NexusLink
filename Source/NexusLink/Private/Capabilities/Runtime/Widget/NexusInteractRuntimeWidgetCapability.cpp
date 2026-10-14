// Copyright byteyang. All Rights Reserved.

#include "Capabilities/Runtime/Widget/NexusInteractRuntimeWidgetCapability.h"
#include "Utils/NexusCapabilityResultBuilder.h"
#include "NexusCapabilityRegistry.h"
#include "NexusMcpSchemaBuilder.h"
#include "Utils/NexusRuntimeUtils.h"
#include "Utils/NexusVersionCompat.h"
#include "Engine/World.h"
#include "Blueprint/UserWidget.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Widget.h"
#include "Components/Button.h"
#include "Components/CheckBox.h"
#include "Components/Slider.h"
#include "Components/TextBlock.h"
#include "Components/EditableTextBox.h"
#include "Components/EditableText.h"
#include "Components/ProgressBar.h"
#include "Components/ComboBoxString.h"
#include "Components/ListView.h"
#include "UObject/UObjectIterator.h"
#include "NexusMcpTool.h"

static UWidget* FindRuntimeWidgetCap(UWorld* World, const FString& WidgetName, const FString& OwnerFilter, FString& OutOwner)
{
	for (TObjectIterator<UUserWidget> It; It; ++It)
	{
		UUserWidget* UW = *It;
		if (!UW || !UW->GetWorld() || UW->GetWorld() != World) continue;
		if (!UW->IsInViewport() && !UW->IsVisible()) continue;
		if (!OwnerFilter.IsEmpty() && !UW->GetName().Contains(OwnerFilter) && !UW->GetClass()->GetName().Contains(OwnerFilter))
			continue;

		UWidget* Found = nullptr;
		FNexusRuntimeUtils::ForEachWidgetRecursive(UW, [&](UWidget* Child)
		{
			if (Found || !Child) return;
			if (Child->GetName() == WidgetName || Child->GetName().Contains(WidgetName))
				Found = Child;
		});
		if (Found) { OutOwner = UW->GetName(); return Found; }
	}
	return nullptr;
}

void FNexusInteractRuntimeWidgetCapability::BuildDefinition(FNexusCapabilityDefinition& Out) const
{
	Out.Name = TEXT("InteractRuntimeWidget");
	Out.Description = TEXT("Trigger runtime UMG events. Action=click|check|toggle|set|read.");
	Out.InputSchema = FNexusSchema::Object()
		.Prop(TEXT("WidgetName"),  FNexusSchema::Str(TEXT("Child Widget name")))
		.Prop(TEXT("Action"),      FNexusSchema::Enum(TEXT("Interaction action"),
			{ TEXT("Click"), TEXT("Check"), TEXT("Uncheck"), TEXT("Toggle"), TEXT("Set"), TEXT("Read") }))
		.Prop(TEXT("Value"),       FNexusSchema::Str(TEXT("New value when Action=set")))
		.Prop(TEXT("OwnerClass"),  FNexusSchema::Str(TEXT("Owner UserWidget class/name filter")))
		.Required({ TEXT("WidgetName"), TEXT("Action") })
		.Build();
	Out.Tags = {FNexusMcpTags::Write, FNexusMcpTags::Runtime };
	Out.ExtraSearchKeywords = { TEXT("Click"), TEXT("button"), TEXT("Toggle"), TEXT("input"), TEXT("UI") };
	Out.RelatedCapabilities = { TEXT("ListRuntimeWidgets"), TEXT("GetRuntimeWidgetProperty") };
	Out.Prerequisites = { TEXT("PIE") };
}

FCapabilityResult FNexusInteractRuntimeWidgetCapability::Execute(const TSharedPtr<FJsonObject>& Arguments) const
{

	return FNexusCapabilityResultBuilder::Build([&](auto& OutEntries, auto& OutTop, auto& OutError)
	{

		FString WidgetName, Action, Value, OwnerFilter;
		if (!Arguments.IsValid() ||
		    !Arguments->TryGetStringField(TEXT("WidgetName"), WidgetName) || WidgetName.IsEmpty() ||
		    !Arguments->TryGetStringField(TEXT("Action"), Action) || Action.IsEmpty())
		{
			OutError = TEXT("WidgetName and action required");
			return;
		}
		Arguments->TryGetStringField(TEXT("Value"),      Value);
		Arguments->TryGetStringField(TEXT("OwnerClass"), OwnerFilter);

		UWorld* World = nullptr;
		for (TObjectIterator<UWorld> It; It; ++It)
		{
			if (It->WorldType == EWorldType::PIE || It->WorldType == EWorldType::Game)
			{ World = *It; break; }
		}
		if (!World) { OutError = TEXT("No running World (start ControlPie first)"); return; }

		TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
		Entry->SetStringField(TEXT("WidgetName"), WidgetName);
		Entry->SetStringField(TEXT("Action"),     Action);

		FString OwnerName;
		UWidget* Target = FindRuntimeWidgetCap(World, WidgetName, OwnerFilter, OwnerName);
		if (!Target)
		{
			Entry->SetStringField(TEXT("Error"), FString::Printf(TEXT("Widget '%s' not found"), *WidgetName));
			OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
			return;
		}

		Entry->SetStringField(TEXT("WidgetName"),  Target->GetName());
		Entry->SetStringField(TEXT("WidgetClass"), Target->GetClass()->GetName());
		Entry->SetStringField(TEXT("OwnerClass"), OwnerName);

		if (UButton* Btn = Cast<UButton>(Target))
		{
			if (Action.Equals(TEXT("Read"), ESearchCase::IgnoreCase))
			{
				if (!Btn->GetIsEnabled()) { Entry->SetBoolField(TEXT("bIsEnabled"), false); }
				if (Btn->IsPressed())     { Entry->SetBoolField(TEXT("bIsPressed"), true); }
			}
			else if (Action.Equals(TEXT("Click"), ESearchCase::IgnoreCase))
			{
				if (!Btn->GetIsEnabled()) { Entry->SetStringField(TEXT("Error"), TEXT("Button is disabled")); OutEntries.Add(MakeShared<FJsonValueObject>(Entry)); return; }
				Btn->OnClicked.Broadcast();
				Entry->SetBoolField(TEXT("bClicked"), true);
			}
			else { Entry->SetStringField(TEXT("Error"), FString::Printf(TEXT("Button does not support Action=%s"), *Action)); OutEntries.Add(MakeShared<FJsonValueObject>(Entry)); return; }
		}
		else if (UCheckBox* CB = Cast<UCheckBox>(Target))
		{
			if (Action.Equals(TEXT("Read"), ESearchCase::IgnoreCase))
			{
				Entry->SetBoolField(TEXT("bIsChecked"), CB->IsChecked());
			}
			else if (Action.Equals(TEXT("Check"), ESearchCase::IgnoreCase))
			{
				CB->SetIsChecked(true); CB->OnCheckStateChanged.Broadcast(true);
				Entry->SetBoolField(TEXT("bIsChecked"), true);
			}
			else if (Action.Equals(TEXT("Uncheck"), ESearchCase::IgnoreCase))
			{
				CB->SetIsChecked(false); CB->OnCheckStateChanged.Broadcast(false);
				Entry->SetBoolField(TEXT("bIsChecked"), false);
			}
			else
			{
				const bool bNew = !CB->IsChecked();
				CB->SetIsChecked(bNew); CB->OnCheckStateChanged.Broadcast(bNew);
				Entry->SetBoolField(TEXT("bIsChecked"), bNew);
			}
		}
		else if (USlider* Slider = Cast<USlider>(Target))
		{
			if (Action.Equals(TEXT("Read"), ESearchCase::IgnoreCase))
			{
				Entry->SetNumberField(TEXT("Value"), Slider->GetValue());
			}
			else if (Action.Equals(TEXT("Set"), ESearchCase::IgnoreCase))
			{
				if (Value.IsEmpty()) { Entry->SetStringField(TEXT("Error"), TEXT("set action requires value")); OutEntries.Add(MakeShared<FJsonValueObject>(Entry)); return; }
				const float NewVal = FCString::Atof(*Value);
				Slider->SetValue(NewVal); Slider->OnValueChanged.Broadcast(NewVal);
				Entry->SetNumberField(TEXT("Value"), NewVal);
			}
			else { Entry->SetStringField(TEXT("Error"), FString::Printf(TEXT("Slider does not support Action=%s"), *Action)); OutEntries.Add(MakeShared<FJsonValueObject>(Entry)); return; }
		}
		else if (UTextBlock* TB = Cast<UTextBlock>(Target))
		{
			if (Action.Equals(TEXT("Set"), ESearchCase::IgnoreCase))
			{
				if (Value.IsEmpty()) { Entry->SetStringField(TEXT("Error"), TEXT("set action requires value")); OutEntries.Add(MakeShared<FJsonValueObject>(Entry)); return; }
				TB->SetText(FText::FromString(Value));
				Entry->SetStringField(TEXT("text"), Value);
			}
			else
			{
				const FString TBText = TB->GetText().ToString();
				if (!TBText.IsEmpty()) { Entry->SetStringField(TEXT("text"), TBText); }
			}
		}
		else if (UEditableTextBox* ETB = Cast<UEditableTextBox>(Target))
		{
			if (Action.Equals(TEXT("Read"), ESearchCase::IgnoreCase))
			{
				const FString ETBText = ETB->GetText().ToString();
				if (!ETBText.IsEmpty()) { Entry->SetStringField(TEXT("text"), ETBText); }
			}
			else if (Action.Equals(TEXT("Set"), ESearchCase::IgnoreCase))
			{
				if (Value.IsEmpty()) { Entry->SetStringField(TEXT("Error"), TEXT("set action requires value")); OutEntries.Add(MakeShared<FJsonValueObject>(Entry)); return; }
				FText NewText = FText::FromString(Value);
				ETB->SetText(NewText); ETB->OnTextChanged.Broadcast(NewText);
				Entry->SetStringField(TEXT("text"), Value);
			}
			else { Entry->SetStringField(TEXT("Error"), FString::Printf(TEXT("EditableTextBox does not support Action=%s"), *Action)); OutEntries.Add(MakeShared<FJsonValueObject>(Entry)); return; }
		}
		else if (UEditableText* ET = Cast<UEditableText>(Target))
		{
			if (Action.Equals(TEXT("Read"), ESearchCase::IgnoreCase))
			{
				const FString ETText = ET->GetText().ToString();
				if (!ETText.IsEmpty()) { Entry->SetStringField(TEXT("text"), ETText); }
			}
			else if (Action.Equals(TEXT("Set"), ESearchCase::IgnoreCase))
			{
				if (Value.IsEmpty()) { Entry->SetStringField(TEXT("Error"), TEXT("set action requires value")); OutEntries.Add(MakeShared<FJsonValueObject>(Entry)); return; }
				FText NewText = FText::FromString(Value);
				ET->SetText(NewText); ET->OnTextChanged.Broadcast(NewText);
				Entry->SetStringField(TEXT("text"), Value);
			}
			else { Entry->SetStringField(TEXT("Error"), FString::Printf(TEXT("EditableText does not support Action=%s"), *Action)); OutEntries.Add(MakeShared<FJsonValueObject>(Entry)); return; }
		}
		else if (UProgressBar* PB = Cast<UProgressBar>(Target))
		{
			if (Action.Equals(TEXT("Read"), ESearchCase::IgnoreCase))
			{
	#if NX_UE_HAS_PROGRESS_GET_PERCENT
				Entry->SetNumberField(TEXT("Percent"), PB->GetPercent());
	#else
				Entry->SetNumberField(TEXT("Percent"), PB->Percent);
	#endif
			}
			else if (Action.Equals(TEXT("Set"), ESearchCase::IgnoreCase))
			{
				if (Value.IsEmpty())
				{
					Entry->SetStringField(TEXT("Error"), TEXT("set action requires value"));
					OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
					return;
				}
				const float NewPercent = FCString::Atof(*Value);
	#if NX_UE_HAS_PROGRESS_GET_PERCENT
				PB->SetPercent(NewPercent);
	#else
				PB->Percent = NewPercent;
	#endif
				Entry->SetNumberField(TEXT("Percent"), NewPercent);
			}
			else
			{
				Entry->SetStringField(TEXT("Error"),
					FString::Printf(TEXT("ProgressBar does not support Action=%s"), *Action));
				OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
				return;
			}
		}
		else if (UComboBoxString* Combo = Cast<UComboBoxString>(Target))
		{
			if (Action.Equals(TEXT("Read"), ESearchCase::IgnoreCase))
			{
				Entry->SetStringField(TEXT("Selected"), Combo->GetSelectedOption());
				Entry->SetNumberField(TEXT("OptionCount"), Combo->GetOptionCount());
			}
			else if (Action.Equals(TEXT("Set"), ESearchCase::IgnoreCase))
			{
				if (Value.IsEmpty()) { Entry->SetStringField(TEXT("Error"), TEXT("set action requires value")); OutEntries.Add(MakeShared<FJsonValueObject>(Entry)); return; }
				Combo->SetSelectedOption(Value);
				Entry->SetStringField(TEXT("Selected"), Combo->GetSelectedOption());
			}
			else { Entry->SetStringField(TEXT("Error"), FString::Printf(TEXT("ComboBox does not support Action=%s"), *Action)); OutEntries.Add(MakeShared<FJsonValueObject>(Entry)); return; }
		}
		else if (UListView* List = Cast<UListView>(Target))
		{
			if (Action.Equals(TEXT("Read"), ESearchCase::IgnoreCase))
			{
				Entry->SetNumberField(TEXT("ItemCount"),
#if NX_UE_HAS_LISTVIEW_GET_NUM_ITEMS
					List->GetNumItems()
#else
					List->GetListItems().Num()
#endif
				);
			}
			else
			{
				Entry->SetStringField(TEXT("Error"), TEXT("ListView supports read only (entry count)"));
				OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
				return;
			}
		}
		else
		{
			Entry->SetStringField(TEXT("Error"),
				FString::Printf(TEXT("Unsupported Widget type: %s"), *Target->GetClass()->GetName()));
		}

		OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
	
	});
}

REGISTER_MCP_CAPABILITY(FNexusInteractRuntimeWidgetCapability)
