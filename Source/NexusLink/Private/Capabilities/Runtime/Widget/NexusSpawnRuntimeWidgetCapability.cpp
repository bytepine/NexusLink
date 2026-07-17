// Copyright byteyang. All Rights Reserved.

#include "Capabilities/Runtime/Widget/NexusSpawnRuntimeWidgetCapability.h"

#if WITH_EDITOR

#include "Utils/NexusCapabilityResultBuilder.h"
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
	Out.Description = TEXT("在 PIE/Game 视口创建并显示 UMG 面板。需 assetPath+zOrder。");
	Out.InputSchema = FNexusSchema::Object()
		.Prop(TEXT("assetPath"), FNexusSchema::Str(TEXT("Widget 蓝图资产路径")))
		.Prop(TEXT("zOrder"),    FNexusSchema::Int(TEXT("AddToViewport 的 Z 序（默认 0）")))
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

		FString AssetPath;
		if (!Arguments.IsValid() || !Arguments->TryGetStringField(TEXT("assetPath"), AssetPath) || AssetPath.IsEmpty())
		{
			OutError = TEXT("缺少 assetPath");
			return;
		}

		const int32 ZOrder = Arguments->HasField(TEXT("zOrder"))
			? static_cast<int32>(Arguments->GetNumberField(TEXT("zOrder"))) : 0;

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
		if (!World) { Entry->SetStringField(TEXT("error"), TEXT("无运行中的 World（请先 control_pie start）")); OutEntries.Add(MakeShared<FJsonValueObject>(Entry)); return; }

		APlayerController* PC = World->GetFirstPlayerController();
		if (!PC) { Entry->SetStringField(TEXT("error"), TEXT("PlayerController 未找到")); OutEntries.Add(MakeShared<FJsonValueObject>(Entry)); return; }

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
			Entry->SetStringField(TEXT("error"), FString::Printf(TEXT("UserWidget 类未找到: %s"), *AssetPath));
			OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
			return;
		}

		UUserWidget* Widget = CreateWidget<UUserWidget>(PC, WidgetClass);
		if (!Widget) { Entry->SetStringField(TEXT("error"), TEXT("CreateWidget 失败")); OutEntries.Add(MakeShared<FJsonValueObject>(Entry)); return; }
		Widget->AddToViewport(ZOrder);

		Entry->SetStringField(TEXT("widgetName"),  Widget->GetName());
		Entry->SetStringField(TEXT("widgetClass"), WidgetClass->GetName());
		Entry->SetNumberField(TEXT("zOrder"),      ZOrder);
		Entry->SetStringField(TEXT("note"), TEXT("用 list_runtime_widgets 枚举 Widget，用 interact_runtime_widget 操作"));
		OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
	
	});
}

REGISTER_MCP_CAPABILITY(FSpawnRuntimeWidgetCapability)

#endif // WITH_EDITOR
