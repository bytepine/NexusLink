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

	if (WidgetName.IsEmpty())   { OutEntry->SetStringField(TEXT("error"), TEXT("缺少 widgetName")); return; }
	if (PropertyPath.IsEmpty()) { OutEntry->SetStringField(TEXT("error"), TEXT("缺少 propertyPath")); return; }

	UWidget* Widget = FNexusRuntimeUtils::FindRuntimeWidget(OwnerClass, WidgetName);
	if (!Widget) { OutEntry->SetStringField(TEXT("error"), FString::Printf(TEXT("运行时 Widget '%s' 未找到"), *WidgetName)); return; }

	TArray<FString> Segs;
	PropertyPath.ParseIntoArray(Segs, TEXT("."), true);
	if (Segs.Num() == 0) { OutEntry->SetStringField(TEXT("error"), TEXT("propertyPath 为空")); return; }

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
	Out.Description = TEXT("批量修改运行时 UMG 字段。updates[] 含控件名/路径/值。");
	Out.InputSchema = FNexusSchema::EmptyObject();
	Out.Tags = {FNexusMcpTags::Readonly, FNexusMcpTags::Editor };
	Out.ExtraSearchKeywords = { TEXT("umg"), TEXT("field"), TEXT("brush"), TEXT("value"), TEXT("mutate") };
	Out.RelatedCapabilities = { TEXT("get_runtime_widget_property") };
	Out.Prerequisites = { TEXT("pie") };
	Out.WhenToUse = TEXT("运行时修改 UMG 实时字段");
}

FCapabilityResult FSetRuntimeWidgetPropertyCapability::Execute(const TSharedPtr<FJsonObject>& Arguments) const
{

	return FNexusCapabilityResultBuilder::Build([&](auto& OutEntries, auto& OutTop, auto& OutError)
	{

		const TArray<TSharedPtr<FJsonValue>>* UpdatesArr = nullptr;
		if (!Arguments->TryGetArrayField(TEXT("updates"), UpdatesArr) || !UpdatesArr)
		{
			OutError = TEXT("缺少 updates");
			return;
		}

		for (const TSharedPtr<FJsonValue>& Val : *UpdatesArr)
		{
			TSharedPtr<FJsonObject> Item = Val->AsObject();
			TSharedPtr<FJsonObject> OutEntry = MakeShared<FJsonObject>();

			if (!Item.IsValid())
			{
				OutEntry->SetStringField(TEXT("error"), TEXT("无效的 update 项"));
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
				OutEntry->SetStringField(TEXT("error"), TEXT("每项 update 均须 propertyPath"));
				OutEntries.Add(MakeShared<FJsonValueObject>(OutEntry));
				continue;
			}

			WriteRuntimeWidgetPropertyImpl(WidgetName, OwnerClass, PropertyPath, NewValue, OutEntry);
			OutEntries.Add(MakeShared<FJsonValueObject>(OutEntry));
		}
	
	});
}

REGISTER_MCP_CAPABILITY(FSetRuntimeWidgetPropertyCapability)
