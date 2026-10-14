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
	O->SetNumberField(TEXT("MinX"), A.Minimum.X);
	O->SetNumberField(TEXT("MinY"), A.Minimum.Y);
	O->SetNumberField(TEXT("MaxX"), A.Maximum.X);
	O->SetNumberField(TEXT("MaxY"), A.Maximum.Y);
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
	Layout->SetNumberField(TEXT("SizeRule"),
			static_cast<double>(static_cast<uint8>(Size.SizeRule.GetValue())));
	Layout->SetNumberField(TEXT("SizeValue"), Size.Value);
}

static void AppendCanvasSlotFields(UCanvasPanelSlot* Slot, TSharedPtr<FJsonObject>& Layout)
{
	const FAnchorData& Data = Slot->GetLayout();
	Layout->SetObjectField(TEXT("Anchors"), AnchorsToJson(Data.Anchors));
	Layout->SetNumberField(TEXT("AlignmentX"), Data.Alignment.X);
	Layout->SetNumberField(TEXT("AlignmentY"), Data.Alignment.Y);
	SetMarginField(Layout, TEXT("Offset"), Data.Offsets);
#if NX_UE_HAS_UMG_SLOT_GETTERS
	Layout->SetBoolField(TEXT("bAutoSize"), Slot->GetAutoSize());
	Layout->SetNumberField(TEXT("ZOrder"), Slot->GetZOrder());
#else
	Layout->SetBoolField(TEXT("bAutoSize"), Slot->bAutoSize);
	Layout->SetNumberField(TEXT("ZOrder"), Slot->ZOrder);
#endif
}

static void AppendBoxSlotFields(UPanelSlot* Slot, TSharedPtr<FJsonObject>& Layout)
{
	if (UHorizontalBoxSlot* HS = Cast<UHorizontalBoxSlot>(Slot))
	{
#if NX_UE_HAS_UMG_SLOT_GETTERS
		Layout->SetStringField(TEXT("HorizontalAlignment"), HAlignToString(HS->GetHorizontalAlignment()));
		Layout->SetStringField(TEXT("VerticalAlignment"), VAlignToString(HS->GetVerticalAlignment()));
		SetMarginField(Layout, TEXT("Padding"), HS->GetPadding());
		AppendChildSizeFields(HS->GetSize(), Layout);
#else
		Layout->SetStringField(TEXT("HorizontalAlignment"), HAlignToString(HS->HorizontalAlignment));
		Layout->SetStringField(TEXT("VerticalAlignment"), VAlignToString(HS->VerticalAlignment));
		SetMarginField(Layout, TEXT("Padding"), HS->Padding);
		AppendChildSizeFields(HS->Size, Layout);
#endif
		return;
	}
	if (UVerticalBoxSlot* VS = Cast<UVerticalBoxSlot>(Slot))
	{
#if NX_UE_HAS_UMG_SLOT_GETTERS
		Layout->SetStringField(TEXT("HorizontalAlignment"), HAlignToString(VS->GetHorizontalAlignment()));
		Layout->SetStringField(TEXT("VerticalAlignment"), VAlignToString(VS->GetVerticalAlignment()));
		SetMarginField(Layout, TEXT("Padding"), VS->GetPadding());
		AppendChildSizeFields(VS->GetSize(), Layout);
#else
		Layout->SetStringField(TEXT("HorizontalAlignment"), HAlignToString(VS->HorizontalAlignment));
		Layout->SetStringField(TEXT("VerticalAlignment"), VAlignToString(VS->VerticalAlignment));
		SetMarginField(Layout, TEXT("Padding"), VS->Padding);
		AppendChildSizeFields(VS->Size, Layout);
#endif
		return;
	}
	if (USizeBoxSlot* SS = Cast<USizeBoxSlot>(Slot))
	{
#if NX_UE_HAS_UMG_SLOT_GETTERS
		Layout->SetStringField(TEXT("HorizontalAlignment"), HAlignToString(SS->GetHorizontalAlignment()));
		Layout->SetStringField(TEXT("VerticalAlignment"), VAlignToString(SS->GetVerticalAlignment()));
		SetMarginField(Layout, TEXT("Padding"), SS->GetPadding());
#else
		Layout->SetStringField(TEXT("HorizontalAlignment"), HAlignToString(SS->HorizontalAlignment));
		Layout->SetStringField(TEXT("VerticalAlignment"), VAlignToString(SS->VerticalAlignment));
		SetMarginField(Layout, TEXT("Padding"), SS->Padding);
#endif
		return;
	}
	if (UGridSlot* GS = Cast<UGridSlot>(Slot))
	{
#if NX_UE_HAS_UMG_SLOT_GETTERS
		Layout->SetNumberField(TEXT("Row"), GS->GetRow());
		Layout->SetNumberField(TEXT("Column"), GS->GetColumn());
		Layout->SetStringField(TEXT("HorizontalAlignment"), HAlignToString(GS->GetHorizontalAlignment()));
		Layout->SetStringField(TEXT("VerticalAlignment"), VAlignToString(GS->GetVerticalAlignment()));
		SetMarginField(Layout, TEXT("Padding"), GS->GetPadding());
#else
		Layout->SetNumberField(TEXT("Row"), GS->Row);
		Layout->SetNumberField(TEXT("Column"), GS->Column);
		Layout->SetStringField(TEXT("HorizontalAlignment"), HAlignToString(GS->HorizontalAlignment));
		Layout->SetStringField(TEXT("VerticalAlignment"), VAlignToString(GS->VerticalAlignment));
		SetMarginField(Layout, TEXT("Padding"), GS->Padding);
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
	Layout->SetStringField(TEXT("Source"), TEXT("umg"));

	if (UTextBlock* TB = Cast<UTextBlock>(Widget))
	{
		Layout->SetBoolField(TEXT("bAutoWrapText"), TB->GetAutoWrapText());
	}
	else if (URichTextBlock* RT = Cast<URichTextBlock>(Widget))
	{
		Layout->SetBoolField(TEXT("bAutoWrapText"), RT->GetAutoWrapText());
	}

	if (UPanelSlot* PanelSlot = Widget->Slot)
	{
		Layout->SetStringField(TEXT("SlotClass"), PanelSlot->GetClass()->GetName());
		if (UCanvasPanelSlot* CanvasSlot = Cast<UCanvasPanelSlot>(PanelSlot))
		{
			AppendCanvasSlotFields(CanvasSlot, Layout);
		}
		else
		{
			AppendBoxSlotFields(PanelSlot, Layout);
		}
	}

	InOutEntry->SetObjectField(TEXT("Layout"), Layout);
}

bool FNexusWidgetLayoutUtils::ApplyCanvasSlotFields(UWidget* Widget, const TSharedPtr<FJsonObject>& Fields, FString& OutError)
{
	if (!Widget || !Fields.IsValid())
	{
		OutError = TEXT("Invalid Widget or layout field");
		return false;
	}
	UCanvasPanelSlot* Slot = Cast<UCanvasPanelSlot>(Widget->Slot);
	if (!Slot)
	{
		OutError = TEXT("Widget has no CanvasPanelSlot; Canvas children only");
		return false;
	}

	if (Fields->HasField(TEXT("AnchorMinX")) && Fields->HasField(TEXT("AnchorMinY"))
		&& Fields->HasField(TEXT("AnchorMaxX")) && Fields->HasField(TEXT("AnchorMaxY")))
	{
		FAnchors Anchors(
			static_cast<float>(Fields->GetNumberField(TEXT("AnchorMinX"))),
			static_cast<float>(Fields->GetNumberField(TEXT("AnchorMinY"))),
			static_cast<float>(Fields->GetNumberField(TEXT("AnchorMaxX"))),
			static_cast<float>(Fields->GetNumberField(TEXT("AnchorMaxY"))));
#if NX_UE_HAS_UMG_SLOT_GETTERS
		Slot->SetAnchors(Anchors);
#else
		Slot->LayoutData.Anchors = Anchors;
#endif
	}

	if (Fields->HasField(TEXT("AlignmentX")) && Fields->HasField(TEXT("AlignmentY")))
	{
		const FVector2D Align(
			static_cast<float>(Fields->GetNumberField(TEXT("AlignmentX"))),
			static_cast<float>(Fields->GetNumberField(TEXT("AlignmentY"))));
#if NX_UE_HAS_UMG_SLOT_GETTERS
		Slot->SetAlignment(Align);
#else
		Slot->LayoutData.Alignment = Align;
#endif
	}

	if (Fields->HasField(TEXT("OffsetLeft")) || Fields->HasField(TEXT("OffsetTop"))
		|| Fields->HasField(TEXT("OffsetRight")) || Fields->HasField(TEXT("OffsetBottom")))
	{
#if NX_UE_HAS_UMG_SLOT_GETTERS
		FMargin Offsets = Slot->GetOffsets();
#else
		FMargin Offsets = Slot->LayoutData.Offsets;
#endif
		if (Fields->HasField(TEXT("OffsetLeft")))   Offsets.Left   = static_cast<float>(Fields->GetNumberField(TEXT("OffsetLeft")));
		if (Fields->HasField(TEXT("OffsetTop")))    Offsets.Top    = static_cast<float>(Fields->GetNumberField(TEXT("OffsetTop")));
		if (Fields->HasField(TEXT("OffsetRight")))  Offsets.Right  = static_cast<float>(Fields->GetNumberField(TEXT("OffsetRight")));
		if (Fields->HasField(TEXT("OffsetBottom"))) Offsets.Bottom = static_cast<float>(Fields->GetNumberField(TEXT("OffsetBottom")));
#if NX_UE_HAS_UMG_SLOT_GETTERS
		Slot->SetOffsets(Offsets);
#else
		Slot->LayoutData.Offsets = Offsets;
#endif
	}

	if (Fields->HasField(TEXT("bAutoSize")))
	{
#if NX_UE_HAS_UMG_SLOT_GETTERS
		Slot->SetAutoSize(Fields->GetBoolField(TEXT("bAutoSize")));
#else
		Slot->bAutoSize = Fields->GetBoolField(TEXT("bAutoSize"));
#endif
	}

	if (Fields->HasField(TEXT("ZOrder")))
	{
#if NX_UE_HAS_UMG_SLOT_GETTERS
		Slot->SetZOrder(static_cast<int32>(Fields->GetNumberField(TEXT("ZOrder"))));
#else
		Slot->ZOrder = static_cast<int32>(Fields->GetNumberField(TEXT("ZOrder")));
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
	SlotObj->SetStringField(TEXT("HorizontalAlignment"), HAlignToString(S.GetHorizontalAlignment()));
	SlotObj->SetStringField(TEXT("VerticalAlignment"), VAlignToString(S.GetVerticalAlignment()));
	SetMarginField(SlotObj, TEXT("Padding"), S.GetPadding());
	SlotObj->SetNumberField(TEXT("SizeValue"), S.GetSizeValue());
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
	SlotObj->SetStringField(TEXT("Source"), TEXT("slate"));

	const FName ParentType = Parent->GetType();
	const TSharedRef<SWidget> ChildRef = Widget.ToSharedRef();

	for (int32 i = 0; i < Children->Num(); ++i)
	{
		if (Children->GetChildAt(i) != ChildRef)
		{
			continue;
		}

		SlotObj->SetNumberField(TEXT("SlotIndex"), i);

		if (ParentType == FName(TEXT("SHorizontalBox")))
		{
			SHorizontalBox* HBox = static_cast<SHorizontalBox*>(const_cast<SWidget*>(Parent.Get()));
			SlotObj->SetStringField(TEXT("SlotType"), TEXT("SHorizontalBox"));
			AppendSlateBoxPanelSlotFields(HBox->GetSlot(i), SlotObj);
		}
		else if (ParentType == FName(TEXT("SVerticalBox")))
		{
			SVerticalBox* VBox = static_cast<SVerticalBox*>(const_cast<SWidget*>(Parent.Get()));
			SlotObj->SetStringField(TEXT("SlotType"), TEXT("SVerticalBox"));
			AppendSlateBoxPanelSlotFields(VBox->GetSlot(i), SlotObj);
		}
		break;
	}

	if (SlotObj->HasField(TEXT("SlotType")))
	{
		Layout->SetObjectField(TEXT("SlateSlot"), SlotObj);
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
	Layout->SetStringField(TEXT("Source"), TEXT("slate"));

	if (Widget->GetType() == FName(TEXT("STextBlock")))
	{
		Layout->SetStringField(TEXT("TextBlockNote"),
			TEXT("STextBlock::AutoWrapText requires linked UTextBlock; check UMGWidget is populated"));
	}

#if NX_UE_HAS_SLATE_BOX_PANEL_SLOT_GETTERS
	TryAppendSlateParentSlot(Widget, Layout);
#endif
	InOutEntry->SetObjectField(TEXT("Layout"), Layout);
}

#endif // WITH_EDITOR
