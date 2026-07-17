// Copyright byteyang. All Rights Reserved.

#include "Capabilities/Asset/UMG/NexusManageAssetUserWidgetCapability.h"
#include "Utils/NexusCapabilityResultBuilder.h"
#include "NexusCapabilityRegistry.h"
#include "NexusMcpSchemaBuilder.h"
#include "Utils/NexusAssetUtils.h"
#include "Utils/NexusVersionCompat.h"
#include "Utils/NexusWidgetLayoutUtils.h"
#include "Utils/NexusPropertyUtils.h"
#if WITH_EDITOR
#include "WidgetBlueprint.h"
#include "Blueprint/WidgetTree.h"
#endif
#include "Components/Widget.h"
#include "Components/PanelWidget.h"
#include "NexusMcpTool.h"

void FManageAssetUserWidgetCapability::BuildDefinition(FNexusCapabilityDefinition& Out) const
{
	Out.Name = TEXT("manage_asset_user_widget");
	Out.SearchAssetTypes = {TEXT("Widget")};
	Out.Description = TEXT("批量编辑 WBP。增删子控件、Canvas 布局与属性；须保存。");
	Out.InputSchema = [this]() -> TSharedPtr<FJsonObject>
	{
		TSharedPtr<FJsonObject> ItemSchema = FNexusSchema::Object()
		.Prop(TEXT("action"),       FNexusSchema::Enum(TEXT("Widget 操作"),
			{ TEXT("add"), TEXT("remove"), TEXT("set_slot"), TEXT("set_property") }))
		.Prop(TEXT("widgetClass"),  FNexusSchema::Str(TEXT("Widget 类短名（add）")))
		.Prop(TEXT("widgetName"),   FNexusSchema::Str(TEXT("Widget 名；remove/set_* 时必填")))
		.Prop(TEXT("parentWidget"), FNexusSchema::Str(TEXT("父面板 Widget 名（add）")))
		.Prop(TEXT("propertyPath"), FNexusSchema::Str(TEXT("属性路径（set_property）")))
		.Prop(TEXT("value"),        FNexusSchema::Str(TEXT("属性值（set_property）")))
		.Prop(TEXT("anchorMinX"),   FNexusSchema::Num(TEXT("Canvas 锚点 minX（set_slot）")))
		.Prop(TEXT("anchorMinY"),   FNexusSchema::Num(TEXT("Canvas 锚点 minY（set_slot）")))
		.Prop(TEXT("anchorMaxX"),   FNexusSchema::Num(TEXT("Canvas 锚点 maxX（set_slot）")))
		.Prop(TEXT("anchorMaxY"),   FNexusSchema::Num(TEXT("Canvas 锚点 maxY（set_slot）")))
		.Prop(TEXT("alignmentX"),   FNexusSchema::Num(TEXT("对齐 X（set_slot）")))
		.Prop(TEXT("alignmentY"),   FNexusSchema::Num(TEXT("对齐 Y（set_slot）")))
		.Prop(TEXT("offsetLeft"),   FNexusSchema::Num(TEXT("偏移 Left（set_slot）")))
		.Prop(TEXT("offsetTop"),    FNexusSchema::Num(TEXT("偏移 Top（set_slot）")))
		.Prop(TEXT("offsetRight"),  FNexusSchema::Num(TEXT("偏移 Right（set_slot）")))
		.Prop(TEXT("offsetBottom"), FNexusSchema::Num(TEXT("偏移 Bottom（set_slot）")))
		.Required({ TEXT("action") })
		.Build();

		return FNexusSchema::Object()
		.Prop(TEXT("assetPath"), FNexusSchema::Str(TEXT("WidgetBlueprint 资产路径（共用）")))
		.Prop(TEXT("widgets"),   FNexusSchema::ArrayOf(TEXT("批量 Widget 操作"), ItemSchema.ToSharedRef()))
		.Required({ TEXT("assetPath"), TEXT("widgets") })
		.Build();
	}();
	Out.Tags = {FNexusMcpTags::Write, FNexusMcpTags::Widget };
	Out.ExtraSearchKeywords = {
		TEXT("wbp"), TEXT("umg"), TEXT("children"), TEXT("ui"), TEXT("slot")
	};
	Out.RelatedCapabilities = { TEXT("get_asset_user_widget"), TEXT("create_asset_user_widget"), TEXT("save_asset") };
	Out.WhenToUse = TEXT("增删子控件、Canvas slot、Widget 属性");
}

FCapabilityResult FManageAssetUserWidgetCapability::Execute(const TSharedPtr<FJsonObject>& Arguments) const
{
#if WITH_EDITOR
	return FNexusCapabilityResultBuilder::Build([&](auto& OutEntries, auto& OutTop, auto& OutError)
	{

		if (!Arguments.IsValid())
		{
			OutError = TEXT("参数无效");
			return;
		}

		FString AssetPath;
		if (!Arguments->TryGetStringField(TEXT("assetPath"), AssetPath) || AssetPath.IsEmpty())
		{
			OutError = TEXT("assetPath 为必填项");
			return;
		}

		UWidgetBlueprint* WBP = FNexusAssetUtils::LoadWidgetBP(AssetPath);
		if (!WBP)           { OutError = FString::Printf(TEXT("WidgetBlueprint 未找到: %s"), *AssetPath); return; }
		if (!WBP->WidgetTree) { OutError = TEXT("WidgetTree 不可用"); return; }

		const TArray<TSharedPtr<FJsonValue>>* WidgetsArr = nullptr;
		if (!Arguments->TryGetArrayField(TEXT("widgets"), WidgetsArr) || !WidgetsArr)
		{
			OutError = TEXT("缺少 widgets");
			return;
		}
		if (WidgetsArr->Num() == 0)
		{
			OutError = TEXT("widgets 不能为空");
			return;
		}

		bool bDidMutate = false;
		for (const TSharedPtr<FJsonValue>& Val : *WidgetsArr)
		{
			TSharedPtr<FJsonObject> Item = Val->AsObject();
			TSharedPtr<FJsonObject> OutEntry = MakeShared<FJsonObject>();

			if (!Item.IsValid())
			{
				OutEntry->SetStringField(TEXT("error"), TEXT("无效的 widget 项"));
				OutEntries.Add(MakeShared<FJsonValueObject>(OutEntry));
				continue;
			}

			const FString Action = Item->HasField(TEXT("action")) ? Item->GetStringField(TEXT("action")).ToLower() : TEXT("");
			OutEntry->SetStringField(TEXT("action"), Action);

			if (Action.IsEmpty())
			{
				OutEntry->SetStringField(TEXT("error"), TEXT("缺少 action"));
			}
			else if (Action == TEXT("remove"))
			{
				FString WidgetName;
				if (!Item->TryGetStringField(TEXT("widgetName"), WidgetName) || WidgetName.IsEmpty())
				{
					OutEntry->SetStringField(TEXT("error"), TEXT("action=remove 时 widgetName 必填"));
				}
				else
				{
					OutEntry->SetStringField(TEXT("widgetName"), WidgetName);
					UWidget* Target = WBP->WidgetTree->FindWidget(FName(*WidgetName));
					if (!Target)
					{
						OutEntry->SetStringField(TEXT("error"), FString::Printf(TEXT("Widget 未找到: %s"), *WidgetName));
					}
					else
					{
						WBP->WidgetTree->Modify();
						if (Target->Slot && Target->Slot->Parent) Target->Slot->Parent->RemoveChild(Target);
						else if (WBP->WidgetTree->RootWidget == Target) WBP->WidgetTree->RootWidget = nullptr;
						Target->Rename(nullptr, GetTransientPackage());
	#if NX_UE_HAS_MARK_AS_GARBAGE
						Target->MarkAsGarbage();
	#else
						Target->MarkPendingKill();
	#endif
						bDidMutate = true;
					}
				}
			}
			else if (Action == TEXT("add"))
			{
				FString WidgetClass;
				if (!Item->TryGetStringField(TEXT("widgetClass"), WidgetClass) || WidgetClass.IsEmpty())
				{
					OutEntry->SetStringField(TEXT("error"), TEXT("action=add 时 widgetClass 必填"));
				}
				else
				{
					FString WidgetName, ParentName;
					Item->TryGetStringField(TEXT("widgetName"),   WidgetName);
					Item->TryGetStringField(TEXT("parentWidget"), ParentName);

					UClass* NewClass = FNexusAssetUtils::FindClassWithUPrefix(WidgetClass);
					if (!NewClass || !NewClass->IsChildOf(UWidget::StaticClass()))
					{
						OutEntry->SetStringField(TEXT("error"), FString::Printf(TEXT("Widget 类未找到: %s"), *WidgetClass));
					}
					else
					{
						if (WidgetName.IsEmpty())
							WidgetName = FString::Printf(TEXT("%s_%d"), *WidgetClass, FMath::Rand() % 10000);

						WBP->WidgetTree->SetFlags(RF_Transactional);
						WBP->WidgetTree->Modify();
						UWidget* NewWidget = WBP->WidgetTree->ConstructWidget<UWidget>(NewClass, FName(*WidgetName));
						if (!NewWidget)
						{
							OutEntry->SetStringField(TEXT("error"), FString::Printf(TEXT("创建 Widget 失败: %s"), *WidgetClass));
						}
						else
						{
							FString AttachedTo;
							if (!ParentName.IsEmpty())
							{
								UPanelWidget* Panel = Cast<UPanelWidget>(WBP->WidgetTree->FindWidget(FName(*ParentName)));
								if (!Panel)
								{
									// 父控件不存在或非 Panel，回滚并报错
									NewWidget->Rename(nullptr, GetTransientPackage());
	#if NX_UE_HAS_MARK_AS_GARBAGE
									NewWidget->MarkAsGarbage();
	#else
									NewWidget->MarkPendingKill();
	#endif
									OutEntry->SetStringField(TEXT("error"), FString::Printf(TEXT("父面板 Widget 未找到: %s"), *ParentName));
									OutEntries.Add(MakeShared<FJsonValueObject>(OutEntry));
									continue;
								}
								Panel->AddChild(NewWidget);
								AttachedTo = ParentName;
							}
							else
							{
								if (!WBP->WidgetTree->RootWidget) WBP->WidgetTree->RootWidget = NewWidget;
								AttachedTo = TEXT("(root)");
							}

							OutEntry->SetStringField(TEXT("widgetName"),  NewWidget->GetName());
							OutEntry->SetStringField(TEXT("widgetClass"), NewClass->GetName());
							OutEntry->SetStringField(TEXT("attachedTo"),  AttachedTo);
							bDidMutate = true;
						}
					}
				}
			}
			else if (Action == TEXT("set_slot"))
			{
				FString WidgetName;
				if (!Item->TryGetStringField(TEXT("widgetName"), WidgetName) || WidgetName.IsEmpty())
				{
					OutEntry->SetStringField(TEXT("error"), TEXT("action=set_slot 时 widgetName 必填"));
				}
				else
				{
					UWidget* Target = WBP->WidgetTree->FindWidget(FName(*WidgetName));
					if (!Target)
					{
						OutEntry->SetStringField(TEXT("error"), FString::Printf(TEXT("Widget 未找到: %s"), *WidgetName));
					}
					else
					{
						FString SlotErr;
						if (!FNexusWidgetLayoutUtils::ApplyCanvasSlotFields(Target, Item, SlotErr))
						{
							OutEntry->SetStringField(TEXT("error"), SlotErr);
						}
						else
						{
							WBP->WidgetTree->Modify();
							Target->Modify();
							OutEntry->SetStringField(TEXT("widgetName"), WidgetName);
							bDidMutate = true;
						}
					}
				}
			}
			else if (Action == TEXT("set_property"))
			{
				FString WidgetName, PropPath, Value;
				if (!Item->TryGetStringField(TEXT("widgetName"), WidgetName) || WidgetName.IsEmpty()
					|| !Item->TryGetStringField(TEXT("propertyPath"), PropPath) || PropPath.IsEmpty()
					|| !Item->TryGetStringField(TEXT("value"), Value) || Value.IsEmpty())
				{
					OutEntry->SetStringField(TEXT("error"), TEXT("set_property 需要 widgetName、propertyPath、value"));
				}
				else
				{
					UWidget* Target = WBP->WidgetTree->FindWidget(FName(*WidgetName));
					if (!Target)
					{
						OutEntry->SetStringField(TEXT("error"), FString::Printf(TEXT("Widget 未找到: %s"), *WidgetName));
					}
					else
					{
						FString OldVal, ActualVal, PropErr;
						if (!FNexusPropertyUtils::WritePropertyAndEcho(Target, { PropPath }, 0, Value, OldVal, ActualVal, PropErr))
						{
							OutEntry->SetStringField(TEXT("error"), PropErr);
						}
						else
						{
							WBP->WidgetTree->Modify();
							Target->Modify();
							OutEntry->SetStringField(TEXT("widgetName"), WidgetName);
							OutEntry->SetStringField(TEXT("propertyPath"), PropPath);
							if (!ActualVal.IsEmpty()) OutEntry->SetStringField(TEXT("newValue"), ActualVal);
							bDidMutate = true;
						}
					}
				}
			}
			else
			{
				OutEntry->SetStringField(TEXT("error"),
					FString::Printf(TEXT("不支持的操作: '%s'。allowedActions: add, remove, set_slot, set_property"), *Action));
				TArray<TSharedPtr<FJsonValue>> Allowed;
				Allowed.Add(MakeShared<FJsonValueString>(TEXT("add")));
				Allowed.Add(MakeShared<FJsonValueString>(TEXT("remove")));
				Allowed.Add(MakeShared<FJsonValueString>(TEXT("set_slot")));
				Allowed.Add(MakeShared<FJsonValueString>(TEXT("set_property")));
				OutEntry->SetArrayField(TEXT("allowedActions"), Allowed);
			}

			OutEntries.Add(MakeShared<FJsonValueObject>(OutEntry));
		}

		if (bDidMutate)
		{
			WBP->MarkPackageDirty();
			OutTop->SetStringField(TEXT("hint"), TEXT("Call save_asset to persist changes"));
		}
	
	});
#else
	return FNexusCapabilityResultBuilder::Build([&](auto& OutEntries, auto& OutTop, auto& OutError)
	{
		OutError = TEXT("manage_asset_user_widget 仅在编辑器构建可用");
	});
#endif
}

REGISTER_MCP_CAPABILITY(FManageAssetUserWidgetCapability)
