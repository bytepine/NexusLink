// Copyright byteyang. All Rights Reserved.
#include "Utils/NexusWidgetTreeCompat.h"
#include "Utils/NexusArgs.h"
#include "Utils/NexusAssetUtils.h"
#include "WidgetBlueprint.h"
#include "Blueprint/WidgetTree.h"
#include "Components/PanelWidget.h"
#include "Components/ContentWidget.h"
#include "Components/PanelSlot.h"
#include "Components/NamedSlotInterface.h"
#include "Kismet2/BlueprintEditorUtils.h"

static bool NexusWidgetCompatAttach(UWidgetBlueprint* Blueprint, UPanelWidget* Parent, int32 Index, UWidget* Widget)
{
    if (!Parent) { Blueprint->WidgetTree->RootWidget = Widget; return true; }
    if (!Parent->AddChild(Widget)) return false;
    if (Index >= 0 && Index < Parent->GetChildrenCount()) Parent->ShiftChild(Index, Widget);
    return true;
}

bool FNexusWidgetTreeCompat::Apply(UWidgetBlueprint* Blueprint, const TSharedPtr<FJsonObject>& Args, const TSharedPtr<FJsonObject>& Entry, FString& Error)
{
    if (!Blueprint || !Blueprint->WidgetTree) { Error = TEXT("Widget tree is unavailable"); return false; }
    const FNexusArgs A(Args); const FString Action = A.Str(TEXT("Action")), Name = A.Str(TEXT("WidgetName"));
    UWidget* Widget = Blueprint->WidgetTree->FindWidget(FName(*Name));
    UPanelWidget* Parent = Widget ? Widget->GetParent() : nullptr;
    const int32 PreviousIndex = Parent ? Parent->GetChildIndex(Widget) : -1;
    if (Action == TEXT("Move")) {
        UPanelWidget* Destination = Cast<UPanelWidget>(Blueprint->WidgetTree->FindWidget(FName(*A.Str(TEXT("ParentWidget")))));
        const int32 Index = static_cast<int32>(A.Num(TEXT("ChildIndex"), -1));
        if (!Widget || !Destination || Index < -1 || Index > Destination->GetChildrenCount()) { Error = TEXT("Move requires an existing widget, destination panel and valid child index"); return false; }
        for (UWidget* Ancestor = Destination; Ancestor; Ancestor = Ancestor->GetParent()) if (Ancestor == Widget) { Error = TEXT("Move would create a widget-tree cycle"); return false; }
        if (Destination != Parent && Destination->IsA(UContentWidget::StaticClass()) && Destination->GetChildrenCount()) { Error = TEXT("Destination single-child panel is occupied"); return false; }
        Blueprint->Modify(); Blueprint->WidgetTree->Modify(); Destination->Modify(); Widget->Modify();
        if (Parent == Destination) { Destination->ShiftChild(Index < 0 ? Destination->GetChildrenCount()-1 : FMath::Min(Index, Destination->GetChildrenCount()-1), Widget); }
        else {
            if (Parent) { Parent->Modify(); Parent->RemoveChild(Widget); } else Blueprint->WidgetTree->RootWidget = nullptr;
            if (!NexusWidgetCompatAttach(Blueprint, Destination, Index, Widget)) { NexusWidgetCompatAttach(Blueprint, Parent, PreviousIndex, Widget); Error = TEXT("Could not attach to destination"); return false; }
            Entry->SetStringField(TEXT("SlotNote"), TEXT("Cross-panel moves create a destination slot; configure its layout with SetSlot"));
        }
    } else if (Action == TEXT("ReplaceWithChild")) {
        UPanelWidget* Panel = Cast<UPanelWidget>(Widget);
        if (!Panel || Panel->GetChildrenCount() != 1) { Error = TEXT("ReplaceWithChild requires exactly one child"); return false; }
        UWidget* Child = Panel->GetChildAt(0); Blueprint->Modify(); Blueprint->WidgetTree->Modify(); Panel->Modify(); Child->Modify();
        Panel->RemoveChild(Child); if (Parent) { Parent->Modify(); Parent->RemoveChild(Panel); }
        if (!NexusWidgetCompatAttach(Blueprint, Parent, PreviousIndex, Child)) { Panel->AddChild(Child); NexusWidgetCompatAttach(Blueprint, Parent, PreviousIndex, Panel); Error = TEXT("Could not replace panel"); return false; }
        Blueprint->WidgetTree->RemoveWidget(Panel); Panel->Rename(nullptr, GetTransientPackage(), REN_DontCreateRedirectors);
        Entry->SetStringField(TEXT("ReplacementName"), Child->GetName());
    } else if (Action == TEXT("Wrap") || Action == TEXT("ReplaceWithTemplate") || Action == TEXT("SetNamedSlotContent")) {
        UClass* Class = FNexusAssetUtils::FindClassWithUPrefix(A.Str(TEXT("WidgetClass")));
        if (!Class || !Class->IsChildOf(UWidget::StaticClass()) || Class->HasAnyClassFlags(CLASS_Abstract)) { Error = TEXT("WidgetClass must be concrete"); return false; }
        if (Action == TEXT("SetNamedSlotContent")) {
            UWidget* Host = Blueprint->WidgetTree->FindWidget(FName(*A.Str(TEXT("ParentWidget")))); INamedSlotInterface* Slots = Cast<INamedSlotInterface>(Host);
            TArray<FName> Names; if (Slots) Slots->GetSlotNames(Names);
            const FName Slot(*A.Str(TEXT("SlotName"))); FText NameError;
            if (!Slots || !Names.Contains(Slot) || Slots->GetContentForSlot(Slot) || Widget || Name.IsEmpty() || !FName(*Name).IsValidObjectName(NameError)) { Error = TEXT("Compatibility named slots require an existing local host, an empty named slot and a unique valid widget name"); return false; }
            Blueprint->Modify(); Blueprint->WidgetTree->Modify(); Host->Modify(); UWidget* NewWidget = Blueprint->WidgetTree->ConstructWidget<UWidget>(Class, FName(*Name));
            if (!NewWidget) { Error = TEXT("Could not construct slot content"); return false; } Slots->SetContentForSlot(Slot, NewWidget);
        } else {
            UPanelWidget* Panel = Cast<UPanelWidget>(Widget);
            if (!Widget || (Action == TEXT("Wrap") && !Class->IsChildOf(UPanelWidget::StaticClass())) || (Action == TEXT("ReplaceWithTemplate") && Panel && Panel->GetChildrenCount())) { Error = TEXT("Wrap requires a panel class; template replacement currently requires a leaf or empty panel"); return false; }
            Blueprint->Modify(); Blueprint->WidgetTree->Modify(); Widget->Modify();
            const FName NewName = MakeUniqueObjectName(Blueprint->WidgetTree, Class);
            UWidget* NewWidget = Blueprint->WidgetTree->ConstructWidget<UWidget>(Class, NewName);
            if (!NewWidget) { Error = TEXT("Could not construct replacement"); return false; }
            if (Parent) { Parent->Modify(); Parent->RemoveChild(Widget); } else Blueprint->WidgetTree->RootWidget = nullptr;
            if (!NexusWidgetCompatAttach(Blueprint, Parent, PreviousIndex, NewWidget)) { NexusWidgetCompatAttach(Blueprint, Parent, PreviousIndex, Widget); Blueprint->WidgetTree->RemoveWidget(NewWidget); Error = TEXT("Could not attach replacement"); return false; }
            if (Action == TEXT("Wrap")) {
                if (!CastChecked<UPanelWidget>(NewWidget)->AddChild(Widget)) {
                    if (Parent) Parent->RemoveChild(NewWidget); else Blueprint->WidgetTree->RootWidget=nullptr;
                    NexusWidgetCompatAttach(Blueprint,Parent,PreviousIndex,Widget); Blueprint->WidgetTree->RemoveWidget(NewWidget); Error=TEXT("Wrapper rejected its child"); return false;
                }
                Entry->SetStringField(TEXT("WrapperName"), NewWidget->GetName());
            }
            else { Blueprint->WidgetTree->RemoveWidget(Widget); Widget->Rename(nullptr, GetTransientPackage(), REN_DontCreateRedirectors); Entry->SetStringField(TEXT("ReplacementName"), NewWidget->GetName()); }
        }
    } else { Error = TEXT("Unknown widget structure action"); return false; }
    Entry->SetStringField(TEXT("Backend"), TEXT("EngineCompatibility")); Entry->SetStringField(TEXT("WidgetName"), Name);
    FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint); return true;
}
