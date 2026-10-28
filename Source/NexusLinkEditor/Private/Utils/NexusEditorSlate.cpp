// Copyright byteyang. All Rights Reserved.
#include "Utils/NexusEditorToolsets.h"
#include "Utils/NexusCaptureUtils.h"
#include "Utils/NexusVersionCompat.h"
#include "Framework/Application/SlateApplication.h"
#include "Widgets/SWindow.h"
#include "Widgets/SWidget.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Input/SEditableText.h"
#include "Widgets/Text/SMultiLineEditableText.h"
#include "Widgets/Input/SMultiLineEditableTextBox.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Input/SMenuAnchor.h"
#include "Widgets/Text/STextBlock.h"
#include "Input/Events.h"
#include "InputCoreTypes.h"
#include "Layout/WidgetPath.h"
#include "Misc/Guid.h"
#include "Misc/Paths.h"
#include "Dom/JsonValue.h"
#include "Containers/Ticker.h"
#if WITH_NEXUS_SLATE_REFS
#include "SlateInspectorToolsetRefCache.h"
#endif

static TMap<FString, TWeakPtr<SWidget>> GNexusSlateRefs;
static const FString GNexusSlateSession = FGuid::NewGuid().ToString(EGuidFormats::Digits);

static FString NexusSlateRef(const TSharedRef<SWidget>& Widget)
{
	for (auto It = GNexusSlateRefs.CreateIterator(); It; ++It)
	{
		const auto Current = It.Value().Pin();
		if (!Current.IsValid()) { It.RemoveCurrent(); continue; }
		if (Current == Widget) return It.Key();
	}
	if (GNexusSlateRefs.Num() >= 10000) return FString();
	const FString Ref = GNexusSlateSession + (FNexusNativeEditorTools::UseNative(TEXT("SlateInspectorToolset")) ? TEXT(":official:") : TEXT(":engine:")) + FGuid::NewGuid().ToString(EGuidFormats::Digits);
	GNexusSlateRefs.Add(Ref, Widget);
	return Ref;
}

FString FNexusEditorToolsets::BindNativeSnapshot(const FString& Snapshot, bool& bTruncated)
{
#if WITH_NEXUS_SLATE_REFS
    TArray<FString> Lines; Snapshot.ParseIntoArrayLines(Lines, false);
    FString Result; Result.Reserve(Snapshot.Len() * 2);
    for (auto& Line : Lines) {
        const int32 Start = Line.Find(TEXT("[ref="), ESearchCase::CaseSensitive, ESearchDir::FromEnd);
        if (Start != INDEX_NONE && Line.EndsWith(TEXT("]"))) {
            const FString Raw = Line.Mid(Start + 5, Line.Len() - Start - 6);
            const auto Widget = FSlateInspectorToolsetRefCache::Get().ResolveRef(Raw);
            const FString Ref = Widget.IsValid() ? NexusSlateRef(Widget.ToSharedRef()) : FString();
            Line = Line.Left(Start) + (Ref.IsEmpty() ? TEXT("[unavailable-ref]") : TEXT("[ref=") + Ref + TEXT("]"));
            if (Ref.IsEmpty()) bTruncated = true;
        }
        Result += Line + TEXT("\n");
    }
    return Result;
#else
    return Snapshot;
#endif
}

TArray<TSharedPtr<FJsonValue>> FNexusEditorToolsets::WindowRefs()
{
 TArray<TSharedPtr<FJsonValue>> Result;
 TArray<TSharedRef<SWindow>> Windows;
 FSlateApplication::Get().GetAllVisibleWindowsOrdered(Windows);
 for (int32 Index = 0; Index < Windows.Num(); ++Index) {
  auto Entry = MakeShared<FJsonObject>(); Entry->SetStringField(TEXT("Ref"), NexusSlateRef(Windows[Index]));
  Entry->SetStringField(TEXT("Title"), Windows[Index]->GetTitle().ToString()); Entry->SetNumberField(TEXT("Index"), Index);
  Result.Add(MakeShared<FJsonValueObject>(Entry));
 }
 return Result;
}

static bool NexusSlateInTree(const TSharedRef<SWidget>& Root, const TSharedRef<SWidget>& Target, int32 Depth = 0, bool bRequireEnabled = false)
{
	if (bRequireEnabled && !Root->IsEnabled()) return false;
	if (!Root->GetVisibility().IsVisible()) return false;
	if (Root == Target) return true;
	if (Depth > 100) return false;
	FChildren* Children = Root->GetChildren();
	if (Children) for (int32 Index = 0; Index < Children->Num(); ++Index)
		if (NexusSlateInTree(Children->GetChildAt(Index), Target, Depth + 1, bRequireEnabled)) return true;
	return false;
}

static TSharedPtr<SWindow> NexusSlateWindow(const TSharedRef<SWidget>& Widget)
{
	TArray<TSharedRef<SWindow>> Windows; FSlateApplication::Get().GetAllVisibleWindowsOrdered(Windows);
	for (const auto& Window : Windows) if (NexusSlateInTree(Window, Widget)) return Window;
	return nullptr;
}

static TSharedPtr<SWidget> NexusSlateInputTarget(const FString& Ref)
{
	const auto* Found = GNexusSlateRefs.Find(Ref);
	const auto Widget = Found ? Found->Pin() : TSharedPtr<SWidget>();
	const auto Window = Widget.IsValid() ? NexusSlateWindow(Widget.ToSharedRef()) : TSharedPtr<SWindow>();
	return Window.IsValid() && NexusSlateInTree(Window.ToSharedRef(), Widget.ToSharedRef(), 0, true) ? Widget : nullptr;
}

static bool NexusSlateParseKey(const FString& Text, FKey& OutKey, FModifierKeysState& OutModifiers)
{
	TArray<FString> Parts; Text.ParseIntoArray(Parts, TEXT("+"), false);
	if (Parts.Num() == 0) return false;
	bool bCtrl = false, bShift = false, bAlt = false, bCmd = false;
	for (int32 PartIndex = 0; PartIndex + 1 < Parts.Num(); ++PartIndex)
	{
		if (Parts[PartIndex].Equals(TEXT("Ctrl"), ESearchCase::IgnoreCase)) bCtrl = true;
		else if (Parts[PartIndex].Equals(TEXT("Shift"), ESearchCase::IgnoreCase)) bShift = true;
		else if (Parts[PartIndex].Equals(TEXT("Alt"), ESearchCase::IgnoreCase)) bAlt = true;
		else if (Parts[PartIndex].Equals(TEXT("Cmd"), ESearchCase::IgnoreCase)) bCmd = true;
		else return false;
	}
	OutKey = FKey(*Parts.Last());
	OutModifiers = FModifierKeysState(bShift, bShift, bCtrl, bCtrl, bAlt, bAlt, bCmd, bCmd, false);
	return OutKey.IsValid();
}

static bool NexusSlateEditableText(const TSharedRef<SWidget>& Widget, FString& Text, FString& Selected)
{
	const FName Type = Widget->GetType();
	if (Type == FName(TEXT("SEditableTextBox"))) { auto Input = StaticCastSharedRef<SEditableTextBox>(Widget); Text = Input->GetText().ToString(); Selected = Input->GetSelectedText().ToString(); }
	else if (Type == FName(TEXT("SEditableText"))) { auto Input = StaticCastSharedRef<SEditableText>(Widget); Text = Input->GetText().ToString(); Selected = Input->GetSelectedText().ToString(); }
	else if (Type == FName(TEXT("SMultiLineEditableTextBox"))) { auto Input = StaticCastSharedRef<SMultiLineEditableTextBox>(Widget); Text = Input->GetText().ToString(); Selected = Input->GetSelectedText().ToString(); }
	else if (Type == FName(TEXT("SMultiLineEditableText"))) { auto Input = StaticCastSharedRef<SMultiLineEditableText>(Widget); Text = Input->GetText().ToString(); Selected = Input->GetSelectedText().ToString(); }
	else return false;
	return true;
}

static FString NexusSlateSelectAllKey()
{
#if PLATFORM_MAC
	return TEXT("Cmd+A");
#else
	return TEXT("Ctrl+A");
#endif
}

static bool NexusSlateHasFocus(const TSharedRef<SWidget>& Target)
{
	const auto Focused = FSlateApplication::Get().GetKeyboardFocusedWidget();
	return Focused.IsValid() && NexusSlateInTree(Target, Focused.ToSharedRef());
}

static void NexusSlateWaitText(const TSharedRef<SWidget>& Widget, const FString& Text, const FString& Gone,
	int32& Count, bool& bTruncated, bool& bFound, bool& bFoundGone, int32 Depth = 0)
{
	if (!Widget->GetVisibility().IsVisible()) return;
	if (++Count > 2000 || Depth > 100) { bTruncated = true; return; }
	FString Label, Selection;
	if (Widget->GetType() == FName(TEXT("STextBlock"))) Label = StaticCastSharedRef<STextBlock>(Widget)->GetText().ToString();
	else if (!NexusSlateEditableText(Widget, Label, Selection))
	{
#if WITH_ACCESSIBILITY
		Label = Widget->GetAccessibleText(EAccessibleType::Main).ToString();
#endif
	}
	bFound |= !Text.IsEmpty() && Label.Contains(Text); bFoundGone |= !Gone.IsEmpty() && Label.Contains(Gone);
	if ((bFound && Gone.IsEmpty()) || bFoundGone) return;
	if (FChildren* Children = Widget->GetChildren()) for (int32 I = 0; I < Children->Num() && Count <= 2000; ++I)
		NexusSlateWaitText(Children->GetChildAt(I), Text, Gone, Count, bTruncated, bFound, bFoundGone, Depth + 1);
}

static bool NexusSlatePoint(const TSharedRef<SWidget>& Widget, FVector2D& OutPoint)
{
	const auto& Geometry = Widget->GetTickSpaceGeometry();
	if (Geometry.GetLocalSize().X <= 0 || Geometry.GetLocalSize().Y <= 0) return false;
	OutPoint = Geometry.LocalToAbsolute(Geometry.GetLocalSize() * 0.5f);
	TArray<TSharedRef<SWindow>> Windows; FSlateApplication::Get().GetAllVisibleWindowsOrdered(Windows);
	const auto Hit = FSlateApplication::Get().LocateWindowUnderMouse(OutPoint, Windows);
#if NX_UE_HAS_WIDGET_PATH_POINTER_CONTAINS
	return Hit.IsValid() && Hit.ContainsWidget(&Widget.Get());
#else
	return Hit.IsValid() && Hit.ContainsWidget(Widget);
#endif
}

static bool NexusSlateClick(const TSharedRef<SWidget>& Widget)
{
	const auto Window = NexusSlateWindow(Widget);
	FVector2D Point;
	if (!Window.IsValid() || !NexusSlatePoint(Widget, Point)) return false;
	FPointerEvent Move(0, Point, Point, TSet<FKey>(), EKeys::Invalid, 0, FModifierKeysState());
	FSlateApplication::Get().ProcessMouseMoveEvent(Move, true);
	TSet<FKey> Buttons; Buttons.Add(EKeys::LeftMouseButton);
	FPointerEvent Down(0, Point, Point, Buttons, EKeys::LeftMouseButton, 0, FModifierKeysState());
	FSlateApplication::Get().ProcessMouseButtonDownEvent(Window->GetNativeWindow(), Down);
	FPointerEvent Up(0, Point, Point, TSet<FKey>(), EKeys::LeftMouseButton, 0, FModifierKeysState());
	FSlateApplication::Get().ProcessMouseButtonUpEvent(Up);
	return true;
}

static TSharedPtr<SWidget> NexusSlateOption(const TSharedRef<SWidget>& Root, const FString& Value, int32 Depth = 0)
{
	if (Depth > 100 || !Root->IsEnabled() || !Root->GetVisibility().IsVisible()) return nullptr;
	if (Root->GetType() == FName(TEXT("STextBlock")) && StaticCastSharedRef<STextBlock>(Root)->GetText().ToString() == Value) return Root;
	if (FChildren* Children = Root->GetChildren())
		for (int32 ChildIndex = 0; ChildIndex < Children->Num(); ++ChildIndex)
			if (auto Match = NexusSlateOption(Children->GetChildAt(ChildIndex), Value, Depth + 1)) return Match;
	return nullptr;
}

static TSharedPtr<SMenuAnchor> NexusSlateMenuAnchor(const TSharedRef<SWidget>& Widget, int32 Depth = 0)
{
	if (Depth > 100) return nullptr;
	const FString Type = Widget->GetTypeAsString();
	if (Type == TEXT("SComboButton") || Type == TEXT("SMenuAnchor") || Type == TEXT("SComboBox") || Type.StartsWith(TEXT("SComboBox<"))) return StaticCastSharedRef<SMenuAnchor>(Widget);
	if (FChildren* Children = Widget->GetChildren()) for (int32 I = 0; I < Children->Num(); ++I)
		if (auto Anchor = NexusSlateMenuAnchor(Children->GetChildAt(I), Depth + 1)) return Anchor;
	return nullptr;
}

static void NexusNativeSlateClick(const TSharedRef<SWidget>& Widget, const TSharedPtr<FNexusEditorOperation>& Operation)
{
#if WITH_NEXUS_SLATE_REFS
	auto Input = MakeShared<FJsonObject>(); Input->SetStringField(TEXT("Ref"), FSlateInspectorToolsetRefCache::Get().GetOrAssignRef(Widget, TEXT("nexus")));
	FNexusNativeEditorTools::Call(TEXT("SlateInspectorToolset"), TEXT("Click"), Input, Operation);
#else
	FNexusEditorOperations::Finish(Operation, TEXT("Failed"), nullptr, TEXT("Native Slate references were not compiled"));
#endif
}

static void NexusSlateSnapshot(const TSharedRef<SWidget>& Widget, int32 Depth, int32 MaxDepth,
	TArray<TSharedPtr<FJsonValue>>& Nodes, bool& bTruncated)
{
	if (Nodes.Num() >= 2000) { bTruncated = true; return; }
	if (!Widget->GetVisibility().IsVisible()) return;
	auto Node = MakeShared<FJsonObject>();
	const FString Ref = NexusSlateRef(Widget);
    if (Ref.IsEmpty()) { bTruncated = true; return; }
    Node->SetStringField(TEXT("Ref"), Ref);
	Node->SetStringField(TEXT("Type"), Widget->GetTypeAsString());
	FString Text = Widget->GetAccessibleText(EAccessibleType::Main).ToString();
    if (Widget->GetType() == FName(TEXT("SWindow"))) Text = StaticCastSharedRef<SWindow>(Widget)->GetTitle().ToString();
    else if (Widget->GetType() == FName(TEXT("STextBlock"))) Text = StaticCastSharedRef<STextBlock>(Widget)->GetText().ToString();
    else if (Widget->GetType() == FName(TEXT("SEditableTextBox"))) {
        const auto Input = StaticCastSharedRef<SEditableTextBox>(Widget);
        Text = Input->IsPassword() ? TEXT("(password)") : Input->GetText().ToString();
    }
    Node->SetStringField(TEXT("Text"), Text.Left(512));
	Node->SetBoolField(TEXT("bEnabled"), Widget->IsEnabled());
	Node->SetNumberField(TEXT("Depth"), Depth);
	Nodes.Add(MakeShared<FJsonValueObject>(Node));
	FChildren* Children = Widget->GetChildren();
	if (Depth >= MaxDepth) { if (Children && Children->Num()) bTruncated = true; return; }
	if (Children) for (int32 Index = 0; Index < Children->Num(); ++Index)
		NexusSlateSnapshot(Children->GetChildAt(Index), Depth + 1, MaxDepth, Nodes, bTruncated);
}

static FString NexusSlateString(const TSharedPtr<FJsonObject>& Args, const TCHAR* Key)
{
	FString Value; Args->TryGetStringField(Key, Value); return Value;
}

TSharedPtr<FNexusEditorOperation> FNexusEditorToolsets::Slate(const FString& Action, const TSharedPtr<FJsonObject>& Args)
{
	const bool bNative = FNexusNativeEditorTools::UseNative(TEXT("SlateInspectorToolset"));
	auto Operation = FNexusEditorOperations::Create(bNative ? TEXT("OfficialToolset") : TEXT("EngineSlate"));
	if (!Operation.IsValid()) return nullptr;
	if (Action == TEXT("Capture")) { Operation->bIncludeImage = true; Args->TryGetBoolField(TEXT("bIncludeImage"), Operation->bIncludeImage); }
	if (!FSlateApplication::IsInitialized() || IsRunningCommandlet())
	{
		FNexusEditorOperations::Finish(Operation, TEXT("Failed"), nullptr, TEXT("A visible editor Slate session is required"));
		return Operation;
	}
	const FString Ref = NexusSlateString(Args, TEXT("Ref"));
	if (Action == TEXT("WaitFor"))
	{
		const FString Text = NexusSlateString(Args, TEXT("Text")), TextGone = NexusSlateString(Args, TEXT("TextGone"));
		if (Text.IsEmpty() && TextGone.IsEmpty()) { FNexusEditorOperations::Finish(Operation, TEXT("Failed"), nullptr, TEXT("WaitFor requires Text or TextGone")); return Operation; }
		if (!Ref.IsEmpty()) { FNexusEditorOperations::Finish(Operation, TEXT("Failed"), nullptr, TEXT("WaitFor checks all windows; omit Ref")); return Operation; }
		double Timeout = 5; Args->TryGetNumberField(TEXT("TimeoutSeconds"), Timeout);
		const double Deadline = FPlatformTime::Seconds() + FMath::Clamp(Timeout, 0.0, 30.0);
		auto Pending = MakeShared<TSharedPtr<FNexusEditorOperation>>();
		TWeakPtr<FNexusEditorOperation> WeakOperation = Operation;
		Operation->Stop = [WeakOperation]() { auto Current = WeakOperation.Pin(); if (!Current.IsValid()) return false; FNexusEditorOperations::Finish(Current, TEXT("Cancelled"), nullptr); return true; };
		AddTicker(FTickerDelegate::CreateLambda([Operation, Pending, Text, TextGone, Deadline, bNative](float)
		{
			if (!Operation->IsRunning()) return false;
			bool bMatched = false;
			if (bNative)
			{
				if (!Pending->IsValid())
				{
					auto Input = MakeShared<FJsonObject>(); Input->SetStringField(TEXT("Text"), Text); Input->SetStringField(TEXT("TextGone"), TextGone);
					*Pending = FNexusNativeEditorTools::Scratch(); FNexusNativeEditorTools::Call(TEXT("SlateInspectorToolset"), TEXT("WaitFor"), Input, *Pending); return true;
				}
				if ((*Pending)->IsRunning()) return true;
				if ((*Pending)->State != TEXT("Completed")) { FNexusEditorOperations::Finish(Operation, TEXT("Failed"), nullptr, (*Pending)->Message); return false; }
				Pending->Reset();
			}
			// 原生 WaitFor 会遍历隐藏子树；统一按可见文本复核，并且不向 Ref 缓存写入轮询结果。
			int32 Count = 0; bool bTruncated = false, bFound = Text.IsEmpty(), bFoundGone = false;
			TArray<TSharedRef<SWindow>> Windows; FSlateApplication::Get().GetAllVisibleWindowsOrdered(Windows);
			for (const auto& Window : Windows)
			{
				NexusSlateWaitText(Window, Text, TextGone, Count, bTruncated, bFound, bFoundGone);
				if ((bFound && TextGone.IsEmpty()) || bFoundGone || Count > 2000) break;
			}
			bMatched = bFound && (TextGone.IsEmpty() || (!bFoundGone && !bTruncated));
			const bool bTimedOut = FPlatformTime::Seconds() >= Deadline;
			if (!bMatched && !bTimedOut) return true;
			auto Result = MakeShared<FJsonObject>(); Result->SetBoolField(TEXT("bConditionMet"), bMatched); Result->SetBoolField(TEXT("bTimedOut"), !bMatched && bTimedOut);
			FNexusEditorOperations::Finish(Operation, TEXT("Completed"), Result); return false;
		}), 0.1f);
		return Operation;
	}
	double MaxDepthValue = 20; Args->TryGetNumberField(TEXT("MaxDepth"), MaxDepthValue);
	const int32 MaxDepth = FMath::Clamp(static_cast<int32>(MaxDepthValue), 0, 30);
    TSharedPtr<SWidget> Target;
    if (!Ref.IsEmpty()) {
        const auto* Found = GNexusSlateRefs.Find(Ref); if (Found) Target = Found->Pin();
        if (!Target.IsValid() || !NexusSlateWindow(Target.ToSharedRef()).IsValid()) {
            FNexusEditorOperations::Finish(Operation, TEXT("Failed"), nullptr, TEXT("Stale or foreign Ref; query Slate again")); return Operation;
        }
    }
	if (Action == TEXT("FillForm"))
	{
		const TArray<TSharedPtr<FJsonValue>>* Fields = nullptr;
		if (!Target.IsValid() || !Args->TryGetArrayField(TEXT("Fields"), Fields) || Fields->Num() == 0 || Fields->Num() > 100)
		{ FNexusEditorOperations::Finish(Operation, TEXT("Failed"), nullptr, TEXT("FillForm requires a live root Ref and 1-100 Fields")); return Operation; }
		for (const auto& Field : *Fields)
		{
			const auto FieldArgs = Field->AsObject(); const auto Widget = NexusSlateInputTarget(NexusSlateString(FieldArgs, TEXT("Ref")));
			const FString Type = NexusSlateString(FieldArgs, TEXT("FieldType")), Value = NexusSlateString(FieldArgs, TEXT("Value"));
			const bool bValidType = Type == TEXT("textbox") || Type == TEXT("combobox") || (Type == TEXT("checkbox") && Widget.IsValid() && Widget->GetType() == FName(TEXT("SCheckBox")) && (Value == TEXT("true") || Value == TEXT("false")));
			if (!Widget.IsValid() || !bValidType || !NexusSlateInTree(Target.ToSharedRef(), Widget.ToSharedRef(), 0, true))
			{ FNexusEditorOperations::Finish(Operation, TEXT("Failed"), nullptr, TEXT("All form fields must be live, enabled descendants with valid type/value")); return Operation; }
		}
		auto Steps = *Fields; auto Index = MakeShared<int32>(0); auto Pending = MakeShared<TSharedPtr<FNexusEditorOperation>>(); TWeakPtr<SWidget> Root = Target;
		AddTicker(FTickerDelegate::CreateLambda([Operation, Steps, Index, Pending, Root](float)
		{
			if (!Operation->IsRunning()) return false;
			if (Pending->IsValid())
			{
				if ((*Pending)->IsRunning()) return true;
				if ((*Pending)->State != TEXT("Completed"))
				{
					auto Result = MakeShared<FJsonObject>(); Result->SetNumberField(TEXT("AppliedCount"), *Index); Result->SetNumberField(TEXT("FailedFieldIndex"), *Index);
					FNexusEditorOperations::Finish(Operation, TEXT("Failed"), Result, (*Pending)->Message); return false;
				}
				++*Index; Pending->Reset();
			}
			if (*Index == Steps.Num()) { auto Result = MakeShared<FJsonObject>(); Result->SetNumberField(TEXT("AppliedCount"), *Index); Result->SetBoolField(TEXT("bApplied"), true); FNexusEditorOperations::Finish(Operation, TEXT("Completed"), Result); return false; }
			const auto Field = Steps[*Index]->AsObject(); const FString FieldRef = NexusSlateString(Field, TEXT("Ref"));
			const auto Widget = NexusSlateInputTarget(FieldRef); const auto CurrentRoot = Root.Pin();
			if (!Widget.IsValid() || !CurrentRoot.IsValid() || !NexusSlateInTree(CurrentRoot.ToSharedRef(), Widget.ToSharedRef(), 0, true))
			{ auto Result = MakeShared<FJsonObject>(); Result->SetNumberField(TEXT("AppliedCount"), *Index); Result->SetNumberField(TEXT("FailedFieldIndex"), *Index); FNexusEditorOperations::Finish(Operation, TEXT("Failed"), Result, TEXT("Form field expired after earlier edits; no retry")); return false; }
			auto Input = MakeShared<FJsonObject>(); Input->SetStringField(TEXT("Ref"), FieldRef); Input->SetStringField(TEXT("Value"), NexusSlateString(Field, TEXT("Value"))); Input->SetStringField(TEXT("Text"), NexusSlateString(Field, TEXT("Value"))); Input->SetBoolField(TEXT("bReplace"), true);
			const FString Type = NexusSlateString(Field, TEXT("FieldType"));
			TGuardValue<FString> Owner(FNexusEditorOperations::RequestOwner(), Operation->Owner);
			*Pending = FNexusEditorToolsets::Slate(Type == TEXT("textbox") ? TEXT("Type") : Type == TEXT("checkbox") ? TEXT("SetChecked") : TEXT("SelectOption"), Input);
			return true;
		}), 0.05f);
		return Operation;
	}
	if (Action == TEXT("SelectOption"))
	{
		Target = NexusSlateInputTarget(Ref);
		const auto Anchor = Target.IsValid() && Target->GetTypeAsString().Contains(TEXT("Combo")) ? NexusSlateMenuAnchor(Target.ToSharedRef()) : nullptr;
		const bool bNativeAllowed = !bNative || FNexusNativeEditorTools::CanCall(TEXT("SlateInspectorToolset"), TEXT("SelectOption"));
		if (!Anchor.IsValid() || !bNativeAllowed)
		{ FNexusEditorOperations::Finish(Operation, TEXT("Failed"), nullptr, FString::Printf(TEXT("SelectOption requires an enabled combo Ref and native tool (Type=%s, MenuAnchor=%d, ToolEnabled=%d)"), Target.IsValid() ? *Target->GetTypeAsString() : TEXT("expired"), Anchor.IsValid(), bNativeAllowed)); return Operation; }
		TSharedPtr<FNexusEditorOperation> Opened;
		if (!Anchor->IsOpen())
		{
			if (bNative) { Opened = FNexusNativeEditorTools::Scratch(); NexusNativeSlateClick(Target.ToSharedRef(), Opened); }
			else if (!NexusSlateClick(Target.ToSharedRef())) { FNexusEditorOperations::Finish(Operation, TEXT("Failed"), nullptr, TEXT("Combo is not hit-testable")); return Operation; }
		}
		TWeakPtr<SMenuAnchor> WeakAnchor = Anchor;
		const FString Value = NexusSlateString(Args, TEXT("Value")); const double Deadline = FPlatformTime::Seconds() + 2;
		AddTicker(FTickerDelegate::CreateLambda([Operation, Opened, WeakAnchor, Ref, Value, Deadline, bNative](float)
		{
			if (!Operation->IsRunning()) return false;
			if (Opened.IsValid() && Opened->IsRunning()) return true;
			const auto Current = NexusSlateInputTarget(Ref); const auto Menu = WeakAnchor.Pin();
			if ((Opened.IsValid() && Opened->State != TEXT("Completed")) || !Current.IsValid() || !Menu.IsValid() || !NexusSlateInTree(Current.ToSharedRef(), Menu.ToSharedRef()) || (bNative && !FNexusNativeEditorTools::CanCall(TEXT("SlateInspectorToolset"), TEXT("SelectOption"))))
			{ FNexusEditorOperations::Finish(Operation, TEXT("Failed"), nullptr, TEXT("Combo expired, failed to open or its native tool was disabled")); return false; }
			const auto Popup = Menu->GetMenuWindow();
			if (Popup.IsValid() && Popup == NexusSlateWindow(Current.ToSharedRef())) { FNexusEditorOperations::Finish(Operation, TEXT("Failed"), nullptr, TEXT("SelectOption requires a separate popup window; use focused key navigation for hosted menus")); return false; }
			const auto Option = Menu->IsOpen() && Popup.IsValid() ? NexusSlateOption(Popup.ToSharedRef(), Value) : nullptr;
			if (Option.IsValid())
			{
				FVector2D Point;
				if (!NexusSlatePoint(Option.ToSharedRef(), Point)) { if (FPlatformTime::Seconds() < Deadline) return true; FNexusEditorOperations::Finish(Operation, TEXT("Failed"), nullptr, TEXT("Dropdown option is not hit-testable")); return false; }
				if (bNative) NexusNativeSlateClick(Option.ToSharedRef(), Operation);
				else { const bool bClicked = NexusSlateClick(Option.ToSharedRef()); auto Result = MakeShared<FJsonObject>(); Result->SetBoolField(TEXT("bApplied"), bClicked); FNexusEditorOperations::Finish(Operation, bClicked ? TEXT("Completed") : TEXT("Failed"), Result); }
				return false;
			}
			if (FPlatformTime::Seconds() < Deadline) return true;
			FNexusEditorOperations::Finish(Operation, TEXT("Failed"), nullptr, TEXT("Exact visible option was not found in this combo's popup")); return false;
		}), 0.05f);
		return Operation;
	}
	TSharedPtr<SWidget> EndTarget;
	if (Action == TEXT("Drag"))
	{
		EndTarget = NexusSlateInputTarget(NexusSlateString(Args, TEXT("EndRef")));
		if (!EndTarget.IsValid()) { FNexusEditorOperations::Finish(Operation, TEXT("Failed"), nullptr, TEXT("Drag requires a live enabled EndRef")); return Operation; }
	}
	if (bNative)
	{
        FString NativeRef;
#if WITH_NEXUS_SLATE_REFS
        if (Target.IsValid() && Action != TEXT("Focus")) {
            // Public cache manages identifiers only; every tool action still goes through Registry.
            NativeRef = FSlateInspectorToolsetRefCache::Get().GetOrAssignRef(Target.ToSharedRef(), TEXT("nexus"));
            if (NativeRef.IsEmpty() || FSlateInspectorToolsetRefCache::Get().ResolveRef(NativeRef) != Target) {
                FNexusEditorOperations::Finish(Operation, TEXT("Failed"), nullptr, TEXT("Native Ref expired; query Snapshot again")); return Operation;
            }
        }
#endif
        if (Target.IsValid() && Action != TEXT("Capture") && Action != TEXT("Snapshot") && Action != TEXT("Windows")
            && !NexusSlateInTree(NexusSlateWindow(Target.ToSharedRef()).ToSharedRef(), Target.ToSharedRef(), 0, true)) {
            FNexusEditorOperations::Finish(Operation, TEXT("Failed"), nullptr, TEXT("Target has a hidden or disabled ancestor")); return Operation;
        }
		auto Input = MakeShared<FJsonObject>();
		FString Tool = Action;
		if (Action == TEXT("Windows")) Input->SetStringField(TEXT("Action"), TEXT("list"));
		else if (Action == TEXT("Focus"))
		{
            const auto* Entry = GNexusSlateRefs.Find(Ref);
            const auto TargetWindow = Entry ? Entry->Pin() : TSharedPtr<SWidget>();
            TArray<TSharedRef<SWindow>> Windows; FSlateApplication::Get().GetAllVisibleWindowsOrdered(Windows);
            const int32 Index = Windows.IndexOfByPredicate([TargetWindow](const TSharedRef<SWindow>& Window) { return Window == TargetWindow; });
            if (Index == INDEX_NONE) { FNexusEditorOperations::Finish(Operation, TEXT("Failed"), nullptr, TEXT("Use a live window Ref from Windows query")); return Operation; }
            Input->SetStringField(TEXT("Action"), TEXT("select")); Input->SetNumberField(TEXT("Index"), Index);
            FNexusNativeEditorTools::Call(TEXT("SlateInspectorToolset"), TEXT("Windows"), Input, Operation);
            return Operation;
		}
		else if (Action == TEXT("Snapshot"))
        {
            Input->SetStringField(TEXT("Ref"), NativeRef); Input->SetNumberField(TEXT("MaxDepth"), MaxDepth);
            // Force a fresh render. Reusing an observer could return stale data and
            // unobserving its shared identifier could remove another client's observer.
            Input->SetBoolField(TEXT("bIncludeSourceLocations"), true);
        }
		else
		{
			if (Action == TEXT("Capture")) Tool = TEXT("Screenshot");
			if (Action == TEXT("SetChecked")) Tool = TEXT("FillForm");
			if (Tool != TEXT("PressKey") && Tool != TEXT("FillForm") && Tool != TEXT("Drag")) Input->SetStringField(TEXT("Ref"), NativeRef);
			if (Action == TEXT("Type")) Input->SetStringField(TEXT("Text"), NexusSlateString(Args, TEXT("Text")));
			if (Action == TEXT("PressKey")) Input->SetStringField(TEXT("Key"), NexusSlateString(Args, TEXT("Key")));
			if (Action == TEXT("SetChecked"))
			{
				const FString Value = NexusSlateString(Args, TEXT("Value"));
				if (!Target.IsValid() || Target->GetType() != FName(TEXT("SCheckBox")) || (Value != TEXT("true") && Value != TEXT("false")))
				{ FNexusEditorOperations::Finish(Operation, TEXT("Failed"), nullptr, TEXT("SetChecked requires SCheckBox and true/false")); return Operation; }
				auto Field = MakeShared<FJsonObject>(); Field->SetStringField(TEXT("ref"), NativeRef); Field->SetStringField(TEXT("value"), Value); Field->SetStringField(TEXT("fieldType"), TEXT("checkbox"));
				Input->SetArrayField(TEXT("Fields"), {MakeShared<FJsonValueObject>(Field)});
			}
			if (Action == TEXT("Drag"))
			{
#if WITH_NEXUS_SLATE_REFS
				const FString EndRef = FSlateInspectorToolsetRefCache::Get().GetOrAssignRef(EndTarget.ToSharedRef(), TEXT("nexus"));
				Input->SetStringField(TEXT("StartRef"), NativeRef); Input->SetStringField(TEXT("EndRef"), EndRef);
#endif
			}
		}
		bool bReplace = false; Args->TryGetBoolField(TEXT("bReplace"), bReplace);
		if (Action == TEXT("Type") && bReplace)
		{
			FString Text, Selection;
			if (!NexusSlateEditableText(Target.ToSharedRef(), Text, Selection) || !FNexusNativeEditorTools::CanCall(TEXT("SlateInspectorToolset"), TEXT("Type")))
			{ FNexusEditorOperations::Finish(Operation, TEXT("Failed"), nullptr, TEXT("Replacement requires an editable text widget and enabled Type tool")); return Operation; }
			auto SelectArgs = MakeShared<FJsonObject>(); SelectArgs->SetStringField(TEXT("Ref"), Ref); SelectArgs->SetStringField(TEXT("Key"), NexusSlateSelectAllKey());
			auto Pending = MakeShared<TSharedPtr<FNexusEditorOperation>>(FNexusEditorToolsets::Slate(TEXT("PressKey"), SelectArgs));
			auto bClearing = MakeShared<bool>(false); TWeakPtr<SWidget> WeakTarget = Target;
			AddTicker(FTickerDelegate::CreateLambda([Pending, bClearing, Operation, Input, WeakTarget](float)
			{
				if (!Operation->IsRunning()) return false;
				if ((*Pending)->IsRunning()) return true;
				const auto Current = WeakTarget.Pin();
				const auto Window = Current.IsValid() ? NexusSlateWindow(Current.ToSharedRef()) : TSharedPtr<SWindow>();
				FString Text, Selection;
				if ((*Pending)->State != TEXT("Completed") || !Window.IsValid() || !NexusSlateInTree(Window.ToSharedRef(), Current.ToSharedRef(), 0, true) || !NexusSlateHasFocus(Current.ToSharedRef()) || !NexusSlateEditableText(Current.ToSharedRef(), Text, Selection))
				{ FNexusEditorOperations::Finish(Operation, TEXT("Failed"), nullptr, TEXT("Text target expired, lost focus or could not select its contents")); return false; }
				if (!*bClearing)
				{
					if (Selection != Text) { FNexusEditorOperations::Finish(Operation, TEXT("Failed"), nullptr, TEXT("Could not select all text; replacement stopped before deleting")); return false; }
					// 原生 Type 再聚焦会丢失选区，先通过 Registry 清除已验证的完整选区。
					auto Delete = MakeShared<FJsonObject>(); Delete->SetStringField(TEXT("Key"), TEXT("BackSpace"));
					*Pending = FNexusNativeEditorTools::Scratch(); *bClearing = true;
					FNexusNativeEditorTools::Call(TEXT("SlateInspectorToolset"), TEXT("PressKey"), Delete, *Pending); return true;
				}
				if (!Text.IsEmpty()) FNexusEditorOperations::Finish(Operation, TEXT("Failed"), nullptr, TEXT("Target changed or could not clear text; replacement stopped"));
				else FNexusNativeEditorTools::Call(TEXT("SlateInspectorToolset"), TEXT("Type"), Input, Operation);
				return false;
			}), 0.05f);
			return Operation;
		}
        if (Action == TEXT("Click") || Action == TEXT("Type") || Action == TEXT("PressKey")) {
			FKey ParsedKey; FModifierKeysState ParsedModifiers;
            if (Action == TEXT("PressKey") && !NexusSlateParseKey(NexusSlateString(Args, TEXT("Key")), ParsedKey, ParsedModifiers)) {
                FNexusEditorOperations::Finish(Operation, TEXT("Failed"), nullptr, TEXT("Unknown Unreal key name")); return Operation;
            }
            if (Action == TEXT("PressKey")) {
                auto Focus = FNexusNativeEditorTools::Scratch();
                auto FocusArgs = MakeShared<FJsonObject>(); FocusArgs->SetStringField(TEXT("Ref"), NativeRef); FocusArgs->SetStringField(TEXT("Text"), TEXT(""));
                FNexusNativeEditorTools::Call(TEXT("SlateInspectorToolset"), TEXT("Type"), FocusArgs, Focus);
                TWeakPtr<SWidget> WeakTarget = Target;
                FNexusEditorToolsets::AddTicker(FTickerDelegate::CreateLambda([Focus, Operation, Input, WeakTarget](float) {
                    if (Focus->IsRunning()) return true;
                    const auto Current = WeakTarget.Pin(); const auto Window = Current.IsValid() ? NexusSlateWindow(Current.ToSharedRef()) : TSharedPtr<SWindow>();
                    if (Focus->State != TEXT("Completed") || !Window.IsValid() || !NexusSlateInTree(Window.ToSharedRef(), Current.ToSharedRef(), 0, true) || !NexusSlateHasFocus(Current.ToSharedRef()))
                        FNexusEditorOperations::Finish(Operation, TEXT("Failed"), nullptr, TEXT("Target expired or could not be focused"));
                    else FNexusNativeEditorTools::Call(TEXT("SlateInspectorToolset"), TEXT("PressKey"), Input, Operation);
                    return false;
                }), 0.05f);
            } else FNexusNativeEditorTools::Call(TEXT("SlateInspectorToolset"), Tool, Input, Operation);
        } else FNexusNativeEditorTools::Call(TEXT("SlateInspectorToolset"), Tool, Input, Operation);
		return Operation;
	}
	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	if (Action == TEXT("Windows"))
	{
		Result->SetArrayField(TEXT("Windows"), WindowRefs());
	}
	else if (Action == TEXT("Snapshot"))
	{
		TArray<TSharedPtr<FJsonValue>> Nodes; bool bTruncated = false;
		if (Target.IsValid()) NexusSlateSnapshot(Target.ToSharedRef(), 0, MaxDepth, Nodes, bTruncated);
		else { TArray<TSharedRef<SWindow>> Windows; FSlateApplication::Get().GetAllVisibleWindowsOrdered(Windows); for (const auto& Window : Windows) NexusSlateSnapshot(Window, 0, MaxDepth, Nodes, bTruncated); }
		Result->SetArrayField(TEXT("Nodes"), Nodes); Result->SetBoolField(TEXT("bTruncated"), bTruncated);
	}
	else if (Action == TEXT("Capture"))
	{
		if (!Target.IsValid()) Target = FSlateApplication::Get().GetActiveTopLevelWindow();
		TArray<FColor> Pixels; int32 Width = 0, Height = 0; TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>(); FString Error;
		if (!Target.IsValid() || !FNexusCaptureUtils::CaptureWidgetPixels(Target.ToSharedRef(), Pixels, Width, Height)
			|| !FNexusCaptureUtils::SaveAndBuildEntry(Pixels, Width, Height, 1920, TEXT("png"), TEXT("slate"), Result, Entry, Error))
		{ FNexusEditorOperations::Finish(Operation, TEXT("Failed"), nullptr, Error.IsEmpty() ? TEXT("Cannot capture target widget") : Error); return Operation; }
        Entry->SetStringField(TEXT("FilePath"), FPaths::ConvertRelativePathToFull(Entry->GetStringField(TEXT("FilePath"))));
		if (Operation->bIncludeImage) Operation->ImageFiles.Add(Entry->GetStringField(TEXT("FilePath")));
		Result = Entry;
	}
	else
	{
		if (!Target.IsValid() || !Target->IsEnabled() || !Target->GetVisibility().IsVisible())
		{ FNexusEditorOperations::Finish(Operation, TEXT("Failed"), nullptr, TEXT("A visible enabled Ref is required")); return Operation; }
		auto Window = NexusSlateWindow(Target.ToSharedRef());
        if (!NexusSlateInTree(Window.ToSharedRef(), Target.ToSharedRef(), 0, true)) {
            FNexusEditorOperations::Finish(Operation, TEXT("Failed"), nullptr, TEXT("Target has a hidden or disabled ancestor")); return Operation;
        }
		Window->BringToFront(true);
		if (Action == TEXT("Focus")) FSlateApplication::Get().SetKeyboardFocus(Target, EFocusCause::SetDirectly);
		else if (Action == TEXT("Click"))
		{
			const auto& Geometry = Target->GetTickSpaceGeometry();
			const FVector2D Position = Geometry.LocalToAbsolute(Geometry.GetLocalSize() * 0.5f);
            if (Geometry.GetLocalSize().X <= 0 || Geometry.GetLocalSize().Y <= 0) {
                FNexusEditorOperations::Finish(Operation, TEXT("Failed"), nullptr, TEXT("Target has no laid out geometry")); return Operation;
            }
            TArray<TSharedRef<SWindow>> Windows; FSlateApplication::Get().GetAllVisibleWindowsOrdered(Windows);
            const auto Hit = FSlateApplication::Get().LocateWindowUnderMouse(Position, Windows);
#if NX_UE_HAS_WIDGET_PATH_POINTER_CONTAINS
            const bool bHitTarget = Hit.ContainsWidget(Target.Get());
#else
            const bool bHitTarget = Hit.ContainsWidget(Target.ToSharedRef());
#endif
            if (!Hit.IsValid() || !bHitTarget) {
                FNexusEditorOperations::Finish(Operation, TEXT("Failed"), nullptr, TEXT("Target is obscured or not hit-testable")); return Operation;
            }
			TSet<FKey> Buttons; Buttons.Add(EKeys::LeftMouseButton);
			FModifierKeysState Modifiers;
			FPointerEvent Move(0, Position, Position, TSet<FKey>(), EKeys::Invalid, 0, Modifiers);
			FSlateApplication::Get().ProcessMouseMoveEvent(Move, true);
			FPointerEvent Down(0, Position, Position, Buttons, EKeys::LeftMouseButton, 0, Modifiers);
			FSlateApplication::Get().ProcessMouseButtonDownEvent(Window->GetNativeWindow(), Down);
			FPointerEvent Up(0, Position, Position, TSet<FKey>(), EKeys::LeftMouseButton, 0, Modifiers);
			FSlateApplication::Get().ProcessMouseButtonUpEvent(Up);
		}
		else if (Action == TEXT("Type"))
		{
			FSlateApplication::Get().SetKeyboardFocus(Target, EFocusCause::SetDirectly);
			bool bReplace = false; Args->TryGetBoolField(TEXT("bReplace"), bReplace);
			if (bReplace)
			{
				FString Text, Selection;
				if (!NexusSlateEditableText(Target.ToSharedRef(), Text, Selection) || !NexusSlateHasFocus(Target.ToSharedRef())) { FNexusEditorOperations::Finish(Operation, TEXT("Failed"), nullptr, TEXT("Replacement requires focused editable text")); return Operation; }
				FKey Key; FModifierKeysState Modifiers; NexusSlateParseKey(NexusSlateSelectAllKey(), Key, Modifiers);
				FKeyEvent Event(Key, Modifiers, 0, false, 0, 0); FSlateApplication::Get().ProcessKeyDownEvent(Event); FSlateApplication::Get().ProcessKeyUpEvent(Event);
				NexusSlateEditableText(Target.ToSharedRef(), Text, Selection);
				if (Text != Selection) { FNexusEditorOperations::Finish(Operation, TEXT("Failed"), nullptr, TEXT("Could not select all text")); return Operation; }
				const FKeyEvent Delete(EKeys::BackSpace, FModifierKeysState(), 0, false, 0, 0);
				FSlateApplication::Get().ProcessKeyDownEvent(Delete); FSlateApplication::Get().ProcessKeyUpEvent(Delete);
				FSlateApplication::Get().ProcessKeyCharEvent(FCharacterEvent(TEXT('\b'), FModifierKeysState(), 0, false));
				NexusSlateEditableText(Target.ToSharedRef(), Text, Selection);
				if (!Text.IsEmpty()) { FNexusEditorOperations::Finish(Operation, TEXT("Failed"), nullptr, TEXT("Could not clear text")); return Operation; }
			}
			const FString Text = NexusSlateString(Args, TEXT("Text"));
			for (TCHAR Character : Text) FSlateApplication::Get().ProcessKeyCharEvent(FCharacterEvent(Character, FModifierKeysState(), 0, false));
		}
		else if (Action == TEXT("PressKey"))
		{
			const FString KeyName = NexusSlateString(Args, TEXT("Key"));
			FKey Key; FModifierKeysState Modifiers;
			if (!NexusSlateParseKey(KeyName, Key, Modifiers)) { FNexusEditorOperations::Finish(Operation, TEXT("Failed"), nullptr, TEXT("Unknown key or modifier")); return Operation; }
			FSlateApplication::Get().SetKeyboardFocus(Target, EFocusCause::SetDirectly);
			const FKeyEvent Event(Key, Modifiers, 0, false, 0, 0);
			FSlateApplication::Get().ProcessKeyDownEvent(Event); FSlateApplication::Get().ProcessKeyUpEvent(Event);
		}
		else if (Action == TEXT("Hover") || Action == TEXT("Drag"))
		{
			FVector2D Start, End;
			if (!NexusSlatePoint(Target.ToSharedRef(), Start) || (Action == TEXT("Drag") && !NexusSlatePoint(EndTarget.ToSharedRef(), End)))
			{ FNexusEditorOperations::Finish(Operation, TEXT("Failed"), nullptr, TEXT("Target is obscured, unlaid out or not hit-testable")); return Operation; }
			FSlateApplication::Get().SetCursorPos(Start);
			FPointerEvent Move(0, Start, Start, TSet<FKey>(), EKeys::Invalid, 0, FModifierKeysState()); FSlateApplication::Get().ProcessMouseMoveEvent(Move, true);
			if (Action == TEXT("Drag"))
			{
				TSet<FKey> Buttons; Buttons.Add(EKeys::LeftMouseButton);
				FPointerEvent Down(0, Start, Start, Buttons, EKeys::LeftMouseButton, 0, FModifierKeysState()); FSlateApplication::Get().ProcessMouseButtonDownEvent(Window->GetNativeWindow(), Down);
				FVector2D Previous = Start;
				for (int32 Step = 1; Step <= 12; ++Step)
				{
					const FVector2D Position = FMath::Lerp(Start, End, static_cast<float>(Step) / 12.0f);
					FSlateApplication::Get().SetCursorPos(Position);
					FPointerEvent Drag(0, Position, Previous, Buttons, EKeys::Invalid, 0, FModifierKeysState()); FSlateApplication::Get().ProcessMouseMoveEvent(Drag, true); Previous = Position;
				}
				FPointerEvent Up(0, End, End, TSet<FKey>(), EKeys::LeftMouseButton, 0, FModifierKeysState()); FSlateApplication::Get().ProcessMouseButtonUpEvent(Up);
			}
		}
		else if (Action == TEXT("SetChecked"))
		{
			const FString Value = NexusSlateString(Args, TEXT("Value"));
			if (Target->GetType() != FName(TEXT("SCheckBox")) || (Value != TEXT("true") && Value != TEXT("false")))
			{ FNexusEditorOperations::Finish(Operation, TEXT("Failed"), nullptr, TEXT("SetChecked requires SCheckBox and true/false")); return Operation; }
			if (StaticCastSharedPtr<SCheckBox>(Target)->IsChecked() != (Value == TEXT("true")) && !NexusSlateClick(Target.ToSharedRef()))
			{ FNexusEditorOperations::Finish(Operation, TEXT("Failed"), nullptr, TEXT("Checkbox is not hit-testable")); return Operation; }
		}
		Result->SetBoolField(TEXT("bApplied"), true);
	}
	FNexusEditorOperations::Finish(Operation, TEXT("Completed"), Result);
	return Operation;
}
