// Copyright byteyang. All Rights Reserved.

#include "Capabilities/Asset/UMG/NexusGetAssetUserWidgetCapability.h"
#include "Utils/NexusWidgetLayoutUtils.h"
#include "Utils/NexusJsonUtils.h"
#include "NexusCapabilityRegistry.h"
#include "NexusMcpSchemaBuilder.h"
#include "Utils/NexusAssetUtils.h"
#include "Utils/NexusAssetEditorUtils.h"
#include "Utils/NexusStringMatchUtils.h"
#include "Utils/NexusWidgetAnimationUtils.h"
#include "Utils/NexusBlueprintGraphUtils.h"
#if WITH_EDITOR
#include "WidgetBlueprint.h"
#include "Engine/Blueprint.h"
#include "Blueprint/WidgetTree.h"
#include "Animation/WidgetAnimation.h"
#include "EdGraph/EdGraph.h"
#endif
#include "Components/Widget.h"
#include "NexusMcpTool.h"

void FNexusGetAssetUserWidgetCapability::BuildDefinition(FNexusCapabilityDefinition& Out) const
{
	Out.Name = TEXT("GetAssetUserWidget");
	Out.SearchAssetTypes = {TEXT("Widget")};
	Out.Description = TEXT("Read WBP tree, animations, graphOverview from editor. Graph details via blueprint cap.");
	Out.InputSchema = BuildSchemaWithSections();
	Out.Tags = { FNexusMcpTags::Readonly, FNexusMcpTags::Widget };
	Out.ExtraSearchKeywords = {
		TEXT("wbp"), TEXT("umg"), TEXT("Hierarchy"), TEXT("Children"), TEXT("Layout"),
		TEXT("Animation"), TEXT("animate"), TEXT("Transition"), TEXT("graph")
	};
	Out.RelatedCapabilities = {
		TEXT("ManageAssetUserWidget"), TEXT("CreateAssetUserWidget"),
		TEXT("GetAssetBlueprint"), TEXT("ManageAssetBlueprint")
	};
	Out.WhenToUse = TEXT("Widget tree/animations here; EventGraph writes via ManageAssetBlueprint");
}

TSharedPtr<FJsonObject> FNexusGetAssetUserWidgetCapability::BuildCapabilitySchema() const
{
	return FNexusSchema::Object()
		.Prop(TEXT("AssetPath"),  FNexusSchema::Str(TEXT("Widget Blueprint asset path")))
		.Prop(TEXT("NameFilter"), FNexusSchema::Str(TEXT("Widget/animation name substring (optional)")))
		.Prop(TEXT("TypeFilter"), FNexusSchema::Str(TEXT("Widget class substring (widgets section only)")))
		.Prop(TEXT("Offset"),     FNexusSchema::Int(TEXT("Widget paginationoffset (default 0)"), 0, 0))
		.Prop(TEXT("Limit"),      FNexusSchema::Int(TEXT("Max widgets per page 1-500 (default 100)"), 100, 1, 500))
		.Required({ TEXT("AssetPath") })
		.Build();
}

TArray<FString> FNexusGetAssetUserWidgetCapability::GetSectionNames() const
{
	return { TEXT("Widgets"), TEXT("Animations"), TEXT("graphOverview") };
}

TArray<FString> FNexusGetAssetUserWidgetCapability::GetDefaultSectionNames() const
{
	return { TEXT("Widgets") };
}

bool FNexusGetAssetUserWidgetCapability::PrepareEntry(const TSharedPtr<FJsonObject>& Args,
                                                 TSharedPtr<FJsonObject>&       OutEntry,
                                                 void*&                         OutTargetOpaque,
                                                 FString&                       OutError) const
{
	FString AssetPath;
	if (!Args.IsValid() || !Args->TryGetStringField(TEXT("AssetPath"), AssetPath) || AssetPath.IsEmpty())
	{
		OutError = TEXT("AssetPath is required");
		return false;
	}

#if WITH_EDITOR
	UWidgetBlueprint* WBP = FNexusAssetEditorUtils::LoadWidgetBP(AssetPath);
	if (!WBP)
	{
		OutError = FString::Printf(TEXT("WidgetBlueprint not found: %s"), *AssetPath);
		return false;
	}

	const FString PkgPath = WBP->GetOutermost()->GetName();
	// 身份字段统一为 path；与入参等价时由适配层 StripRedundantPathEcho 省略回显
	OutEntry->SetStringField(TEXT("Path"), PkgPath);

	OutTargetOpaque = static_cast<void*>(WBP);
	return true;
#else
	OutError = TEXT("GetAssetUserWidget only available in editor builds");
	return false;
#endif
}

void FNexusGetAssetUserWidgetCapability::ExecuteSection(const FString&                 SectionName,
                                                   const TSharedPtr<FJsonObject>& Args,
                                                   void*                          TargetOpaque,
                                                   TSharedPtr<FJsonObject>&       InOutDetail,
                                                   FString&                       OutError) const
{
#if WITH_EDITOR
	UWidgetBlueprint* WBP = static_cast<UWidgetBlueprint*>(TargetOpaque);
	if (!WBP)
	{
		OutError = TEXT("Invalid WidgetBlueprint target");
		return;
	}

	FString NameFilter, TypeFilter;
	int32 Offset = 0, Limit = 100;
	if (Args.IsValid())
	{
		Args->TryGetStringField(TEXT("NameFilter"), NameFilter);
		Args->TryGetStringField(TEXT("TypeFilter"), TypeFilter);
		if (Args->HasField(TEXT("Offset"))) Offset = FMath::Max(0, static_cast<int32>(Args->GetNumberField(TEXT("Offset"))));
		if (Args->HasField(TEXT("Limit")))  Limit  = FMath::Clamp(static_cast<int32>(Args->GetNumberField(TEXT("Limit"))), 1, 500);
	}

	if (SectionName == TEXT("Widgets"))
	{
		if (!WBP->WidgetTree)
		{
			OutError = TEXT("WidgetTree is empty");
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
			O->SetStringField(TEXT("Class"), All[i].Class);
			if (!All[i].Parent.IsEmpty()) O->SetStringField(TEXT("Parent"), All[i].Parent);
			if (UWidget* W = WBP->WidgetTree->FindWidget(FName(*All[i].Name)))
			{
				FNexusWidgetLayoutUtils::AppendUmgLayoutFields(W, O);
			}
			Page.Add(MakeShared<FJsonValueObject>(O));
		}

		InOutDetail->SetNumberField(TEXT("TotalCount"), Total);
		InOutDetail->SetNumberField(TEXT("Offset"),     Start);
		InOutDetail->SetNumberField(TEXT("Limit"),      Limit);
		InOutDetail->SetArrayField(TEXT("Widgets"),     Page);
		return;
	}

	if (SectionName == TEXT("Animations"))
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
				AnimObj->SetStringField(TEXT("DisplayLabel"), DisplayLabel);
			}

			const float StartTime = Anim->GetStartTime();
			const float EndTime   = Anim->GetEndTime();
			AnimObj->SetNumberField(TEXT("StartTime"), StartTime);
			AnimObj->SetNumberField(TEXT("EndTime"),   EndTime);
			AnimObj->SetNumberField(TEXT("Duration"),  FMath::Max(0.f, EndTime - StartTime));

			TArray<TSharedPtr<FJsonValue>> Bindings;
			for (const FWidgetAnimationBinding& Binding : Anim->GetBindings())
			{
				TSharedPtr<FJsonObject> BindObj = MakeShared<FJsonObject>();
				if (!Binding.WidgetName.IsNone())
				{
					BindObj->SetStringField(TEXT("WidgetName"), Binding.WidgetName.ToString());
				}
				if (!Binding.SlotWidgetName.IsNone())
				{
					BindObj->SetStringField(TEXT("SlotWidgetName"), Binding.SlotWidgetName.ToString());
				}
				if (Binding.bIsRootWidget)
				{
					BindObj->SetBoolField(TEXT("bIsRootWidget"), true);
				}
				Bindings.Add(MakeShared<FJsonValueObject>(BindObj));
			}
			if (Bindings.Num() > 0)
			{
				AnimObj->SetArrayField(TEXT("Bindings"), Bindings);
			}

			TArray<TSharedPtr<FJsonValue>> Tracks;
			FNexusWidgetAnimationUtils::AppendTrackSummaries(Anim, Tracks);
			if (Tracks.Num() > 0)
			{
				AnimObj->SetArrayField(TEXT("Tracks"), Tracks);
			}

			AnimPage.Add(MakeShared<FJsonValueObject>(AnimObj));
		}

		InOutDetail->SetNumberField(TEXT("AnimationCount"), AnimPage.Num());
		InOutDetail->SetArrayField(TEXT("Animations"), AnimPage);
		return;
	}

	if (SectionName == TEXT("graphOverview"))
	{
		UBlueprint* BP = Cast<UBlueprint>(WBP);
		if (!BP)
		{
			OutError = TEXT("Cannot cast WidgetBlueprint to Blueprint to enumerate graphs");
			return;
		}
		TArray<UEdGraph*> AllGraphs;
		FNexusBlueprintGraphUtils::CollectAllGraphs(BP, AllGraphs);
		TArray<TSharedPtr<FJsonValue>> List;
		for (const UEdGraph* G : AllGraphs)
		{
			if (G)
			{
				List.Add(MakeShared<FJsonValueObject>(FNexusBlueprintGraphUtils::BuildBPGraphSummary(G)));
			}
		}
		InOutDetail->SetArrayField(TEXT("Graphs"), List);
		InOutDetail->SetStringField(TEXT("Hint"), TEXT("Graph details via GetAssetBlueprint; EventGraph writes via ManageAssetBlueprint"));
		return;
	}

	OutError = FString::Printf(TEXT("Unsupported section '%s'"), *SectionName);
#else
	OutError = TEXT("GetAssetUserWidget only available in editor builds");
#endif
}

REGISTER_MCP_CAPABILITY(FNexusGetAssetUserWidgetCapability)
