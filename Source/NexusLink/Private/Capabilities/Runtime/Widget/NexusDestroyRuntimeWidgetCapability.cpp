// Copyright byteyang. All Rights Reserved.

#include "Capabilities/Runtime/Widget/NexusDestroyRuntimeWidgetCapability.h"
#include "Utils/NexusCapabilityResultBuilder.h"
#include "Utils/NexusArgs.h"
#include "NexusCapabilityRegistry.h"
#include "NexusMcpSchemaBuilder.h"
#include "Utils/NexusRuntimeUtils.h"
#include "Blueprint/UserWidget.h"
#include "NexusMcpTool.h"

void FDestroyRuntimeWidgetCapability::BuildDefinition(FNexusCapabilityDefinition& Out) const
{
	Out.Name = TEXT("destroy_runtime_widget");
	Out.Description = TEXT("Remove and destroy runtime UMG panel from viewport. Locate by widgetName.");
	Out.InputSchema = FNexusSchema::Object()
		.Prop(TEXT("widgetName"), FNexusSchema::Str(TEXT("UserWidget instance name to destroy")))
		.Prop(TEXT("ownerClass"), FNexusSchema::Str(TEXT("Owner UserWidget class/name filter (optional)")))
		.Required({ TEXT("widgetName") })
		.Build();
	Out.Tags = { FNexusMcpTags::Write, FNexusMcpTags::Runtime };
	Out.ExtraSearchKeywords = { TEXT("umg"), TEXT("viewport"), TEXT("remove"), TEXT("close"), TEXT("dismiss") };
	Out.RelatedCapabilities = { TEXT("spawn_runtime_widget"), TEXT("list_runtime_widgets") };
	Out.Prerequisites = { TEXT("pie") };
	Out.WhenToUse = TEXT("Remove UMG panel from viewport in PIE");
}

FCapabilityResult FDestroyRuntimeWidgetCapability::Execute(const TSharedPtr<FJsonObject>& Arguments) const
{
	return FNexusCapabilityResultBuilder::Build([&](auto& OutEntries, auto& OutTop, auto& OutError)
	{
		const FNexusArgs A(Arguments);
		const FString WidgetName = A.Str(TEXT("widgetName"));
		FString OwnerFilter;
		if (Arguments.IsValid()) Arguments->TryGetStringField(TEXT("ownerClass"), OwnerFilter);

		FString WorldError;
		UWorld* World = FNexusRuntimeUtils::RequirePlayWorld(WorldError);
		if (!World) { OutError = WorldError; return; }

		// 按名称查找匹配的 UserWidget
		UUserWidget* FoundWidget = nullptr;
		for (UUserWidget* UW : FNexusRuntimeUtils::GetActiveUserWidgets())
		{
			if (!UW || !UW->GetWorld() || UW->GetWorld() != World) continue;
			if (!OwnerFilter.IsEmpty() && !UW->GetName().Contains(OwnerFilter) && !UW->GetClass()->GetName().Contains(OwnerFilter))
				continue;
			if (UW->GetName() == WidgetName || UW->GetName().Contains(WidgetName))
			{
				FoundWidget = UW;
				break;
			}
		}

		TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
		Entry->SetStringField(TEXT("widgetName"), WidgetName);

		if (!FoundWidget)
		{
			Entry->SetStringField(TEXT("error"), FString::Printf(TEXT("UserWidget '%s' not found"), *WidgetName));
			OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
			return;
		}

		Entry->SetStringField(TEXT("matchedName"),  FoundWidget->GetName());
		Entry->SetStringField(TEXT("widgetClass"), FoundWidget->GetClass()->GetName());

		FoundWidget->RemoveFromParent();
		Entry->SetBoolField(TEXT("removed"), true);

		OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
	});
}

REGISTER_MCP_CAPABILITY(FDestroyRuntimeWidgetCapability)
