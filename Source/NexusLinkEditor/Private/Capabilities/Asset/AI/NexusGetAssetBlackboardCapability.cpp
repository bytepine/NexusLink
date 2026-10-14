// Copyright byteyang. All Rights Reserved.

#include "Capabilities/Asset/AI/NexusGetAssetBlackboardCapability.h"
#include "Utils/NexusCapabilityResultBuilder.h"
#include "NexusCapabilityRegistry.h"
#include "NexusMcpSchemaBuilder.h"
#include "Utils/NexusAssetUtils.h"
#include "Utils/NexusStringMatchUtils.h"
#include "BehaviorTree/BlackboardData.h"
#include "NexusMcpTool.h"

void FNexusGetAssetBlackboardCapability::BuildDefinition(FNexusCapabilityDefinition& Out) const
{
	Out.Name = TEXT("GetAssetBlackboard");
	Out.SearchAssetTypes = {TEXT("Blackboard")};
	Out.Description = TEXT("Inspect BB key definitions. Name/type snapshot; read-only.");
	Out.InputSchema = FNexusSchema::Object()
		.Prop(TEXT("AssetPath"), FNexusSchema::Str(TEXT("BlackboardData asset path")))
		.Prop(TEXT("NameFilter"), FNexusSchema::Str(TEXT("Blackboard key name filter")))
		.Build();
	Out.Tags = {FNexusMcpTags::Readonly, FNexusMcpTags::Blueprint };
	Out.ExtraSearchKeywords = { TEXT("Blackboard"), TEXT("bb"), TEXT("Keys"), TEXT("ai") };
	Out.RelatedCapabilities = { TEXT("ManageAssetBlackboard"), TEXT("GetAssetBehaviorTree") };
	Out.WhenToUse = TEXT("Read BB key list; runtime values via GetRuntimeActorBehaviorTree");
}

FCapabilityResult FNexusGetAssetBlackboardCapability::Execute(const TSharedPtr<FJsonObject>& Arguments) const
{

	return FNexusCapabilityResultBuilder::Build([&](auto& OutEntries, auto& OutTop, auto& OutError)
	{

		TSharedPtr<FJsonObject> OutEntry = MakeShared<FJsonObject>();

		FString NameFilter;
		Arguments->TryGetStringField(TEXT("NameFilter"), NameFilter);

		FString AssetPath;
		if (!Arguments->TryGetStringField(TEXT("AssetPath"), AssetPath) || AssetPath.IsEmpty())
		{
			OutEntry->SetStringField(TEXT("Error"), TEXT("AssetPath is required"));
			OutEntries.Add(MakeShared<FJsonValueObject>(OutEntry));
			return;
		}

		UBlackboardData* BB = FNexusAssetUtils::LoadAssetWithFallback<UBlackboardData>(AssetPath);

		if (!BB)
		{
			OutEntry->SetStringField(TEXT("Error"), TEXT("BlackboardData not found at given path"));
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
		OutEntry->SetArrayField(TEXT("Keys"), Keys);

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
				KeyObj->SetStringField(TEXT("From"), Cur->GetPathName());
				InheritedKeys.Add(MakeShared<FJsonValueObject>(KeyObj));
			}
		}
		if (InheritedKeys.Num() > 0)
		{
			OutEntry->SetArrayField(TEXT("ParentKeys"), InheritedKeys);
		}

		// 输出直接 parent 路径
		if (BB->Parent)
		{
			OutEntry->SetStringField(TEXT("ParentPath"), BB->Parent->GetPathName());
		}

		OutEntries.Add(MakeShared<FJsonValueObject>(OutEntry));
	
	});
}

REGISTER_MCP_CAPABILITY(FNexusGetAssetBlackboardCapability)

