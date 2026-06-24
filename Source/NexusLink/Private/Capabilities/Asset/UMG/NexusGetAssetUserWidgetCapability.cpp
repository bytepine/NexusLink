// Copyright byteyang. All Rights Reserved.

#include "Capabilities/Asset/UMG/NexusGetAssetUserWidgetCapability.h"
#include "Utils/NexusWidgetLayoutUtils.h"
#include "Utils/NexusJsonUtils.h"
#include "NexusCapabilityRegistry.h"
#include "NexusMcpSchemaBuilder.h"
#include "Utils/NexusAssetUtils.h"
#include "Utils/NexusStringMatchUtils.h"
#if WITH_EDITOR
#include "WidgetBlueprint.h"
#include "Blueprint/WidgetTree.h"
#include "Animation/WidgetAnimation.h"
#endif
#include "Components/Widget.h"
#include "NexusMcpTool.h"

void FGetAssetUserWidgetCapability::BuildDefinition(FNexusCapabilityDefinition& Out) const
{
	Out.Name = TEXT("get_asset_user_widget");
	Out.Description = TEXT("从编辑器读 WBP 树与动画。回答 Widget 问题前必须先调；勿从源码推断。");
	Out.InputSchema = BuildSchemaWithSections();
	Out.Tags = { FNexusMcpTags::Readonly, FNexusMcpTags::Widget };
	Out.ExtraSearchKeywords = {
		TEXT("wbp"), TEXT("umg"), TEXT("hierarchy"), TEXT("children"), TEXT("layout"),
		TEXT("animation"), TEXT("animate"), TEXT("transition")
	};
	Out.RelatedCapabilities = { TEXT("manage_asset_user_widget"), TEXT("create_asset_user_widget") };
	Out.WhenToUse = TEXT("用户问控件树/UMG 动画 — 必须先调，勿 grep 源码");
}

TSharedPtr<FJsonObject> FGetAssetUserWidgetCapability::BuildCapabilitySchema() const
{
	return FNexusSchema::Object()
		.Prop(TEXT("assetPath"),  FNexusSchema::Str(TEXT("Widget 蓝图资产路径")))
		.Prop(TEXT("nameFilter"), FNexusSchema::Str(TEXT("Widget/动画名称子串匹配（可选）")))
		.Prop(TEXT("typeFilter"), FNexusSchema::Str(TEXT("Widget 类子串匹配（仅 widgets 段）")))
		.Prop(TEXT("offset"),     FNexusSchema::Int(TEXT("Widget 分页偏移（默认 0）"), 0, 0))
		.Prop(TEXT("limit"),      FNexusSchema::Int(TEXT("每页最大 Widget 数 1~500（默认 100）"), 100, 1, 500))
		.Required({ TEXT("assetPath") })
		.Build();
}

TArray<FString> FGetAssetUserWidgetCapability::GetSectionNames() const
{
	return { TEXT("widgets"), TEXT("animations") };
}

TArray<FString> FGetAssetUserWidgetCapability::GetDefaultSectionNames() const
{
	return { TEXT("widgets") };
}

bool FGetAssetUserWidgetCapability::PrepareEntry(const TSharedPtr<FJsonObject>& Args,
                                                 TSharedPtr<FJsonObject>&       OutEntry,
                                                 void*&                         OutTargetOpaque,
                                                 FString&                       OutError) const
{
	FString AssetPath;
	if (!Args.IsValid() || !Args->TryGetStringField(TEXT("assetPath"), AssetPath) || AssetPath.IsEmpty())
	{
		OutError = TEXT("assetPath 为必填项");
		return false;
	}

#if WITH_EDITOR
	UWidgetBlueprint* WBP = FNexusAssetUtils::LoadWidgetBP(AssetPath);
	if (!WBP)
	{
		OutError = FString::Printf(TEXT("WidgetBlueprint 未找到: %s"), *AssetPath);
		return false;
	}

	const FString PkgPath = WBP->GetOutermost()->GetName();
	OutEntry->SetStringField(TEXT("assetPath"), AssetPath);
	OutEntry->SetStringField(TEXT("path"),       PkgPath);

	OutTargetOpaque = static_cast<void*>(WBP);
	return true;
#else
	OutError = TEXT("get_asset_user_widget 仅在编辑器构建可用");
	return false;
#endif
}

void FGetAssetUserWidgetCapability::ExecuteSection(const FString&                 SectionName,
                                                   const TSharedPtr<FJsonObject>& Args,
                                                   void*                          TargetOpaque,
                                                   TSharedPtr<FJsonObject>&       InOutDetail,
                                                   FString&                       OutError) const
{
#if WITH_EDITOR
	UWidgetBlueprint* WBP = static_cast<UWidgetBlueprint*>(TargetOpaque);
	if (!WBP)
	{
		OutError = TEXT("无效的 WidgetBlueprint 目标");
		return;
	}

	FString NameFilter, TypeFilter;
	int32 Offset = 0, Limit = 100;
	if (Args.IsValid())
	{
		Args->TryGetStringField(TEXT("nameFilter"), NameFilter);
		Args->TryGetStringField(TEXT("typeFilter"), TypeFilter);
		if (Args->HasField(TEXT("offset"))) Offset = FMath::Max(0, static_cast<int32>(Args->GetNumberField(TEXT("offset"))));
		if (Args->HasField(TEXT("limit")))  Limit  = FMath::Clamp(static_cast<int32>(Args->GetNumberField(TEXT("limit"))), 1, 500);
	}

	if (SectionName == TEXT("widgets"))
	{
		if (!WBP->WidgetTree)
		{
			OutError = TEXT("WidgetTree 为空");
			return;
		}

		struct FWEntry { FString Name; FString Class; FString Parent; };
		TArray<FWEntry> All;
		WBP->WidgetTree->ForEachWidget([&](UWidget* W)
		{
			if (!W) return;
			FWEntry E;
			E.Name  = W->GetName();
			E.Class = W->GetClass()->GetName();
			if (UWidget* P = W->GetParent()) E.Parent = P->GetName();
			All.Add(E);
		});

		All = All.FilterByPredicate([&](const FWEntry& E)
		{
			if (!NameFilter.IsEmpty() && !FNexusStringMatchUtils::Matches(E.Name, NameFilter)) return false;
			if (!TypeFilter.IsEmpty() && !FNexusStringMatchUtils::Matches(E.Class, TypeFilter)) return false;
			return true;
		});

		const int32 Total = All.Num();
		int32 Start, End;
		FNexusJsonUtils::ComputeSlice(Total, Offset, Limit, Start, End);

		TArray<TSharedPtr<FJsonValue>> Page;
		for (int32 i = Start; i < End; ++i)
		{
			TSharedPtr<FJsonObject> O = MakeShared<FJsonObject>();
			O->SetStringField(TEXT("name"),  All[i].Name);
			O->SetStringField(TEXT("class"), All[i].Class);
			if (!All[i].Parent.IsEmpty()) O->SetStringField(TEXT("parent"), All[i].Parent);
			if (UWidget* W = WBP->WidgetTree->FindWidget(FName(*All[i].Name)))
			{
				FNexusWidgetLayoutUtils::AppendUmgLayoutFields(W, O);
			}
			Page.Add(MakeShared<FJsonValueObject>(O));
		}

		InOutDetail->SetNumberField(TEXT("totalCount"), Total);
		InOutDetail->SetNumberField(TEXT("offset"),     Start);
		InOutDetail->SetNumberField(TEXT("limit"),      Limit);
		InOutDetail->SetArrayField(TEXT("widgets"),     Page);
		return;
	}

	if (SectionName == TEXT("animations"))
	{
		TArray<TSharedPtr<FJsonValue>> AnimPage;
		for (int32 i = 0; i < WBP->Animations.Num(); ++i)
		{
			UWidgetAnimation* Anim = WBP->Animations[i];
			if (!Anim) continue;

			const FString ObjName = Anim->GetName();
#if WITH_EDITOR
			const FString DisplayLabel = Anim->GetDisplayLabel();
#else
			const FString DisplayLabel;
#endif
			if (!NameFilter.IsEmpty()
				&& !FNexusStringMatchUtils::Matches(ObjName, NameFilter)
				&& (DisplayLabel.IsEmpty() || !FNexusStringMatchUtils::Matches(DisplayLabel, NameFilter)))
			{
				continue;
			}

			TSharedPtr<FJsonObject> AnimObj = MakeShared<FJsonObject>();
			AnimObj->SetStringField(TEXT("name"), ObjName);
			if (!DisplayLabel.IsEmpty() && DisplayLabel != ObjName)
			{
				AnimObj->SetStringField(TEXT("displayLabel"), DisplayLabel);
			}

			const float StartTime = Anim->GetStartTime();
			const float EndTime   = Anim->GetEndTime();
			AnimObj->SetNumberField(TEXT("startTime"), StartTime);
			AnimObj->SetNumberField(TEXT("endTime"),   EndTime);
			AnimObj->SetNumberField(TEXT("duration"),  FMath::Max(0.f, EndTime - StartTime));

			TArray<TSharedPtr<FJsonValue>> Bindings;
			for (const FWidgetAnimationBinding& Binding : Anim->GetBindings())
			{
				TSharedPtr<FJsonObject> BindObj = MakeShared<FJsonObject>();
				if (!Binding.WidgetName.IsNone())
				{
					BindObj->SetStringField(TEXT("widgetName"), Binding.WidgetName.ToString());
				}
				if (!Binding.SlotWidgetName.IsNone())
				{
					BindObj->SetStringField(TEXT("slotWidgetName"), Binding.SlotWidgetName.ToString());
				}
				if (Binding.bIsRootWidget)
				{
					BindObj->SetBoolField(TEXT("isRootWidget"), true);
				}
				Bindings.Add(MakeShared<FJsonValueObject>(BindObj));
			}
			if (Bindings.Num() > 0)
			{
				AnimObj->SetArrayField(TEXT("bindings"), Bindings);
			}

			AnimPage.Add(MakeShared<FJsonValueObject>(AnimObj));
		}

		InOutDetail->SetNumberField(TEXT("animationCount"), AnimPage.Num());
		InOutDetail->SetArrayField(TEXT("animations"), AnimPage);
		return;
	}

	OutError = FString::Printf(TEXT("不支持的 section '%s'"), *SectionName);
#else
	OutError = TEXT("get_asset_user_widget 仅在编辑器构建可用");
#endif
}

REGISTER_MCP_CAPABILITY(FGetAssetUserWidgetCapability)
