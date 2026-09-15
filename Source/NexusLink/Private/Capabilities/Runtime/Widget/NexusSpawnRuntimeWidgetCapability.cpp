// Copyright byteyang. All Rights Reserved.

#include "Capabilities/Runtime/Widget/NexusSpawnRuntimeWidgetCapability.h"

#include "Utils/NexusCapabilityResultBuilder.h"
#include "Utils/NexusArgs.h"
#include "Utils/NexusRuntimeUtils.h"
#include "NexusCapabilityRegistry.h"
#include "NexusMcpSchemaBuilder.h"
#include "Engine/World.h"
#include "Misc/Paths.h"
#include "Blueprint/UserWidget.h"
#include "GameFramework/PlayerController.h"
#include "NexusMcpTool.h"
#if WITH_EDITOR
// UWidgetBlueprint（UMGEditor 模块）本类不可直接依赖：NexusLink 是 Runtime 模块，不链接任何
// UnrealEd 系模块。GeneratedClass 是基类 UBlueprint 的成员，用 UBlueprint 即可拿到，
// 不需要 UWidgetBlueprint 的具体类型。
#include "Engine/Blueprint.h"
#endif

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

		TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
		FString WorldError;
		UWorld* World = FNexusRuntimeUtils::RequirePlayWorld(WorldError);
		if (!World)
		{
			Entry->SetStringField(TEXT("error"), WorldError);
			OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
			return;
		}

		APlayerController* PC = World->GetFirstPlayerController();
		if (!PC) { Entry->SetStringField(TEXT("error"), TEXT("PlayerController not found")); OutEntries.Add(MakeShared<FJsonValueObject>(Entry)); return; }

		UClass* WidgetClass = nullptr;
#if WITH_EDITOR
		UBlueprint* WBP = LoadObject<UBlueprint>(nullptr, *AssetPath);
		if (!WBP) WBP = LoadObject<UBlueprint>(nullptr, *(AssetPath + TEXT(".") + FPaths::GetBaseFilename(AssetPath)));
		if (WBP && WBP->GeneratedClass)
		{
			WidgetClass = WBP->GeneratedClass;
		}
		else
#endif
		{
			// cooked 包（Game/DS）资产只剩 WidgetBlueprintGeneratedClass，直接按 UClass 加载
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
