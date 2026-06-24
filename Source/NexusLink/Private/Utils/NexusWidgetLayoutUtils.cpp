// Copyright byteyang. All Rights Reserved.

#include "Utils/NexusWidgetLayoutUtils.h"
#include "Utils/NexusVersionCompat.h"
#include "Components/Widget.h"
#include "Components/TextBlock.h"
#include "Components/RichTextBlock.h"
#include "Components/PanelSlot.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/VerticalBoxSlot.h"
#include "Components/SizeBoxSlot.h"
#include "Components/GridSlot.h"
#include "Widgets/Layout/Anchors.h"

static void SetMarginField(TSharedPtr<FJsonObject>& Obj, const TCHAR* Key, const FMargin& M)
	{
		Obj->SetNumberField(FString::Printf(TEXT("%sLeft"), Key), M.Left);
		Obj->SetNumberField(FString::Printf(TEXT("%sTop"), Key), M.Top);
		Obj->SetNumberField(FString::Printf(TEXT("%sRight"), Key), M.Right);
		Obj->SetNumberField(FString::Printf(TEXT("%sBottom"), Key), M.Bottom);
	}

static TSharedPtr<FJsonObject> AnchorsToJson(const FAnchors& A)
	{
		TSharedPtr<FJsonObject> O = MakeShared<FJsonObject>();
		O->SetNumberField(TEXT("minX"), A.Minimum.X);
		O->SetNumberField(TEXT("minY"), A.Minimum.Y);
		O->SetNumberField(TEXT("maxX"), A.Maximum.X);
		O->SetNumberField(TEXT("maxY"), A.Maximum.Y);
		return O;
	}

static FString HAlignToString(EHorizontalAlignment H)
	{
		switch (H)
		{
		case HAlign_Left:   return TEXT("Left");
		case HAlign_Center: return TEXT("Center");
		case HAlign_Right:  return TEXT("Right");
		case HAlign_Fill:   return TEXT("Fill");
		default:            return TEXT("Unknown");
		}
	}

static FString VAlignToString(EVerticalAlignment V)
	{
		switch (V)
		{
		case VAlign_Top:    return TEXT("Top");
		case VAlign_Center: return TEXT("Center");
		case VAlign_Bottom: return TEXT("Bottom");
		case VAlign_Fill:   return TEXT("Fill");
		default:            return TEXT("Unknown");
		}
	}

static void AppendChildSizeFields(const FSlateChildSize& Size, TSharedPtr<FJsonObject>& Layout)
	{
		Layout->SetNumberField(TEXT("sizeRule"),
			static_cast<double>(static_cast<uint8>(Size.SizeRule.GetValue())));
		Layout->SetNumberField(TEXT("sizeValue"), Size.Value);
	}

static void AppendCanvasSlotFields(UCanvasPanelSlot* Slot, TSharedPtr<FJsonObject>& Layout)
	{
		const FAnchorData& Data = Slot->GetLayout();
		Layout->SetObjectField(TEXT("anchors"), AnchorsToJson(Data.Anchors));
		Layout->SetNumberField(TEXT("alignmentX"), Data.Alignment.X);
		Layout->SetNumberField(TEXT("alignmentY"), Data.Alignment.Y);
		SetMarginField(Layout, TEXT("offset"), Data.Offsets);
#if NX_UE_HAS_UMG_SLOT_GETTERS
		Layout->SetBoolField(TEXT("autoSize"), Slot->GetAutoSize());
		Layout->SetNumberField(TEXT("zOrder"), Slot->GetZOrder());
#else
		Layout->SetBoolField(TEXT("autoSize"), Slot->bAutoSize);
		Layout->SetNumberField(TEXT("zOrder"), Slot->ZOrder);
#endif
	}

static void AppendBoxSlotFields(UPanelSlot* Slot, TSharedPtr<FJsonObject>& Layout)
	{
		if (UHorizontalBoxSlot* HS = Cast<UHorizontalBoxSlot>(Slot))
		{
#if NX_UE_HAS_UMG_SLOT_GETTERS
			Layout->SetStringField(TEXT("horizontalAlignment"), HAlignToString(HS->GetHorizontalAlignment()));
			Layout->SetStringField(TEXT("verticalAlignment"), VAlignToString(HS->GetVerticalAlignment()));
			SetMarginField(Layout, TEXT("padding"), HS->GetPadding());
			AppendChildSizeFields(HS->GetSize(), Layout);
#else
			Layout->SetStringField(TEXT("horizontalAlignment"), HAlignToString(HS->HorizontalAlignment));
			Layout->SetStringField(TEXT("verticalAlignment"), VAlignToString(HS->VerticalAlignment));
			SetMarginField(Layout, TEXT("padding"), HS->Padding);
			AppendChildSizeFields(HS->Size, Layout);
#endif
			return;
		}
		if (UVerticalBoxSlot* VS = Cast<UVerticalBoxSlot>(Slot))
		{
#if NX_UE_HAS_UMG_SLOT_GETTERS
			Layout->SetStringField(TEXT("horizontalAlignment"), HAlignToString(VS->GetHorizontalAlignment()));
			Layout->SetStringField(TEXT("verticalAlignment"), VAlignToString(VS->GetVerticalAlignment()));
			SetMarginField(Layout, TEXT("padding"), VS->GetPadding());
			AppendChildSizeFields(VS->GetSize(), Layout);
#else
			Layout->SetStringField(TEXT("horizontalAlignment"), HAlignToString(VS->HorizontalAlignment));
			Layout->SetStringField(TEXT("verticalAlignment"), VAlignToString(VS->VerticalAlignment));
			SetMarginField(Layout, TEXT("padding"), VS->Padding);
			AppendChildSizeFields(VS->Size, Layout);
#endif
			return;
		}
		if (USizeBoxSlot* SS = Cast<USizeBoxSlot>(Slot))
		{
#if NX_UE_HAS_UMG_SLOT_GETTERS
			Layout->SetStringField(TEXT("horizontalAlignment"), HAlignToString(SS->GetHorizontalAlignment()));
			Layout->SetStringField(TEXT("verticalAlignment"), VAlignToString(SS->GetVerticalAlignment()));
			SetMarginField(Layout, TEXT("padding"), SS->GetPadding());
#else
			Layout->SetStringField(TEXT("horizontalAlignment"), HAlignToString(SS->HorizontalAlignment));
			Layout->SetStringField(TEXT("verticalAlignment"), VAlignToString(SS->VerticalAlignment));
			SetMarginField(Layout, TEXT("padding"), SS->Padding);
#endif
			return;
		}
		if (UGridSlot* GS = Cast<UGridSlot>(Slot))
		{
#if NX_UE_HAS_UMG_SLOT_GETTERS
			Layout->SetNumberField(TEXT("row"), GS->GetRow());
			Layout->SetNumberField(TEXT("column"), GS->GetColumn());
			Layout->SetStringField(TEXT("horizontalAlignment"), HAlignToString(GS->GetHorizontalAlignment()));
			Layout->SetStringField(TEXT("verticalAlignment"), VAlignToString(GS->GetVerticalAlignment()));
			SetMarginField(Layout, TEXT("padding"), GS->GetPadding());
#else
			Layout->SetNumberField(TEXT("row"), GS->Row);
			Layout->SetNumberField(TEXT("column"), GS->Column);
			Layout->SetStringField(TEXT("horizontalAlignment"), HAlignToString(GS->HorizontalAlignment));
			Layout->SetStringField(TEXT("verticalAlignment"), VAlignToString(GS->VerticalAlignment));
			SetMarginField(Layout, TEXT("padding"), GS->Padding);
#endif
		}
	}

void FNexusWidgetLayoutUtils::AppendUmgLayoutFields(UWidget* Widget, TSharedPtr<FJsonObject>& InOutEntry)
{
	if (!Widget || !InOutEntry.IsValid())
	{
		return;
	}

	TSharedPtr<FJsonObject> Layout = MakeShared<FJsonObject>();
	Layout->SetStringField(TEXT("source"), TEXT("umg"));

	if (UTextBlock* TB = Cast<UTextBlock>(Widget))
	{
		Layout->SetBoolField(TEXT("autoWrapText"), TB->GetAutoWrapText());
	}
	else if (URichTextBlock* RT = Cast<URichTextBlock>(Widget))
	{
		Layout->SetBoolField(TEXT("autoWrapText"), RT->GetAutoWrapText());
	}

	if (UPanelSlot* PanelSlot = Widget->Slot)
	{
		Layout->SetStringField(TEXT("slotClass"), PanelSlot->GetClass()->GetName());
		if (UCanvasPanelSlot* CanvasSlot = Cast<UCanvasPanelSlot>(PanelSlot))
		{
			AppendCanvasSlotFields(CanvasSlot, Layout);
		}
		else
		{
			AppendBoxSlotFields(PanelSlot, Layout);
		}
	}

	InOutEntry->SetObjectField(TEXT("layout"), Layout);
}

bool FNexusWidgetLayoutUtils::ApplyCanvasSlotFields(UWidget* Widget, const TSharedPtr<FJsonObject>& Fields, FString& OutError)
{
	if (!Widget || !Fields.IsValid())
	{
		OutError = TEXT("Widget 或 layout 字段无效");
		return false;
	}
	UCanvasPanelSlot* Slot = Cast<UCanvasPanelSlot>(Widget->Slot);
	if (!Slot)
	{
		OutError = TEXT("Widget 无 CanvasPanelSlot；当前仅支持 Canvas 子控件");
		return false;
	}

	if (Fields->HasField(TEXT("anchorMinX")) && Fields->HasField(TEXT("anchorMinY"))
		&& Fields->HasField(TEXT("anchorMaxX")) && Fields->HasField(TEXT("anchorMaxY")))
	{
		FAnchors Anchors(
			static_cast<float>(Fields->GetNumberField(TEXT("anchorMinX"))),
			static_cast<float>(Fields->GetNumberField(TEXT("anchorMinY"))),
			static_cast<float>(Fields->GetNumberField(TEXT("anchorMaxX"))),
			static_cast<float>(Fields->GetNumberField(TEXT("anchorMaxY"))));
#if NX_UE_HAS_UMG_SLOT_GETTERS
		Slot->SetAnchors(Anchors);
#else
		Slot->LayoutData.Anchors = Anchors;
#endif
	}

	if (Fields->HasField(TEXT("alignmentX")) && Fields->HasField(TEXT("alignmentY")))
	{
		const FVector2D Align(
			static_cast<float>(Fields->GetNumberField(TEXT("alignmentX"))),
			static_cast<float>(Fields->GetNumberField(TEXT("alignmentY"))));
#if NX_UE_HAS_UMG_SLOT_GETTERS
		Slot->SetAlignment(Align);
#else
		Slot->LayoutData.Alignment = Align;
#endif
	}

	if (Fields->HasField(TEXT("offsetLeft")) || Fields->HasField(TEXT("offsetTop"))
		|| Fields->HasField(TEXT("offsetRight")) || Fields->HasField(TEXT("offsetBottom")))
	{
#if NX_UE_HAS_UMG_SLOT_GETTERS
		FMargin Offsets = Slot->GetOffsets();
#else
		FMargin Offsets = Slot->LayoutData.Offsets;
#endif
		if (Fields->HasField(TEXT("offsetLeft")))   Offsets.Left   = static_cast<float>(Fields->GetNumberField(TEXT("offsetLeft")));
		if (Fields->HasField(TEXT("offsetTop")))    Offsets.Top    = static_cast<float>(Fields->GetNumberField(TEXT("offsetTop")));
		if (Fields->HasField(TEXT("offsetRight")))  Offsets.Right  = static_cast<float>(Fields->GetNumberField(TEXT("offsetRight")));
		if (Fields->HasField(TEXT("offsetBottom"))) Offsets.Bottom = static_cast<float>(Fields->GetNumberField(TEXT("offsetBottom")));
#if NX_UE_HAS_UMG_SLOT_GETTERS
		Slot->SetOffsets(Offsets);
#else
		Slot->LayoutData.Offsets = Offsets;
#endif
	}

	if (Fields->HasField(TEXT("autoSize")))
	{
#if NX_UE_HAS_UMG_SLOT_GETTERS
		Slot->SetAutoSize(Fields->GetBoolField(TEXT("autoSize")));
#else
		Slot->bAutoSize = Fields->GetBoolField(TEXT("autoSize"));
#endif
	}

	if (Fields->HasField(TEXT("zOrder")))
	{
#if NX_UE_HAS_UMG_SLOT_GETTERS
		Slot->SetZOrder(static_cast<int32>(Fields->GetNumberField(TEXT("zOrder"))));
#else
		Slot->ZOrder = static_cast<int32>(Fields->GetNumberField(TEXT("zOrder")));
#endif
	}

	return true;
}

#if WITH_EDITOR

#include "Widgets/SWidget.h"
#include "Widgets/SBoxPanel.h"
#include "Blueprint/WidgetTree.h"
#include "Blueprint/UserWidget.h"
#include "Components/Widget.h"

#if NX_UE_HAS_SLATE_BOX_PANEL_SLOT_GETTERS

template<typename SlotType>
static void AppendSlateBoxPanelSlotFields(const SlotType& S, TSharedPtr<FJsonObject>& SlotObj)
{
	SlotObj->SetStringField(TEXT("horizontalAlignment"), HAlignToString(S.GetHorizontalAlignment()));
	SlotObj->SetStringField(TEXT("verticalAlignment"), VAlignToString(S.GetVerticalAlignment()));
	SetMarginField(SlotObj, TEXT("padding"), S.GetPadding());
	SlotObj->SetNumberField(TEXT("sizeValue"), S.GetSizeValue());
}

static void TryAppendSlateParentSlot(const TSharedPtr<SWidget>& Widget, TSharedPtr<FJsonObject>& Layout)
	{
		const TSharedPtr<SWidget> Parent = Widget->GetParentWidget();
		if (!Parent.IsValid())
		{
			return;
		}

		FChildren* Children = Parent->GetChildren();
		if (!Children)
		{
			return;
		}

		TSharedPtr<FJsonObject> SlotObj = MakeShared<FJsonObject>();
		SlotObj->SetStringField(TEXT("source"), TEXT("slate"));

		const FName ParentType = Parent->GetType();
		const TSharedRef<SWidget> ChildRef = Widget.ToSharedRef();

		for (int32 i = 0; i < Children->Num(); ++i)
		{
			if (Children->GetChildAt(i) != ChildRef)
			{
				continue;
			}

			SlotObj->SetNumberField(TEXT("slotIndex"), i);

			if (ParentType == FName(TEXT("SHorizontalBox")))
			{
				SHorizontalBox* HBox = static_cast<SHorizontalBox*>(const_cast<SWidget*>(Parent.Get()));
				SlotObj->SetStringField(TEXT("slotType"), TEXT("SHorizontalBox"));
				AppendSlateBoxPanelSlotFields(HBox->GetSlot(i), SlotObj);
			}
			else if (ParentType == FName(TEXT("SVerticalBox")))
			{
				SVerticalBox* VBox = static_cast<SVerticalBox*>(const_cast<SWidget*>(Parent.Get()));
				SlotObj->SetStringField(TEXT("slotType"), TEXT("SVerticalBox"));
				AppendSlateBoxPanelSlotFields(VBox->GetSlot(i), SlotObj);
			}
			break;
		}

		if (SlotObj->HasField(TEXT("slotType")))
		{
			Layout->SetObjectField(TEXT("slateSlot"), SlotObj);
		}
	}

#endif // NX_UE_HAS_SLATE_BOX_PANEL_SLOT_GETTERS

static UWidget* FindUWidgetForSlate(const TSharedPtr<SWidget>& SlateWidget)
	{
		if (!SlateWidget.IsValid())
		{
			return nullptr;
		}
		for (TObjectIterator<UWidget> It; It; ++It)
		{
			UWidget* UW = *It;
			if (!IsValid(UW))
			{
				continue;
			}
			const TSharedPtr<SWidget> Cached = UW->GetCachedWidget();
			if (Cached.IsValid() && Cached.Get() == SlateWidget.Get())
			{
				return UW;
			}
		}
		return nullptr;
}

void FNexusWidgetLayoutUtils::AppendSlateLayoutFields(const TSharedPtr<SWidget>& Widget, TSharedPtr<FJsonObject>& InOutEntry)
{
	if (!Widget.IsValid() || !InOutEntry.IsValid())
	{
		return;
	}

	if (UWidget* UW = FindUWidgetForSlate(Widget))
	{
		AppendUmgLayoutFields(UW, InOutEntry);
		return;
	}

	TSharedPtr<FJsonObject> Layout = MakeShared<FJsonObject>();
	Layout->SetStringField(TEXT("source"), TEXT("slate"));

	if (Widget->GetType() == FName(TEXT("STextBlock")))
	{
		Layout->SetStringField(TEXT("textBlockNote"),
			TEXT("STextBlock::AutoWrapText 需通过关联 UTextBlock 读取；检查 umgWidget 是否已填充"));
	}

#if NX_UE_HAS_SLATE_BOX_PANEL_SLOT_GETTERS
	TryAppendSlateParentSlot(Widget, Layout);
#endif
	InOutEntry->SetObjectField(TEXT("layout"), Layout);
}

#endif // WITH_EDITOR
