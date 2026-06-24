// Copyright byteyang. All Rights Reserved.

#include "Capabilities/Runtime/Widget/NexusGetRuntimeSlateWidgetCapability.h"
#include "Utils/NexusWidgetLayoutUtils.h"
#include "Utils/NexusCapabilityResultBuilder.h"
#include "NexusCapabilityRegistry.h"
#include "NexusMcpSchemaBuilder.h"
#include "Utils/NexusVersionCompat.h"
#include "Framework/Application/SlateApplication.h"
#include "Widgets/SWidget.h"
#include "Widgets/SWindow.h"
#include "Components/Widget.h"
#include "Blueprint/UserWidget.h"
#include "Blueprint/WidgetTree.h"
#include "UObject/UObjectIterator.h"
#include "NexusMcpTool.h"

static FString WidgetTypeToString(FName Type)           { return Type.ToString(); }

static TSharedPtr<SWidget> FindSlateWidgetByAddress(UPTRINT TargetAddress)
{
	if (!FSlateApplication::IsInitialized()) return nullptr;
	const TArray<TSharedRef<SWindow>>& TopWindows = FSlateApplication::Get().GetTopLevelWindows();
	TArray<TSharedRef<SWidget>> Stack;
	Stack.Reserve(256);
	for (const TSharedRef<SWindow>& Window : TopWindows) Stack.Add(Window);
	while (Stack.Num() > 0)
	{
#if NX_UE_HAS_ALLOW_SHRINKING_ENUM
		TSharedRef<SWidget> Current = Stack.Pop(EAllowShrinking::No);
#else
		TSharedRef<SWidget> Current = Stack.Pop(false);
#endif
		if (reinterpret_cast<UPTRINT>(&Current.Get()) == TargetAddress) return Current;
		FChildren* Children = Current->GetChildren();
		for (int32 i = 0; i < Children->Num(); ++i) Stack.Add(Children->GetChildAt(i));
	}
	return nullptr;
}

static FString VisibilityToString(EVisibility Vis)
{
	if (Vis == EVisibility::Collapsed)            return TEXT("Collapsed");
	if (Vis == EVisibility::Hidden)               return TEXT("Hidden");
	if (Vis == EVisibility::HitTestInvisible)     return TEXT("HitTestInvisible");
	if (Vis == EVisibility::SelfHitTestInvisible) return TEXT("SelfHitTestInvisible");
	return TEXT("Visible");
}

void FGetRuntimeSlateWidgetCapability::BuildDefinition(FNexusCapabilityDefinition& Out) const
{
	Out.Name = TEXT("get_runtime_slate_widget");
	Out.Description = TEXT("按十六进制地址检查原生 SWidget（来自 Widget Reflector）；含 layout（AutoWrapText、锚点等）。");
	Out.InputSchema = FNexusSchema::Object()
		.Prop(TEXT("address"), FNexusSchema::Str(TEXT("Widget Reflector 提供的十六进制地址")))
		.Required({ TEXT("address") })
		.Build();
	Out.Tags = {FNexusMcpTags::Readonly, FNexusMcpTags::Widget };
	Out.ExtraSearchKeywords = { TEXT("reflector"), TEXT("native"), TEXT("swidget"), TEXT("address"), TEXT("layout") };
	Out.RelatedCapabilities = { TEXT("list_runtime_widgets") };
	Out.Prerequisites = { TEXT("pie") };
	Out.WhenToUse = TEXT("持有 Widget Reflector 十六进制地址时用");
}

FCapabilityResult FGetRuntimeSlateWidgetCapability::Execute(const TSharedPtr<FJsonObject>& Arguments) const
{

	return FNexusCapabilityResultBuilder::Build([&](auto& OutEntries, auto& OutTop, auto& OutError)
	{

		TSharedPtr<FJsonObject> OutEntry = MakeShared<FJsonObject>();

		if (!Arguments.IsValid() || !Arguments->HasField(TEXT("address")))
		{
			OutError = TEXT("缺少 address");
			return;
		}

		FString AddressStr = Arguments->GetStringField(TEXT("address")).TrimStartAndEnd();
		if (AddressStr.StartsWith(TEXT("0x")) || AddressStr.StartsWith(TEXT("0X")))
			AddressStr = AddressStr.Mid(2);
		UPTRINT CachedAddress = static_cast<UPTRINT>(FCString::Strtoui64(*AddressStr, nullptr, 16));
		if (CachedAddress == 0)
		{
			OutError = TEXT("无效的十六进制地址");
			return;
		}

		TSharedPtr<SWidget> Widget = FindSlateWidgetByAddress(CachedAddress);
		if (!Widget.IsValid())
		{
			OutEntry->SetStringField(TEXT("error"), FString::Printf(
				TEXT("No Slate widget matches address 0x%llX in visible window trees"),
				static_cast<uint64>(CachedAddress)));
			OutEntries.Add(MakeShared<FJsonValueObject>(OutEntry));
			return;
		}

		OutEntry->SetStringField(TEXT("address"), FString::Printf(TEXT("0x%llX"), static_cast<uint64>(CachedAddress)));
		OutEntry->SetStringField(TEXT("type"), WidgetTypeToString(Widget->GetType()));

		const FString Location = Widget->GetReadableLocation();
		if (!Location.IsEmpty()) OutEntry->SetStringField(TEXT("readableLocation"), Location);

		const EVisibility Vis = Widget->GetVisibility();
		if (Vis != EVisibility::Visible) OutEntry->SetStringField(TEXT("visibility"), VisibilityToString(Vis));
		if (!Widget->IsEnabled()) OutEntry->SetBoolField(TEXT("isEnabled"), false);

		const FVector2D DesiredSize = Widget->GetDesiredSize();
		if (DesiredSize.X > 0.f || DesiredSize.Y > 0.f)
			OutEntry->SetStringField(TEXT("desiredSize"),
				FString::Printf(TEXT("%.1f x %.1f"), DesiredSize.X, DesiredSize.Y));

		FChildren* Children = Widget->GetChildren();
		const int32 NumChildren = Children ? Children->Num() : 0;
		if (NumChildren > 0)
		{
			OutEntry->SetNumberField(TEXT("numChildren"), NumChildren);
			TArray<TSharedPtr<FJsonValue>> ChildArr;
			const int32 Cap = FMath::Min(NumChildren, 10);
			for (int32 i = 0; i < Cap; ++i)
			{
				TSharedRef<SWidget> Child = Children->GetChildAt(i);
				TSharedPtr<FJsonObject> ChildObj = MakeShared<FJsonObject>();
				ChildObj->SetStringField(TEXT("type"), WidgetTypeToString(Child->GetType()));
				ChildObj->SetStringField(TEXT("address"), FString::Printf(TEXT("0x%llX"),
					static_cast<uint64>(reinterpret_cast<UPTRINT>(&Child.Get()))));
				ChildArr.Add(MakeShared<FJsonValueObject>(ChildObj));
			}
			OutEntry->SetArrayField(TEXT("children"), ChildArr);
		}

		const FName Tag = Widget->GetTag();
		if (Tag != NAME_None) OutEntry->SetStringField(TEXT("tag"), Tag.ToString());

		const float Opacity = Widget->GetRenderOpacity();
		if (!FMath::IsNearlyEqual(Opacity, 1.0f)) OutEntry->SetNumberField(TEXT("renderOpacity"), Opacity);

		for (TObjectIterator<UWidget> It; It; ++It)
		{
			UWidget* UW = *It;
			if (!IsValid(UW)) continue;
			TSharedPtr<SWidget> CachedWidget = UW->GetCachedWidget();
			if (!CachedWidget.IsValid() || CachedWidget.Get() != Widget.Get()) continue;

			TSharedPtr<FJsonObject> UmgObj = MakeShared<FJsonObject>();
			UmgObj->SetStringField(TEXT("name"),  UW->GetName());
			UmgObj->SetStringField(TEXT("class"), UW->GetClass()->GetName());
			if (UWidgetTree* WT = Cast<UWidgetTree>(UW->GetOuter()))
				if (UUserWidget* Owner = Cast<UUserWidget>(WT->GetOuter()))
					UmgObj->SetStringField(TEXT("ownerWidgetClass"), Owner->GetClass()->GetName());
			OutEntry->SetObjectField(TEXT("umgWidget"), UmgObj);
#if WITH_EDITOR
			FNexusWidgetLayoutUtils::AppendSlateLayoutFields(Widget, OutEntry);
#else
			FNexusWidgetLayoutUtils::AppendUmgLayoutFields(UW, OutEntry);
#endif
			break;
		}

#if WITH_EDITOR
		if (!OutEntry->HasField(TEXT("layout")))
		{
			FNexusWidgetLayoutUtils::AppendSlateLayoutFields(Widget, OutEntry);
		}
#endif

		OutEntries.Add(MakeShared<FJsonValueObject>(OutEntry));
	
	});
}

REGISTER_MCP_CAPABILITY(FGetRuntimeSlateWidgetCapability)
