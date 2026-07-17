// Copyright byteyang. All Rights Reserved.

#include "Capabilities/Asset/AI/NexusGetAssetBlackboardCapability.h"
#include "Utils/NexusCapabilityResultBuilder.h"
#include "NexusCapabilityRegistry.h"
#include "NexusMcpSchemaBuilder.h"
#include "Utils/NexusAssetUtils.h"
#include "Utils/NexusStringMatchUtils.h"
#include "BehaviorTree/BlackboardData.h"
#include "NexusMcpTool.h"

void FGetAssetBlackboardCapability::BuildDefinition(FNexusCapabilityDefinition& Out) const
{
	Out.Name = TEXT("get_asset_blackboard");
	Out.SearchAssetTypes = {TEXT("Blackboard")};
	Out.Description = TEXT("检查 BB 键定义。返回名称与类型快照；只读。");
	Out.InputSchema = FNexusSchema::Object()
		.Prop(TEXT("assetPath"), FNexusSchema::Str(TEXT("BlackboardData 资产路径")))
		.Prop(TEXT("nameFilter"), FNexusSchema::Str(TEXT("黑板键名过滤")))
		.Build();
	Out.Tags = {FNexusMcpTags::Readonly, FNexusMcpTags::Blueprint };
	Out.ExtraSearchKeywords = { TEXT("blackboard"), TEXT("bb"), TEXT("keys"), TEXT("ai") };
	Out.RelatedCapabilities = { TEXT("manage_asset_blackboard"), TEXT("get_asset_behavior_tree") };
	Out.WhenToUse = TEXT("读 BB 键列表；运行时值用 get_runtime_actor_behavior_tree");
}

FCapabilityResult FGetAssetBlackboardCapability::Execute(const TSharedPtr<FJsonObject>& Arguments) const
{

	return FNexusCapabilityResultBuilder::Build([&](auto& OutEntries, auto& OutTop, auto& OutError)
	{

		TSharedPtr<FJsonObject> OutEntry = MakeShared<FJsonObject>();

		FString NameFilter;
		Arguments->TryGetStringField(TEXT("nameFilter"), NameFilter);

		FString AssetPath;
		if (!Arguments->TryGetStringField(TEXT("assetPath"), AssetPath) || AssetPath.IsEmpty())
		{
			OutEntry->SetStringField(TEXT("error"), TEXT("assetPath 为必填项"));
			OutEntries.Add(MakeShared<FJsonValueObject>(OutEntry));
			return;
		}

		UBlackboardData* BB = FNexusAssetUtils::LoadAssetWithFallback<UBlackboardData>(AssetPath);

		if (!BB)
		{
			OutEntry->SetStringField(TEXT("error"), TEXT("给定路径的 BlackboardData 未找到"));
			OutEntries.Add(MakeShared<FJsonValueObject>(OutEntry));
			return;
		}

		TArray<TSharedPtr<FJsonValue>> Keys;
		for (const FBlackboardEntry& Entry : BB->Keys)
		{
			if (!NameFilter.IsEmpty() && !FNexusStringMatchUtils::Matches(Entry.EntryName.ToString(), NameFilter))
			{
				continue;
			}

			TSharedPtr<FJsonObject> KeyObj = MakeShared<FJsonObject>();
			KeyObj->SetStringField(TEXT("name"), Entry.EntryName.ToString());
			if (Entry.KeyType)
			{
				// 去掉 BlackboardKeyType_ 前缀，只保留类型名
				FString TypeName = Entry.KeyType->GetClass()->GetName();
				TypeName.RemoveFromStart(TEXT("BlackboardKeyType_"));
				KeyObj->SetStringField(TEXT("type"), TypeName);
			}
			Keys.Add(MakeShared<FJsonValueObject>(KeyObj));
		}
		OutEntry->SetArrayField(TEXT("keys"), Keys);

		// 递归收集所有 parent 链上的 keys
		TArray<TSharedPtr<FJsonValue>> InheritedKeys;
		for (UBlackboardData* Cur = BB->Parent; Cur; Cur = Cur->Parent)
		{
			for (const FBlackboardEntry& Entry : Cur->Keys)
			{
				if (!NameFilter.IsEmpty() && !FNexusStringMatchUtils::Matches(Entry.EntryName.ToString(), NameFilter))
				{
					continue;
				}

				TSharedPtr<FJsonObject> KeyObj = MakeShared<FJsonObject>();
				KeyObj->SetStringField(TEXT("name"), Entry.EntryName.ToString());
				if (Entry.KeyType)
				{
					FString TypeName = Entry.KeyType->GetClass()->GetName();
					TypeName.RemoveFromStart(TEXT("BlackboardKeyType_"));
					KeyObj->SetStringField(TEXT("type"), TypeName);
				}
				KeyObj->SetStringField(TEXT("from"), Cur->GetPathName());
				InheritedKeys.Add(MakeShared<FJsonValueObject>(KeyObj));
			}
		}
		if (InheritedKeys.Num() > 0)
		{
			OutEntry->SetArrayField(TEXT("parentKeys"), InheritedKeys);
		}

		// 输出直接 parent 路径
		if (BB->Parent)
		{
			OutEntry->SetStringField(TEXT("parentPath"), BB->Parent->GetPathName());
		}

		OutEntries.Add(MakeShared<FJsonValueObject>(OutEntry));
	
	});
}

REGISTER_MCP_CAPABILITY(FGetAssetBlackboardCapability)

