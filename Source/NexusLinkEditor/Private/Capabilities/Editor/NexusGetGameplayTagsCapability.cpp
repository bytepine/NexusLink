// Copyright byteyang. All Rights Reserved.

#include "Capabilities/Editor/NexusGetGameplayTagsCapability.h"

#if WITH_EDITOR

#include "NexusCapabilityRegistry.h"
#include "NexusMcpSchemaBuilder.h"
#include "Utils/NexusStringMatchUtils.h"
#include "Utils/NexusRuntimeUtils.h"
#include "Utils/NexusAssetUtils.h"
#include "GameplayTagsManager.h"
#include "GameplayTagContainer.h"
#include "GameFramework/Actor.h"
#include "Utils/NexusGameplayTagReferencerUtils.h"
#include "Utils/NexusJsonUtils.h"
#include "NexusMcpTool.h"

/** 递归收集 Tag 子节点，构建层级树。*/
static void CapCollectTagChildren(
	TSharedPtr<FGameplayTagNode> Node,
	const FString& NameFilter,
	TArray<TSharedPtr<FJsonValue>>& OutArray,
	int32& Count,
	int32 Limit,
	int32 Depth)
{
	if (!Node.IsValid() || Count >= Limit || Depth > 20) return;

	const TArray<TSharedPtr<FGameplayTagNode>>& Children = Node->GetChildTagNodes();
	for (const TSharedPtr<FGameplayTagNode>& Child : Children)
	{
		if (Count >= Limit) break;
		if (!Child.IsValid()) continue;

		const FString TagStr = Child->GetCompleteTag().ToString();
		if (!NameFilter.IsEmpty() && !FNexusStringMatchUtils::Matches(TagStr, NameFilter)) continue;

		TSharedPtr<FJsonObject> TagObj = MakeShared<FJsonObject>();
		TagObj->SetStringField(TEXT("Tag"),       TagStr);
		TagObj->SetStringField(TEXT("ShortName"), Child->GetSimpleTagName().ToString());

		TArray<TSharedPtr<FJsonValue>> ChildArr;
		int32 SubCount = Count;
		CapCollectTagChildren(Child, NameFilter, ChildArr, SubCount, Limit, Depth + 1);
		Count = SubCount;

		if (ChildArr.Num() > 0) TagObj->SetArrayField(TEXT("Children"), ChildArr);
		OutArray.Add(MakeShared<FJsonValueObject>(TagObj));
		++Count;
	}
}

// ── FNexusCapability 基础钩子 ─────────────────────────────────────────────────

void FNexusGetGameplayTagsCapability::BuildDefinition(FNexusCapabilityDefinition& Out) const
{
	Out.Name = TEXT("GetGameplayTags");
	Out.Description = TEXT("Query Tag tree/Actor/asset/referencers. sections includes referencers.");
	Out.InputSchema = BuildSchemaWithSections();
	Out.Tags = {FNexusMcpTags::Readonly, FNexusMcpTags::Editor };
	// "Query" 不是本 cap 的参数（易误导 Agent 照抄传参），去掉；name 已含 token "Tags"
	// （GetGameplayTags 拆词），补单数 tag / 缩写 gtag 覆盖更多口语查询
	Out.ExtraSearchKeywords = { TEXT("Hierarchy"), TEXT("container"), TEXT("gas"), TEXT("Actor"), TEXT("Tag"), TEXT("gtag") };
	Out.RelatedCapabilities = { TEXT("GetRuntimeActorProperty") };
}

TSharedPtr<FJsonObject> FNexusGetGameplayTagsCapability::BuildCapabilitySchema() const
{
	return FNexusSchema::Object()
		.Prop(TEXT("ParentTag"),  FNexusSchema::Str(TEXT("Root tag for subtree")))
		.Prop(TEXT("ActorName"),  FNexusSchema::Str(TEXT("Runtime Actor name (actor section)")))
		.Prop(TEXT("AssetPath"),  FNexusSchema::Str(TEXT("Asset path (asset section)")))
		.Prop(TEXT("Tag"),         FNexusSchema::Str(TEXT("referencers section: GameplayTag full name")))
		.Prop(TEXT("NameFilter"), FNexusSchema::Str(TEXT("Tag name filter")))
		.Prop(TEXT("Offset"),     FNexusSchema::Int(TEXT("referencers section pagination offset"), 0, 0))
		.Prop(TEXT("Limit"),      FNexusSchema::Int(TEXT("Max count"), 200, 1, 2000))
		.Build();
}

// ── multi-section 钩子 ────────────────────────────────────────────────────────

TArray<FString> FNexusGetGameplayTagsCapability::GetSectionNames() const
{
	return { TEXT("Hierarchy"), TEXT("Actor"), TEXT("Asset"), TEXT("Referencers"), TEXT("Info"), TEXT("Sources") };
}

TArray<FString> FNexusGetGameplayTagsCapability::GetDefaultSectionNames() const
{
	return { TEXT("Hierarchy") };
}

void FNexusGetGameplayTagsCapability::ExecuteSection(const FString&                 SectionName,
                                                const TSharedPtr<FJsonObject>& Args,
                                                void*                          /*TargetOpaque*/,
                                                TSharedPtr<FJsonObject>&       InOutDetail,
                                                FString&                       OutError) const
{
	FString NameFilter;
	FString TagName;
	int32   Offset = 0;
	int32   Limit = 200;
	if (Args.IsValid())
	{
		Args->TryGetStringField(TEXT("NameFilter"), NameFilter);
		Args->TryGetStringField(TEXT("Tag"), TagName);
		if (Args->HasField(TEXT("Offset")))
			Offset = FMath::Max(0, static_cast<int32>(Args->GetNumberField(TEXT("Offset"))));
		if (Args->HasField(TEXT("Limit")))
			Limit = FMath::Clamp(static_cast<int32>(Args->GetNumberField(TEXT("Limit"))), 1, 2000);
	}

    if (SectionName == TEXT("Info")) {
        auto Node = UGameplayTagsManager::Get().FindTagNode(FName(*TagName));
        if (!Node.IsValid()) { OutError = TEXT("Tag not found"); return; }
        auto Info = MakeShared<FJsonObject>(); Info->SetStringField(TEXT("Tag"), Node->GetCompleteTagName().ToString());
        FString Comment; FName Source; bool bExplicit = false, bRestricted = false, bAllowChildren = false;
        UGameplayTagsManager::Get().GetTagEditorData(FName(*TagName), Comment, Source, bExplicit, bRestricted, bAllowChildren);
        Info->SetBoolField(TEXT("bExplicit"), bExplicit); Info->SetStringField(TEXT("Source"), Source.ToString()); Info->SetStringField(TEXT("Comment"), Comment);
        InOutDetail->SetObjectField(TEXT("Info"), Info); return;
    }
    if (SectionName == TEXT("Sources")) {
        TArray<const FGameplayTagSource*> Sources;
        UGameplayTagsManager::Get().FindTagSourcesWithType(EGameplayTagSourceType::TagList, Sources);
        TArray<TSharedPtr<FJsonValue>> Values;
        for (const auto* Source : Sources) if (Source && (NameFilter.IsEmpty() || FNexusStringMatchUtils::Matches(Source->SourceName.ToString(), NameFilter))) {
            auto Item = MakeShared<FJsonObject>(); Item->SetStringField(TEXT("Name"), Source->SourceName.ToString()); Values.Add(MakeShared<FJsonValueObject>(Item));
        }
        Values.Sort([](const auto& Left, const auto& Right) { return Left->AsObject()->GetStringField(TEXT("Name")) < Right->AsObject()->GetStringField(TEXT("Name")); });
        InOutDetail->SetArrayField(TEXT("Sources"), Values); return;
    }
	if (SectionName == TEXT("Hierarchy"))
	{
		UGameplayTagsManager& Manager = UGameplayTagsManager::Get();

		FString ParentTagStr;
		if (Args.IsValid()) Args->TryGetStringField(TEXT("ParentTag"), ParentTagStr);

		TSharedPtr<FGameplayTagNode> StartNode;
		if (ParentTagStr.IsEmpty())
		{
			StartNode = Manager.FindTagNode(FGameplayTag());
		}
		else
		{
			FGameplayTag ParentTag = Manager.RequestGameplayTag(FName(*ParentTagStr), false);
			if (!ParentTag.IsValid())
			{
				OutError = FString::Printf(TEXT("Tag not found: '%s'"), *ParentTagStr);
				return;
			}
			StartNode = Manager.FindTagNode(ParentTag);
		}

		TArray<TSharedPtr<FJsonValue>> Tags;
		if (StartNode.IsValid())
		{
			int32 Count = 0;
			CapCollectTagChildren(StartNode, NameFilter, Tags, Count, Limit, 0);
		}
		else
		{
			FGameplayTagContainer AllTags;
			Manager.RequestAllGameplayTags(AllTags, true);
			int32 Count = 0;
			for (const FGameplayTag& Tag : AllTags)
			{
				if (Count >= Limit) break;
				const FString TagStr = Tag.ToString();
				if (!NameFilter.IsEmpty() && !FNexusStringMatchUtils::Matches(TagStr, NameFilter)) continue;
				TSharedPtr<FJsonObject> TagObj = MakeShared<FJsonObject>();
				TagObj->SetStringField(TEXT("Tag"), TagStr);
				Tags.Add(MakeShared<FJsonValueObject>(TagObj));
				++Count;
			}
		}

		InOutDetail->SetArrayField(TEXT("Tags"), Tags);
		InOutDetail->SetNumberField(TEXT("Count"), Tags.Num());
	}
	else if (SectionName == TEXT("Actor"))
	{
		FString ActorName;
		if (!Args.IsValid() || !Args->TryGetStringField(TEXT("ActorName"), ActorName) || ActorName.IsEmpty())
		{
			OutError = TEXT("actor section requires ActorName");
			return;
		}

		// 只读：未开 PIE 时仍可扫编辑器关卡 Actor
		UWorld* World = FNexusRuntimeUtils::GetActiveWorld();
		if (!World)
		{
			OutError = TEXT("No active World");
			return;
		}

		AActor* Actor = FNexusRuntimeUtils::FindActorByName(World, ActorName);
		if (!Actor) { OutError = FString::Printf(TEXT("Actor not found: %s"), *ActorName); return; }

		InOutDetail->SetStringField(TEXT("ActorName"), Actor->GetName());

		TArray<TSharedPtr<FJsonValue>> Tags;

		auto ScanObject = [&](UObject* Obj, const FString& Prefix)
		{
			for (TFieldIterator<FProperty> It(Obj->GetClass()); It; ++It)
			{
				FStructProperty* StructProp = CastField<FStructProperty>(*It);
				if (!StructProp) continue;

				if (StructProp->Struct == FGameplayTagContainer::StaticStruct())
				{
					const FGameplayTagContainer* Container =
						StructProp->ContainerPtrToValuePtr<FGameplayTagContainer>(Obj);
					if (!Container) continue;
					for (const FGameplayTag& Tag : *Container)
					{
						const FString TagStr = Tag.ToString();
						if (!NameFilter.IsEmpty() && !FNexusStringMatchUtils::Matches(TagStr, NameFilter)) continue;
						TSharedPtr<FJsonObject> TagObj = MakeShared<FJsonObject>();
						TagObj->SetStringField(TEXT("Tag"), TagStr);
						TagObj->SetStringField(TEXT("Source"), Prefix.IsEmpty()
							? It->GetName() : (Prefix + TEXT(".") + It->GetName()));
						Tags.Add(MakeShared<FJsonValueObject>(TagObj));
					}
				}
				else if (StructProp->Struct == FGameplayTag::StaticStruct())
				{
					const FGameplayTag* Tag = StructProp->ContainerPtrToValuePtr<FGameplayTag>(Obj);
					if (!Tag || !Tag->IsValid()) continue;
					const FString TagStr = Tag->ToString();
					if (!NameFilter.IsEmpty() && !FNexusStringMatchUtils::Matches(TagStr, NameFilter)) continue;
					TSharedPtr<FJsonObject> TagObj = MakeShared<FJsonObject>();
					TagObj->SetStringField(TEXT("Tag"), TagStr);
					TagObj->SetStringField(TEXT("Source"), Prefix.IsEmpty()
						? It->GetName() : (Prefix + TEXT(".") + It->GetName()));
					Tags.Add(MakeShared<FJsonValueObject>(TagObj));
				}
			}
		};

		ScanObject(Actor, TEXT(""));
		TArray<UActorComponent*> Components;
		Actor->GetComponents(Components);
		for (UActorComponent* Comp : Components)
			if (Comp) ScanObject(Comp, Comp->GetName());

		InOutDetail->SetArrayField(TEXT("Tags"), Tags);
		InOutDetail->SetNumberField(TEXT("Count"), Tags.Num());
	}
	else if (SectionName == TEXT("Referencers"))
	{
		if (TagName.IsEmpty())
		{
			OutError = TEXT("referencers section requires tag (GameplayTag full name)");
			return;
		}

		TArray<FString> AllPaths;
		if (!FNexusGameplayTagReferencerUtils::FindReferencerPackagePaths(TagName, AllPaths, OutError))
		{
			return;
		}

		const int32 Total = AllPaths.Num();
		int32 Start = 0;
		int32 End   = 0;
		FNexusJsonUtils::ComputeSlice(Total, Offset, Limit, Start, End);

		TArray<TSharedPtr<FJsonValue>> Page;
		for (int32 i = Start; i < End; ++i)
		{
			TSharedPtr<FJsonObject> Item = MakeShared<FJsonObject>();
			Item->SetStringField(TEXT("Path"), AllPaths[i]);
			Page.Add(MakeShared<FJsonValueObject>(Item));
		}

		InOutDetail->SetStringField(TEXT("Tag"), TagName);
		InOutDetail->SetNumberField(TEXT("TotalCount"), Total);
		InOutDetail->SetNumberField(TEXT("Offset"), Offset);
		InOutDetail->SetArrayField(TEXT("Referencers"), Page);
	}
	else if (SectionName == TEXT("Asset"))
	{
		FString AssetPath;
		if (!Args.IsValid() || !Args->TryGetStringField(TEXT("AssetPath"), AssetPath) || AssetPath.IsEmpty())
		{
			OutError = TEXT("asset section requires AssetPath");
			return;
		}

		UObject* Asset = FNexusAssetUtils::LoadAssetWithFallback<UObject>(AssetPath);
		if (!Asset)
		{
			OutError = FString::Printf(TEXT("Asset not found: %s"), *AssetPath);
			return;
		}

		InOutDetail->SetStringField(TEXT("Path"),  AssetPath);
		InOutDetail->SetStringField(TEXT("AssetClass"), Asset->GetClass()->GetName());

		TArray<TSharedPtr<FJsonValue>> Tags;
		for (TFieldIterator<FProperty> It(Asset->GetClass()); It; ++It)
		{
			FStructProperty* StructProp = CastField<FStructProperty>(*It);
			if (!StructProp) continue;

			if (StructProp->Struct == FGameplayTagContainer::StaticStruct())
			{
				const FGameplayTagContainer* Container =
					StructProp->ContainerPtrToValuePtr<FGameplayTagContainer>(Asset);
				if (!Container) continue;
				for (const FGameplayTag& Tag : *Container)
				{
					const FString TagStr = Tag.ToString();
					if (!NameFilter.IsEmpty() && !FNexusStringMatchUtils::Matches(TagStr, NameFilter)) continue;
					TSharedPtr<FJsonObject> TagObj = MakeShared<FJsonObject>();
					TagObj->SetStringField(TEXT("Tag"),      TagStr);
					TagObj->SetStringField(TEXT("Property"), It->GetName());
					Tags.Add(MakeShared<FJsonValueObject>(TagObj));
				}
			}
			else if (StructProp->Struct == FGameplayTag::StaticStruct())
			{
				const FGameplayTag* Tag = StructProp->ContainerPtrToValuePtr<FGameplayTag>(Asset);
				if (!Tag || !Tag->IsValid()) continue;
				const FString TagStr = Tag->ToString();
				if (!NameFilter.IsEmpty() && !FNexusStringMatchUtils::Matches(TagStr, NameFilter)) continue;
				TSharedPtr<FJsonObject> TagObj = MakeShared<FJsonObject>();
				TagObj->SetStringField(TEXT("Tag"),      TagStr);
				TagObj->SetStringField(TEXT("Property"), It->GetName());
				Tags.Add(MakeShared<FJsonValueObject>(TagObj));
			}
		}
		InOutDetail->SetArrayField(TEXT("Tags"),  Tags);
		InOutDetail->SetNumberField(TEXT("Count"), Tags.Num());
	}
	else
	{
		OutError = FString::Printf(TEXT("Unhandled section '%s'"), *SectionName);
	}
}

REGISTER_MCP_CAPABILITY(FNexusGetGameplayTagsCapability)

#endif // WITH_EDITOR
