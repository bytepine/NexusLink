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

void FInteractRuntimeWidgetCapability::BuildDefinition(FNexusCapabilityDefinition& Out) const
{
	Out.Name = TEXT("interact_runtime_widget");
	Out.Description = TEXT("触发运行时 UMG 事件。action=click|check|toggle|set|read。");
	Out.InputSchema = FNexusSchema::Object()
		.Prop(TEXT("widgetName"),  FNexusSchema::Str(TEXT("子 Widget 名")))
		.Prop(TEXT("action"),      FNexusSchema::Enum(TEXT("交互操作"),
			{ TEXT("click"), TEXT("check"), TEXT("uncheck"), TEXT("toggle"), TEXT("set"), TEXT("read") }))
		.Prop(TEXT("value"),       FNexusSchema::Str(TEXT("action=set 时的新值")))
		.Prop(TEXT("ownerWidget"), FNexusSchema::Str(TEXT("Owner UserWidget 类/名过滤")))
		.Required({ TEXT("widgetName"), TEXT("action") })
		.Build();
	Out.Tags = {FNexusMcpTags::Write, FNexusMcpTags::Runtime };
	Out.ExtraSearchKeywords = { TEXT("click"), TEXT("button"), TEXT("toggle"), TEXT("input"), TEXT("ui") };
	Out.RelatedCapabilities = { TEXT("list_runtime_widgets"), TEXT("get_runtime_widget_property") };
	Out.Prerequisites = { TEXT("pie") };
}

FCapabilityResult FInteractRuntimeWidgetCapability::Execute(const TSharedPtr<FJsonObject>& Arguments) const
{

	return FNexusCapabilityResultBuilder::Build([&](auto& OutEntries, auto& OutTop, auto& OutError)
	{

		FString WidgetName, Action, Value, OwnerFilter;
		if (!Arguments.IsValid() ||
		    !Arguments->TryGetStringField(TEXT("widgetName"), WidgetName) || WidgetName.IsEmpty() ||
		    !Arguments->TryGetStringField(TEXT("action"), Action) || Action.IsEmpty())
		{
			OutError = TEXT("需要 widgetName 与 action");
			return;
		}
		Arguments->TryGetStringField(TEXT("value"),       Value);
		Arguments->TryGetStringField(TEXT("ownerWidget"), OwnerFilter);

		UWorld* World = nullptr;
		for (TObjectIterator<UWorld> It; It; ++It)
		{
			if (It->WorldType == EWorldType::PIE || It->WorldType == EWorldType::Game)
			{ World = *It; break; }
		}
		if (!World) { OutError = TEXT("无运行中的 World（请先 control_pie start）"); return; }

		TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
		Entry->SetStringField(TEXT("widgetName"), WidgetName);
		Entry->SetStringField(TEXT("action"),     Action);

		FString OwnerName;
		UWidget* Target = FindRuntimeWidgetCap(World, WidgetName, OwnerFilter, OwnerName);
		if (!Target)
		{
			Entry->SetStringField(TEXT("error"), FString::Printf(TEXT("Widget '%s' 未找到"), *WidgetName));
			OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
			return;
		}

		Entry->SetStringField(TEXT("widgetName"),  Target->GetName());
		Entry->SetStringField(TEXT("widgetClass"), Target->GetClass()->GetName());
		Entry->SetStringField(TEXT("ownerWidget"), OwnerName);

		if (UButton* Btn = Cast<UButton>(Target))
		{
			if (Action.Equals(TEXT("read"), ESearchCase::IgnoreCase))
			{
				if (!Btn->GetIsEnabled()) { Entry->SetBoolField(TEXT("isEnabled"), false); }
				if (Btn->IsPressed())     { Entry->SetBoolField(TEXT("isPressed"), true); }
			}
			else if (Action.Equals(TEXT("click"), ESearchCase::IgnoreCase))
			{
				if (!Btn->GetIsEnabled()) { Entry->SetStringField(TEXT("error"), TEXT("Button 已禁用")); OutEntries.Add(MakeShared<FJsonValueObject>(Entry)); return; }
				Btn->OnClicked.Broadcast();
				Entry->SetBoolField(TEXT("clicked"), true);
			}
			else { Entry->SetStringField(TEXT("error"), FString::Printf(TEXT("Button 不支持 action=%s"), *Action)); OutEntries.Add(MakeShared<FJsonValueObject>(Entry)); return; }
		}
		else if (UCheckBox* CB = Cast<UCheckBox>(Target))
		{
			if (Action.Equals(TEXT("read"), ESearchCase::IgnoreCase))
			{
				Entry->SetBoolField(TEXT("isChecked"), CB->IsChecked());
			}
			else if (Action.Equals(TEXT("check"), ESearchCase::IgnoreCase))
			{
				CB->SetIsChecked(true); CB->OnCheckStateChanged.Broadcast(true);
				Entry->SetBoolField(TEXT("isChecked"), true);
			}
			else if (Action.Equals(TEXT("uncheck"), ESearchCase::IgnoreCase))
			{
				CB->SetIsChecked(false); CB->OnCheckStateChanged.Broadcast(false);
				Entry->SetBoolField(TEXT("isChecked"), false);
			}
			else
			{
				const bool bNew = !CB->IsChecked();
				CB->SetIsChecked(bNew); CB->OnCheckStateChanged.Broadcast(bNew);
				Entry->SetBoolField(TEXT("isChecked"), bNew);
			}
		}
		else if (USlider* Slider = Cast<USlider>(Target))
		{
			if (Action.Equals(TEXT("read"), ESearchCase::IgnoreCase))
			{
				Entry->SetNumberField(TEXT("value"), Slider->GetValue());
			}
			else if (Action.Equals(TEXT("set"), ESearchCase::IgnoreCase))
			{
				if (Value.IsEmpty()) { Entry->SetStringField(TEXT("error"), TEXT("set 操作需要 value")); OutEntries.Add(MakeShared<FJsonValueObject>(Entry)); return; }
				const float NewVal = FCString::Atof(*Value);
				Slider->SetValue(NewVal); Slider->OnValueChanged.Broadcast(NewVal);
				Entry->SetNumberField(TEXT("value"), NewVal);
			}
			else { Entry->SetStringField(TEXT("error"), FString::Printf(TEXT("Slider 不支持 action=%s"), *Action)); OutEntries.Add(MakeShared<FJsonValueObject>(Entry)); return; }
		}
		else if (UTextBlock* TB = Cast<UTextBlock>(Target))
		{
			if (Action.Equals(TEXT("set"), ESearchCase::IgnoreCase))
			{
				if (Value.IsEmpty()) { Entry->SetStringField(TEXT("error"), TEXT("set 操作需要 value")); OutEntries.Add(MakeShared<FJsonValueObject>(Entry)); return; }
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
			if (Action.Equals(TEXT("read"), ESearchCase::IgnoreCase))
			{
				const FString ETBText = ETB->GetText().ToString();
				if (!ETBText.IsEmpty()) { Entry->SetStringField(TEXT("text"), ETBText); }
			}
			else if (Action.Equals(TEXT("set"), ESearchCase::IgnoreCase))
			{
				if (Value.IsEmpty()) { Entry->SetStringField(TEXT("error"), TEXT("set 操作需要 value")); OutEntries.Add(MakeShared<FJsonValueObject>(Entry)); return; }
				FText NewText = FText::FromString(Value);
				ETB->SetText(NewText); ETB->OnTextChanged.Broadcast(NewText);
				Entry->SetStringField(TEXT("text"), Value);
			}
			else { Entry->SetStringField(TEXT("error"), FString::Printf(TEXT("EditableTextBox 不支持 action=%s"), *Action)); OutEntries.Add(MakeShared<FJsonValueObject>(Entry)); return; }
		}
		else if (UEditableText* ET = Cast<UEditableText>(Target))
		{
			if (Action.Equals(TEXT("read"), ESearchCase::IgnoreCase))
			{
				const FString ETText = ET->GetText().ToString();
				if (!ETText.IsEmpty()) { Entry->SetStringField(TEXT("text"), ETText); }
			}
			else if (Action.Equals(TEXT("set"), ESearchCase::IgnoreCase))
			{
				if (Value.IsEmpty()) { Entry->SetStringField(TEXT("error"), TEXT("set 操作需要 value")); OutEntries.Add(MakeShared<FJsonValueObject>(Entry)); return; }
				FText NewText = FText::FromString(Value);
				ET->SetText(NewText); ET->OnTextChanged.Broadcast(NewText);
				Entry->SetStringField(TEXT("text"), Value);
			}
			else { Entry->SetStringField(TEXT("error"), FString::Printf(TEXT("EditableText 不支持 action=%s"), *Action)); OutEntries.Add(MakeShared<FJsonValueObject>(Entry)); return; }
		}
		else if (UProgressBar* PB = Cast<UProgressBar>(Target))
		{
			if (Action.Equals(TEXT("read"), ESearchCase::IgnoreCase))
			{
	#if NX_UE_HAS_PROGRESS_GET_PERCENT
				Entry->SetNumberField(TEXT("percent"), PB->GetPercent());
	#else
				Entry->SetNumberField(TEXT("percent"), PB->Percent);
	#endif
			}
			else if (Action.Equals(TEXT("set"), ESearchCase::IgnoreCase))
			{
				if (Value.IsEmpty())
				{
					Entry->SetStringField(TEXT("error"), TEXT("set 操作需要 value"));
					OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
					return;
				}
				const float NewPercent = FCString::Atof(*Value);
	#if NX_UE_HAS_PROGRESS_GET_PERCENT
				PB->SetPercent(NewPercent);
	#else
				PB->Percent = NewPercent;
	#endif
				Entry->SetNumberField(TEXT("percent"), NewPercent);
			}
			else
			{
				Entry->SetStringField(TEXT("error"),
					FString::Printf(TEXT("ProgressBar 不支持 action=%s"), *Action));
				OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
				return;
			}
		}
		else
		{
			Entry->SetStringField(TEXT("error"),
				FString::Printf(TEXT("不支持的 Widget 类型: %s"), *Target->GetClass()->GetName()));
		}

		OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
	
	});
}

REGISTER_MCP_CAPABILITY(FInteractRuntimeWidgetCapability)
