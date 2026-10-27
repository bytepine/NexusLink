// Copyright byteyang. All Rights Reserved.
#include "Utils/NexusEditorToolsets.h"
#include "Utils/NexusCaptureUtils.h"
#include "Utils/NexusVersionCompat.h"
#include "Framework/Application/SlateApplication.h"
#include "Widgets/SWindow.h"
#include "Widgets/SWidget.h"
#include "Widgets/Input/SEditableTextBox.h"
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
	for (const auto& Info : FNexusCaptureUtils::GetSortedTopLevelWindows())
		if (Info.Window.IsValid() && Info.Window->IsVisible() && NexusSlateInTree(Info.Window.ToSharedRef(), Widget)) return Info.Window;
	return nullptr;
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
	if (!FSlateApplication::IsInitialized() || IsRunningCommandlet())
	{
		FNexusEditorOperations::Finish(Operation, TEXT("Failed"), nullptr, TEXT("A visible editor Slate session is required"));
		return Operation;
	}
	const FString Ref = NexusSlateString(Args, TEXT("Ref"));
	double MaxDepthValue = 20; Args->TryGetNumberField(TEXT("MaxDepth"), MaxDepthValue);
	const int32 MaxDepth = FMath::Clamp(static_cast<int32>(MaxDepthValue), 0, 30);
    TSharedPtr<SWidget> Target;
    if (!Ref.IsEmpty()) {
        const auto* Found = GNexusSlateRefs.Find(Ref); if (Found) Target = Found->Pin();
        if (!Target.IsValid() || !NexusSlateWindow(Target.ToSharedRef()).IsValid()) {
            FNexusEditorOperations::Finish(Operation, TEXT("Failed"), nullptr, TEXT("Stale or foreign Ref; query Slate again")); return Operation;
        }
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
			if (Tool != TEXT("PressKey")) Input->SetStringField(TEXT("Ref"), NativeRef);
			if (Action == TEXT("Type")) Input->SetStringField(TEXT("Text"), NexusSlateString(Args, TEXT("Text")));
			if (Action == TEXT("PressKey")) Input->SetStringField(TEXT("Key"), NexusSlateString(Args, TEXT("Key")));
		}
        if (Action == TEXT("Click") || Action == TEXT("Type") || Action == TEXT("PressKey")) {
            if (Action == TEXT("PressKey") && !FKey(*NexusSlateString(Args, TEXT("Key"))).IsValid()) {
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
                    if (Focus->State != TEXT("Completed") || !Window.IsValid() || !NexusSlateInTree(Window.ToSharedRef(), Current.ToSharedRef(), 0, true))
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
		TArray<TSharedPtr<FJsonValue>> Windows;
		for (const auto& Info : FNexusCaptureUtils::GetSortedTopLevelWindows())
		{
			if (!Info.Window.IsValid() || !Info.Window->IsVisible()) continue;
			auto Window = MakeShared<FJsonObject>(); Window->SetStringField(TEXT("Ref"), NexusSlateRef(Info.Window.ToSharedRef()));
			Window->SetStringField(TEXT("Title"), Info.Title); Windows.Add(MakeShared<FJsonValueObject>(Window));
		}
		Result->SetArrayField(TEXT("Windows"), Windows);
	}
	else if (Action == TEXT("Snapshot"))
	{
		TArray<TSharedPtr<FJsonValue>> Nodes; bool bTruncated = false;
		if (Target.IsValid()) NexusSlateSnapshot(Target.ToSharedRef(), 0, MaxDepth, Nodes, bTruncated);
		else for (const auto& Info : FNexusCaptureUtils::GetSortedTopLevelWindows())
			if (Info.Window.IsValid()) NexusSlateSnapshot(Info.Window.ToSharedRef(), 0, MaxDepth, Nodes, bTruncated);
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
			const FString Text = NexusSlateString(Args, TEXT("Text"));
			for (TCHAR Character : Text) FSlateApplication::Get().ProcessKeyCharEvent(FCharacterEvent(Character, FModifierKeysState(), 0, false));
		}
		else if (Action == TEXT("PressKey"))
		{
			const FString KeyName = NexusSlateString(Args, TEXT("Key"));
			const FKey Key(*KeyName);
			if (!Key.IsValid()) { FNexusEditorOperations::Finish(Operation, TEXT("Failed"), nullptr, TEXT("Unknown key; use an Unreal key name")); return Operation; }
			FSlateApplication::Get().SetKeyboardFocus(Target, EFocusCause::SetDirectly);
			const FKeyEvent Event(Key, FModifierKeysState(), 0, false, 0, 0);
			FSlateApplication::Get().ProcessKeyDownEvent(Event); FSlateApplication::Get().ProcessKeyUpEvent(Event);
		}
		Result->SetBoolField(TEXT("bApplied"), true);
	}
	FNexusEditorOperations::Finish(Operation, TEXT("Completed"), Result);
	return Operation;
}
