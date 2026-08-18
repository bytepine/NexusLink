// Copyright byteyang. All Rights Reserved.

#include "Capabilities/Runtime/Widget/NexusSpawnRuntimeWidgetCapability.h"

#if WITH_EDITOR

#include "Utils/NexusCapabilityResultBuilder.h"
#include "Utils/NexusArgs.h"
#include "NexusCapabilityRegistry.h"
#include "NexusMcpSchemaBuilder.h"
#include "Engine/World.h"
#include "Engine/Engine.h"
#include "Blueprint/UserWidget.h"
#include "WidgetBlueprint.h"
#include "GameFramework/PlayerController.h"
#include "NexusMcpTool.h"

void FSpawnRuntimeWidgetCapability::BuildDefinition(FNexusCapabilityDefinition& Out) const
{
	Out.Name = TEXT("spawn_runtime_widget");
	Out.Description = TEXT("Create and show UMG panel in PIE/Game viewport. Requires assetPath+zOrder.");
	Out.InputSchema = FNexusSchema::Object()
		.Prop(TEXT("assetPath"), FNexusSchema::Str(TEXT("Widget Blueprint asset path")))
		.Prop(TEXT("zOrder"),    FNexusSchema::Int(TEXT("AddToViewport Z-order (default 0)")))
		.Required({ TEXT("assetPath") })
		.Build();
	Out.Tags = {FNexusMcpTags::Write, FNexusMcpTags::Runtime };
	Out.ExtraSearchKeywords = { TEXT("umg"), TEXT("viewport"), TEXT("hud"), TEXT("create"), TEXT("mount") };
	Out.RelatedCapabilities = { TEXT("list_runtime_widgets"), TEXT("interact_runtime_widget") };
	Out.Prerequisites = { TEXT("pie") };
}

FCapabilityResult FSpawnRuntimeWidgetCapability::Execute(const TSharedPtr<FJsonObject>& Arguments) const
{

	return FNexusCapabilityResultBuilder::Build([&](auto& OutEntries, auto& OutTop, auto& OutError)
	{
		const FNexusArgs A(Arguments);

		const FString AssetPath = A.Str(TEXT("assetPath"));

		const int32 ZOrder = Arguments->HasField(TEXT("zOrder"))
			? static_cast<int32>(A.Num(TEXT("zOrder"))) : 0;

		UWorld* World = nullptr;
		if (GEngine)
		{
			for (const FWorldContext& Ctx : GEngine->GetWorldContexts())
			{
				if ((Ctx.WorldType == EWorldType::PIE || Ctx.WorldType == EWorldType::Game) && Ctx.World())
				{ World = Ctx.World(); break; }
			}
		}

		TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
		if (!World) { Entry->SetStringField(TEXT("error"), TEXT("No running World (start control_pie first)")); OutEntries.Add(MakeShared<FJsonValueObject>(Entry)); return; }

		APlayerController* PC = World->GetFirstPlayerController();
		if (!PC) { Entry->SetStringField(TEXT("error"), TEXT("PlayerController not found")); OutEntries.Add(MakeShared<FJsonValueObject>(Entry)); return; }

		UClass* WidgetClass = nullptr;
		UWidgetBlueprint* WBP = LoadObject<UWidgetBlueprint>(nullptr, *AssetPath);
		if (!WBP) WBP = LoadObject<UWidgetBlueprint>(nullptr, *(AssetPath + TEXT(".") + FPaths::GetBaseFilename(AssetPath)));
		if (WBP && WBP->GeneratedClass)
		{
			WidgetClass = WBP->GeneratedClass;
		}
		else
		{
			WidgetClass = LoadObject<UClass>(nullptr, *AssetPath);
		}

		if (!WidgetClass || !WidgetClass->IsChildOf(UUserWidget::StaticClass()))
		{
			Entry->SetStringField(TEXT("error"), FString::Printf(TEXT("UserWidget class not found: %s"), *AssetPath));
			OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
			return;
		}

		UUserWidget* Widget = CreateWidget<UUserWidget>(PC, WidgetClass);
		if (!Widget) { Entry->SetStringField(TEXT("error"), TEXT("CreateWidget failed")); OutEntries.Add(MakeShared<FJsonValueObject>(Entry)); return; }
		Widget->AddToViewport(ZOrder);

		Entry->SetStringField(TEXT("widgetName"),  Widget->GetName());
		Entry->SetStringField(TEXT("widgetClass"), WidgetClass->GetName());
		Entry->SetNumberField(TEXT("zOrder"),      ZOrder);
		Entry->SetStringField(TEXT("note"), TEXT("Use list_runtime_widgets to enumerate; interact_runtime_widget to operate"));
		OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
	
	});
}

REGISTER_MCP_CAPABILITY(FSpawnRuntimeWidgetCapability)

#endif // WITH_EDITOR
