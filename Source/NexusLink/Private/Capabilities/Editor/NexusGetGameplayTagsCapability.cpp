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
		TagObj->SetStringField(TEXT("tag"),       TagStr);
		TagObj->SetStringField(TEXT("shortName"), Child->GetSimpleTagName().ToString());

		TArray<TSharedPtr<FJsonValue>> ChildArr;
		int32 SubCount = Count;
		CapCollectTagChildren(Child, NameFilter, ChildArr, SubCount, Limit, Depth + 1);
		Count = SubCount;

		if (ChildArr.Num() > 0) TagObj->SetArrayField(TEXT("children"), ChildArr);
		OutArray.Add(MakeShared<FJsonValueObject>(TagObj));
		++Count;
	}
}

// ── FNexusCapability 基础钩子 ─────────────────────────────────────────────────

void FGetGameplayTagsCapability::BuildDefinition(FNexusCapabilityDefinition& Out) const
{
	Out.Name = TEXT("get_gameplay_tags");
	Out.Description = TEXT("查 Tag 树/Actor/资产/referencers。sections 含 referencers。");
	Out.InputSchema = BuildSchemaWithSections();
	Out.Tags = {FNexusMcpTags::Readonly, FNexusMcpTags::Editor };
	Out.ExtraSearchKeywords = { TEXT("hierarchy"), TEXT("container"), TEXT("query"), TEXT("gas"), TEXT("actor") };
	Out.RelatedCapabilities = { TEXT("get_runtime_actor_property") };
}

TSharedPtr<FJsonObject> FGetGameplayTagsCapability::BuildCapabilitySchema() const
{
	return FNexusSchema::Object()
		.Prop(TEXT("parentTag"),  FNexusSchema::Str(TEXT("层级子树的根标签")))
		.Prop(TEXT("actorName"),  FNexusSchema::Str(TEXT("运行时 Actor 名（actor 段）")))
		.Prop(TEXT("assetPath"),  FNexusSchema::Str(TEXT("资产路径（asset 段）")))
		.Prop(TEXT("tag"),         FNexusSchema::Str(TEXT("referencers 段：GameplayTag 全名")))
		.Prop(TEXT("nameFilter"), FNexusSchema::Str(TEXT("标签名过滤")))
		.Prop(TEXT("offset"),     FNexusSchema::Int(TEXT("referencers 段分页偏移"), 0, 0))
		.Prop(TEXT("limit"),      FNexusSchema::Int(TEXT("最大条数"), 200, 1, 2000))
		.Build();
}

// ── multi-section 钩子 ────────────────────────────────────────────────────────

TArray<FString> FGetGameplayTagsCapability::GetSectionNames() const
{
	return { TEXT("hierarchy"), TEXT("actor"), TEXT("asset"), TEXT("referencers") };
}

TArray<FString> FGetGameplayTagsCapability::GetDefaultSectionNames() const
{
	return { TEXT("hierarchy") };
}

void FGetGameplayTagsCapability::ExecuteSection(const FString&                 SectionName,
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
		Args->TryGetStringField(TEXT("nameFilter"), NameFilter);
		Args->TryGetStringField(TEXT("tag"), TagName);
		if (Args->HasField(TEXT("offset")))
			Offset = FMath::Max(0, static_cast<int32>(Args->GetNumberField(TEXT("offset"))));
		if (Args->HasField(TEXT("limit")))
			Limit = FMath::Clamp(static_cast<int32>(Args->GetNumberField(TEXT("limit"))), 1, 2000);
	}

	if (SectionName == TEXT("hierarchy"))
	{
		UGameplayTagsManager& Manager = UGameplayTagsManager::Get();

		FString ParentTagStr;
		if (Args.IsValid()) Args->TryGetStringField(TEXT("parentTag"), ParentTagStr);

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
				OutError = FString::Printf(TEXT("标签未找到: '%s'"), *ParentTagStr);
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
				TagObj->SetStringField(TEXT("tag"), TagStr);
				Tags.Add(MakeShared<FJsonValueObject>(TagObj));
				++Count;
			}
		}

		InOutDetail->SetArrayField(TEXT("tags"), Tags);
		InOutDetail->SetNumberField(TEXT("count"), Tags.Num());
	}
	else if (SectionName == TEXT("actor"))
	{
		FString ActorName;
		if (!Args.IsValid() || !Args->TryGetStringField(TEXT("actorName"), ActorName) || ActorName.IsEmpty())
		{
			OutError = TEXT("actor 段需要 actorName");
			return;
		}

		UWorld* World = FNexusRuntimeUtils::RequirePlayWorld(OutError);
		if (!World) return;

		AActor* Actor = FNexusRuntimeUtils::FindActorByName(World, ActorName);
		if (!Actor) { OutError = FString::Printf(TEXT("Actor 未找到: %s"), *ActorName); return; }

		InOutDetail->SetStringField(TEXT("actorName"), Actor->GetName());

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
						TagObj->SetStringField(TEXT("tag"), TagStr);
						TagObj->SetStringField(TEXT("source"), Prefix.IsEmpty()
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
					TagObj->SetStringField(TEXT("tag"), TagStr);
					TagObj->SetStringField(TEXT("source"), Prefix.IsEmpty()
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

		InOutDetail->SetArrayField(TEXT("tags"), Tags);
		InOutDetail->SetNumberField(TEXT("count"), Tags.Num());
	}
	else if (SectionName == TEXT("referencers"))
	{
		if (TagName.IsEmpty())
		{
			OutError = TEXT("referencers 段需要 tag（GameplayTag 全名）");
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
			Item->SetStringField(TEXT("assetPath"), AllPaths[i]);
			Page.Add(MakeShared<FJsonValueObject>(Item));
		}

		InOutDetail->SetStringField(TEXT("tag"), TagName);
		InOutDetail->SetNumberField(TEXT("totalCount"), Total);
		InOutDetail->SetNumberField(TEXT("offset"), Offset);
		InOutDetail->SetArrayField(TEXT("referencers"), Page);
	}
	else if (SectionName == TEXT("asset"))
	{
		FString AssetPath;
		if (!Args.IsValid() || !Args->TryGetStringField(TEXT("assetPath"), AssetPath) || AssetPath.IsEmpty())
		{
			OutError = TEXT("asset 段需要 assetPath");
			return;
		}

		UObject* Asset = FNexusAssetUtils::LoadAssetWithFallback<UObject>(AssetPath);
		if (!Asset)
		{
			OutError = FString::Printf(TEXT("资产未找到: %s"), *AssetPath);
			return;
		}

		InOutDetail->SetStringField(TEXT("assetPath"),  AssetPath);
		InOutDetail->SetStringField(TEXT("assetClass"), Asset->GetClass()->GetName());

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
					TagObj->SetStringField(TEXT("tag"),      TagStr);
					TagObj->SetStringField(TEXT("property"), It->GetName());
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
				TagObj->SetStringField(TEXT("tag"),      TagStr);
				TagObj->SetStringField(TEXT("property"), It->GetName());
				Tags.Add(MakeShared<FJsonValueObject>(TagObj));
			}
		}
		InOutDetail->SetArrayField(TEXT("tags"),  Tags);
		InOutDetail->SetNumberField(TEXT("count"), Tags.Num());
	}
	else
	{
		OutError = FString::Printf(TEXT("未处理的 section '%s'"), *SectionName);
	}
}

REGISTER_MCP_CAPABILITY(FGetGameplayTagsCapability)

#endif // WITH_EDITOR
