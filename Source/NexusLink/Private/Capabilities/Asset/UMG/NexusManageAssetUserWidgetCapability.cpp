// Copyright byteyang. All Rights Reserved.

#include "Capabilities/Asset/UMG/NexusManageAssetUserWidgetCapability.h"
#include "Utils/NexusCapabilityResultBuilder.h"
#include "NexusCapabilityRegistry.h"
#include "NexusMcpSchemaBuilder.h"
#include "Utils/NexusAssetUtils.h"
#include "Utils/NexusJsonUtils.h"
#include "Utils/NexusArgs.h"
#include "Utils/NexusVersionCompat.h"
#include "Utils/NexusWidgetLayoutUtils.h"
#include "Utils/NexusWidgetAnimationUtils.h"
#include "Utils/NexusPropertyUtils.h"
#if WITH_EDITOR
#include "WidgetBlueprint.h"
#include "Blueprint/WidgetTree.h"
#include "Animation/WidgetAnimation.h"
#endif
#include "Components/Widget.h"
#include "Components/PanelWidget.h"
#include "NexusMcpTool.h"

void FManageAssetUserWidgetCapability::BuildDefinition(FNexusCapabilityDefinition& Out) const
{
	Out.Name = TEXT("manage_asset_user_widget");
	Out.SearchAssetTypes = {TEXT("Widget")};
	Out.Description = TEXT("Batch edit WBP: widget tree/slots/props and animation tracks; EventGraph via manage_asset_blueprint.");
	Out.InputSchema = [this]() -> TSharedPtr<FJsonObject>
	{
		TSharedPtr<FJsonObject> ItemSchema = FNexusSchema::Object()
		.Prop(TEXT("action"),       FNexusSchema::Enum(TEXT("Widget operation"),
			{ TEXT("add"), TEXT("remove"), TEXT("set_slot"), TEXT("set_property"),
			  TEXT("add_animation"), TEXT("remove_animation"), TEXT("add_track"), TEXT("add_key"),
			  TEXT("remove_track"), TEXT("remove_key") }))
		.Prop(TEXT("widgetClass"),  FNexusSchema::Str(TEXT("Widget class short name (add)")))
		.Prop(TEXT("widgetName"),   FNexusSchema::Str(TEXT("Widget name; required for remove/set_*/animation track")))
		.Prop(TEXT("parentWidget"), FNexusSchema::Str(TEXT("Parent panel Widget name (add)")))
		.Prop(TEXT("animationName"), FNexusSchema::Str(TEXT("Animation name (add/remove_animation/track/key)")))
		.Prop(TEXT("trackName"),    FNexusSchema::Str(TEXT("Float track display name (add/remove_track/key)")))
		.Prop(TEXT("propertyPath"), FNexusSchema::Str(TEXT("Property path (set_property; e.g. RenderOpacity for add_track)")))
		.Prop(TEXT("time"),         FNexusSchema::Num(TEXT("Keyframe time seconds (add_key/remove_key)")))
		.Prop(TEXT("keyValue"),     FNexusSchema::Num(TEXT("Float keyframe value (add_key)")))
		.Prop(TEXT("value"),        FNexusSchema::Str(TEXT("Property value (set_property)")))
		.Prop(TEXT("anchorMinX"),   FNexusSchema::Num(TEXT("Canvas anchor minX (set_slot)")))
		.Prop(TEXT("anchorMinY"),   FNexusSchema::Num(TEXT("Canvas anchor minY (set_slot)")))
		.Prop(TEXT("anchorMaxX"),   FNexusSchema::Num(TEXT("Canvas anchor maxX (set_slot)")))
		.Prop(TEXT("anchorMaxY"),   FNexusSchema::Num(TEXT("Canvas anchor maxY (set_slot)")))
		.Prop(TEXT("alignmentX"),   FNexusSchema::Num(TEXT("Alignment X (set_slot)")))
		.Prop(TEXT("alignmentY"),   FNexusSchema::Num(TEXT("Alignment Y (set_slot)")))
		.Prop(TEXT("offsetLeft"),   FNexusSchema::Num(TEXT("offset Left (set_slot)")))
		.Prop(TEXT("offsetTop"),    FNexusSchema::Num(TEXT("offset Top (set_slot)")))
		.Prop(TEXT("offsetRight"),  FNexusSchema::Num(TEXT("offset Right (set_slot)")))
		.Prop(TEXT("offsetBottom"), FNexusSchema::Num(TEXT("offset Bottom (set_slot)")))
		.Required({ TEXT("action") })
		.Build();

		return FNexusSchema::Object()
		.Prop(TEXT("assetPath"),  FNexusSchema::Str(TEXT("WidgetBlueprint asset path (shared)")))
		.Prop(TEXT("operations"), FNexusSchema::ArrayOf(TEXT("Batch widget ops"), ItemSchema.ToSharedRef()))
		.Required({ TEXT("assetPath"), TEXT("operations") })
		.Build();
	}();
	Out.Tags = {FNexusMcpTags::Write, FNexusMcpTags::Widget };
	Out.ExtraSearchKeywords = {
		TEXT("wbp"), TEXT("umg"), TEXT("children"), TEXT("ui"), TEXT("slot"), TEXT("animation"), TEXT("key")
	};
	Out.RelatedCapabilities = {
		TEXT("get_asset_user_widget"), TEXT("create_asset_user_widget"), TEXT("save_asset"),
		TEXT("get_asset_blueprint"), TEXT("manage_asset_blueprint")
	};
	Out.WhenToUse = TEXT("Widget tree/animation tracks use this cap; EventGraph via manage_asset_blueprint");
}

FCapabilityResult FManageAssetUserWidgetCapability::Execute(const TSharedPtr<FJsonObject>& Arguments) const
{
#if WITH_EDITOR
	return FNexusCapabilityResultBuilder::Build([&](auto& OutEntries, auto& OutTop, auto& OutError)
	{
		const FNexusArgs A(Arguments);


		const FString AssetPath = A.Str(TEXT("assetPath"));

		UWidgetBlueprint* WBP = FNexusAssetUtils::LoadWidgetBP(AssetPath);
		if (!WBP)           { OutError = FString::Printf(TEXT("WidgetBlueprint not found: %s"), *AssetPath); return; }
		if (!WBP->WidgetTree) { OutError = TEXT("WidgetTree unavailable"); return; }

		const TArray<TSharedPtr<FJsonValue>> Ops = FNexusJsonUtils::ExtractOperations(Arguments);
		if (Ops.Num() == 0)
		{
			OutError = TEXT("Missing or empty operations");
			return;
		}

		bool bDidMutate = false;
		for (const TSharedPtr<FJsonValue>& Val : Ops)
		{
			TSharedPtr<FJsonObject> Item = Val->AsObject();
			TSharedPtr<FJsonObject> OutEntry = MakeShared<FJsonObject>();

			if (!Item.IsValid())
			{
				OutEntry->SetStringField(TEXT("error"), TEXT("Invalid operation item"));
				OutEntries.Add(MakeShared<FJsonValueObject>(OutEntry));
				continue;
			}

			const FString Action = Item->HasField(TEXT("action")) ? Item->GetStringField(TEXT("action")).ToLower() : TEXT("");
			OutEntry->SetStringField(TEXT("action"), Action);

			if (Action.IsEmpty())
			{
				OutEntry->SetStringField(TEXT("error"), TEXT("Missing action"));
			}
			else if (Action == TEXT("remove"))
			{
				FString WidgetName;
				if (!Item->TryGetStringField(TEXT("widgetName"), WidgetName) || WidgetName.IsEmpty())
				{
					OutEntry->SetStringField(TEXT("error"), TEXT("action=remove requires widgetName"));
				}
				else
				{
					OutEntry->SetStringField(TEXT("widgetName"), WidgetName);
					UWidget* Target = WBP->WidgetTree->FindWidget(FName(*WidgetName));
					if (!Target)
					{
						OutEntry->SetStringField(TEXT("error"), FString::Printf(TEXT("Widget not found: %s"), *WidgetName));
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
					OutEntry->SetStringField(TEXT("error"), TEXT("action=add requires widgetClass"));
				}
				else
				{
					FString WidgetName, ParentName;
					Item->TryGetStringField(TEXT("widgetName"),   WidgetName);
					Item->TryGetStringField(TEXT("parentWidget"), ParentName);

					UClass* NewClass = FNexusAssetUtils::FindClassWithUPrefix(WidgetClass);
					if (!NewClass || !NewClass->IsChildOf(UWidget::StaticClass()))
					{
						OutEntry->SetStringField(TEXT("error"), FString::Printf(TEXT("Widget class not found: %s"), *WidgetClass));
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
							OutEntry->SetStringField(TEXT("error"), FString::Printf(TEXT("Create Widget failed: %s"), *WidgetClass));
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
									OutEntry->SetStringField(TEXT("error"), FString::Printf(TEXT("Parent panel Widget not found: %s"), *ParentName));
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
					OutEntry->SetStringField(TEXT("error"), TEXT("action=set_slot requires widgetName"));
				}
				else
				{
					UWidget* Target = WBP->WidgetTree->FindWidget(FName(*WidgetName));
					if (!Target)
					{
						OutEntry->SetStringField(TEXT("error"), FString::Printf(TEXT("Widget not found: %s"), *WidgetName));
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
					OutEntry->SetStringField(TEXT("error"), TEXT("set_property requires widgetName、propertyPath、value"));
				}
				else
				{
					UWidget* Target = WBP->WidgetTree->FindWidget(FName(*WidgetName));
					if (!Target)
					{
						OutEntry->SetStringField(TEXT("error"), FString::Printf(TEXT("Widget not found: %s"), *WidgetName));
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
			else if (Action == TEXT("add_animation"))
			{
				FString AnimName;
				if (!Item->TryGetStringField(TEXT("animationName"), AnimName) || AnimName.IsEmpty())
				{
					OutEntry->SetStringField(TEXT("error"), TEXT("add_animation requires animationName"));
				}
				else
				{
					FString AnimErr;
					UWidgetAnimation* Anim = FNexusWidgetAnimationUtils::AddAnimation(WBP, AnimName, AnimErr);
					if (!Anim)
					{
						OutEntry->SetStringField(TEXT("error"), AnimErr);
					}
					else
					{
						OutEntry->SetStringField(TEXT("animationName"), Anim->GetName());
						bDidMutate = true;
					}
				}
			}
			else if (Action == TEXT("remove_animation"))
			{
				FString AnimName;
				if (!Item->TryGetStringField(TEXT("animationName"), AnimName) || AnimName.IsEmpty())
				{
					OutEntry->SetStringField(TEXT("error"), TEXT("remove_animation requires animationName"));
				}
				else
				{
					FString AnimErr;
					if (!FNexusWidgetAnimationUtils::RemoveAnimation(WBP, AnimName, AnimErr))
					{
						OutEntry->SetStringField(TEXT("error"), AnimErr);
					}
					else
					{
						OutEntry->SetStringField(TEXT("animationName"), AnimName);
						bDidMutate = true;
					}
				}
			}
			else if (Action == TEXT("add_track"))
			{
				FString AnimName, TrackName, WidgetName, PropPath;
				Item->TryGetStringField(TEXT("animationName"), AnimName);
				Item->TryGetStringField(TEXT("trackName"), TrackName);
				Item->TryGetStringField(TEXT("widgetName"), WidgetName);
				Item->TryGetStringField(TEXT("propertyPath"), PropPath);
				UWidgetAnimation* Anim = FNexusWidgetAnimationUtils::FindAnimation(WBP, AnimName);
				if (!Anim)
				{
					OutEntry->SetStringField(TEXT("error"), TEXT("add_track requires existing animationName"));
				}
				else
				{
					FString OutTrack, AnimErr;
					const bool bOk = WidgetName.IsEmpty()
						? FNexusWidgetAnimationUtils::AddFloatTrack(Anim, TrackName, OutTrack, AnimErr)
						: FNexusWidgetAnimationUtils::AddBoundFloatTrack(Anim, WBP, WidgetName, PropPath, TrackName, OutTrack, AnimErr);
					if (!bOk)
					{
						OutEntry->SetStringField(TEXT("error"), AnimErr);
					}
					else
					{
						OutEntry->SetStringField(TEXT("animationName"), Anim->GetName());
						OutEntry->SetStringField(TEXT("trackName"), OutTrack);
						if (!WidgetName.IsEmpty()) OutEntry->SetStringField(TEXT("widgetName"), WidgetName);
						if (!PropPath.IsEmpty()) OutEntry->SetStringField(TEXT("propertyPath"), PropPath);
						bDidMutate = true;
					}
				}
			}
			else if (Action == TEXT("add_key"))
			{
				FString AnimName, TrackName;
				Item->TryGetStringField(TEXT("animationName"), AnimName);
				Item->TryGetStringField(TEXT("trackName"), TrackName);
				UWidgetAnimation* Anim = FNexusWidgetAnimationUtils::FindAnimation(WBP, AnimName);
				if (!Anim)
				{
					OutEntry->SetStringField(TEXT("error"), TEXT("add_key requires existing animationName"));
				}
				else if (!Item->HasField(TEXT("time")) || !Item->HasField(TEXT("keyValue")))
				{
					OutEntry->SetStringField(TEXT("error"), TEXT("add_key requires time and keyValue"));
				}
				else
				{
					const float TimeSec = static_cast<float>(Item->GetNumberField(TEXT("time")));
					const float KeyVal  = static_cast<float>(Item->GetNumberField(TEXT("keyValue")));
					FString AnimErr;
					if (!FNexusWidgetAnimationUtils::AddFloatKey(Anim, TrackName, TimeSec, KeyVal, AnimErr))
					{
						OutEntry->SetStringField(TEXT("error"), AnimErr);
					}
					else
					{
						OutEntry->SetStringField(TEXT("animationName"), Anim->GetName());
						if (!TrackName.IsEmpty()) OutEntry->SetStringField(TEXT("trackName"), TrackName);
						OutEntry->SetNumberField(TEXT("time"), TimeSec);
						OutEntry->SetNumberField(TEXT("keyValue"), KeyVal);
						bDidMutate = true;
					}
				}
			}
			else if (Action == TEXT("remove_track"))
			{
				FString AnimName, TrackName;
				Item->TryGetStringField(TEXT("animationName"), AnimName);
				Item->TryGetStringField(TEXT("trackName"), TrackName);
				UWidgetAnimation* Anim = FNexusWidgetAnimationUtils::FindAnimation(WBP, AnimName);
				if (!Anim)
				{
					OutEntry->SetStringField(TEXT("error"), TEXT("remove_track requires existing animationName"));
				}
				else
				{
					FString AnimErr;
					if (!FNexusWidgetAnimationUtils::RemoveFloatTrack(Anim, TrackName, AnimErr))
					{
						OutEntry->SetStringField(TEXT("error"), AnimErr);
					}
					else
					{
						OutEntry->SetStringField(TEXT("animationName"), Anim->GetName());
						OutEntry->SetStringField(TEXT("trackName"), TrackName);
						bDidMutate = true;
					}
				}
			}
			else if (Action == TEXT("remove_key"))
			{
				FString AnimName, TrackName;
				Item->TryGetStringField(TEXT("animationName"), AnimName);
				Item->TryGetStringField(TEXT("trackName"), TrackName);
				UWidgetAnimation* Anim = FNexusWidgetAnimationUtils::FindAnimation(WBP, AnimName);
				if (!Anim)
				{
					OutEntry->SetStringField(TEXT("error"), TEXT("remove_key requires existing animationName"));
				}
				else if (!Item->HasField(TEXT("time")))
				{
					OutEntry->SetStringField(TEXT("error"), TEXT("remove_key requires time"));
				}
				else
				{
					const float TimeSec = static_cast<float>(Item->GetNumberField(TEXT("time")));
					FString AnimErr;
					if (!FNexusWidgetAnimationUtils::RemoveFloatKey(Anim, TrackName, TimeSec, AnimErr))
					{
						OutEntry->SetStringField(TEXT("error"), AnimErr);
					}
					else
					{
						OutEntry->SetStringField(TEXT("animationName"), Anim->GetName());
						OutEntry->SetNumberField(TEXT("time"), TimeSec);
						bDidMutate = true;
					}
				}
			}
			else
			{
				OutEntry->SetStringField(TEXT("error"),
					FString::Printf(TEXT("Unsupported operation: '%s'. allowedActions: add, remove, set_slot, set_property, add_animation, remove_animation, add_track, add_key, remove_track, remove_key"), *Action));
				TArray<TSharedPtr<FJsonValue>> Allowed;
				Allowed.Add(MakeShared<FJsonValueString>(TEXT("add")));
				Allowed.Add(MakeShared<FJsonValueString>(TEXT("remove")));
				Allowed.Add(MakeShared<FJsonValueString>(TEXT("set_slot")));
				Allowed.Add(MakeShared<FJsonValueString>(TEXT("set_property")));
				Allowed.Add(MakeShared<FJsonValueString>(TEXT("add_animation")));
				Allowed.Add(MakeShared<FJsonValueString>(TEXT("remove_animation")));
				Allowed.Add(MakeShared<FJsonValueString>(TEXT("add_track")));
				Allowed.Add(MakeShared<FJsonValueString>(TEXT("add_key")));
				Allowed.Add(MakeShared<FJsonValueString>(TEXT("remove_track")));
				Allowed.Add(MakeShared<FJsonValueString>(TEXT("remove_key")));
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
		const FNexusArgs A(Arguments);
		OutError = TEXT("manage_asset_user_widget only available in editor builds");
	});
#endif
}

REGISTER_MCP_CAPABILITY(FManageAssetUserWidgetCapability)
