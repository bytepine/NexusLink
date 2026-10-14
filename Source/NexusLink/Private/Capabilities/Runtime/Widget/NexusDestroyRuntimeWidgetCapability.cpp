// Copyright byteyang. All Rights Reserved.

#include "Capabilities/Runtime/Widget/NexusDestroyRuntimeWidgetCapability.h"
#include "Utils/NexusCapabilityResultBuilder.h"
#include "Utils/NexusArgs.h"
#include "NexusCapabilityRegistry.h"
#include "NexusMcpSchemaBuilder.h"
#include "Utils/NexusRuntimeUtils.h"
#include "Blueprint/UserWidget.h"
#include "NexusMcpTool.h"

void FNexusDestroyRuntimeWidgetCapability::BuildDefinition(FNexusCapabilityDefinition& Out) const
{
	Out.Name = TEXT("DestroyRuntimeWidget");
	Out.Description = TEXT("Remove and destroy runtime UMG panel from viewport. Locate by WidgetName.");
	Out.InputSchema = FNexusSchema::Object()
		.Prop(TEXT("WidgetName"), FNexusSchema::Str(TEXT("UserWidget instance name to destroy")))
		.Prop(TEXT("OwnerClass"), FNexusSchema::Str(TEXT("Owner UserWidget class/name filter (optional)")))
		.Required({ TEXT("WidgetName") })
		.Build();
	Out.Tags = { FNexusMcpTags::Write, FNexusMcpTags::Runtime };
	Out.ExtraSearchKeywords = { TEXT("umg"), TEXT("Viewport"), TEXT("Remove"), TEXT("close"), TEXT("dismiss") };
	Out.RelatedCapabilities = { TEXT("SpawnRuntimeWidget"), TEXT("ListRuntimeWidgets") };
	Out.Prerequisites = { TEXT("PIE") };
	Out.WhenToUse = TEXT("Remove UMG panel from viewport in PIE");
}

FCapabilityResult FNexusDestroyRuntimeWidgetCapability::Execute(const TSharedPtr<FJsonObject>& Arguments) const
{
	return FNexusCapabilityResultBuilder::Build([&](auto& OutEntries, auto& OutTop, auto& OutError)
	{
		const FNexusArgs A(Arguments);
		const FString WidgetName = A.Str(TEXT("WidgetName"));
		FString OwnerFilter;
		if (Arguments.IsValid()) Arguments->TryGetStringField(TEXT("OwnerClass"), OwnerFilter);

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
		Entry->SetStringField(TEXT("WidgetName"), WidgetName);

		if (!FoundWidget)
		{
			Entry->SetStringField(TEXT("Error"), FString::Printf(TEXT("UserWidget '%s' not found"), *WidgetName));
			OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
			return;
		}

		Entry->SetStringField(TEXT("MatchedName"),  FoundWidget->GetName());
		Entry->SetStringField(TEXT("WidgetClass"), FoundWidget->GetClass()->GetName());

		FoundWidget->RemoveFromParent();
		Entry->SetBoolField(TEXT("bRemoved"), true);

		OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
	});
}

REGISTER_MCP_CAPABILITY(FNexusDestroyRuntimeWidgetCapability)
