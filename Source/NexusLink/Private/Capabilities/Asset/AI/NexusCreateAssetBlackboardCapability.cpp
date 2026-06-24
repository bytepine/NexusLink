// Copyright byteyang. All Rights Reserved.

#include "Capabilities/Asset/AI/NexusCreateAssetBlackboardCapability.h"
#include "Utils/NexusCapabilityResultBuilder.h"
#include "NexusCapabilityRegistry.h"
#include "NexusMcpSchemaBuilder.h"
#include "Utils/NexusAssetUtils.h"
#include "BehaviorTree/BlackboardData.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "NexusMcpTool.h"

void FCreateAssetBlackboardCapability::BuildDefinition(FNexusCapabilityDefinition& Out) const
{
	Out.Name = TEXT("create_asset_blackboard");
	Out.Description = TEXT("创建无键 BB。用 manage BT 的 set_blackboard 关联。");
	Out.InputSchema = FNexusSchema::Object()
		.Prop(TEXT("assetPath"), FNexusSchema::Str(TEXT("BlackboardData 包路径，如 '/Game/AI/BB_Enemy'")))
		.Required({ TEXT("assetPath") })
		.Build();
	Out.Tags = {FNexusMcpTags::Write, FNexusMcpTags::Blueprint };
	Out.ExtraSearchKeywords = { TEXT("bb"), TEXT("new"), TEXT("ai"), TEXT("blackboard"), TEXT("keys") };
	Out.RelatedCapabilities = { TEXT("manage_asset_blackboard"), TEXT("get_asset_blackboard"), TEXT("create_asset_behavior_tree") };
	Out.WhenToUse = TEXT("创建空白 BB；用 manage_asset_blackboard 加键");
}

FCapabilityResult FCreateAssetBlackboardCapability::Execute(const TSharedPtr<FJsonObject>& Arguments) const
{

	return FNexusCapabilityResultBuilder::Build([&](auto& OutEntries, auto& OutTop, auto& OutError)
	{

		TSharedPtr<FJsonObject> OutEntry = MakeShared<FJsonObject>();

		if (!Arguments.IsValid() || !Arguments->HasField(TEXT("assetPath")))
		{
			OutError = TEXT("缺少 assetPath");
			return;
		}

		const FString AssetPath = Arguments->GetStringField(TEXT("assetPath"));
		const FString AssetName = FPaths::GetBaseFilename(AssetPath);

		if (LoadObject<UBlackboardData>(nullptr, *AssetPath))
		{
			OutEntry->SetStringField(TEXT("error"), FString::Printf(TEXT("BlackboardData 已存在: %s"), *AssetPath));
			OutEntries.Add(MakeShared<FJsonValueObject>(OutEntry));
			return;
		}

		UPackage* Package = CreatePackage(*AssetPath);
		if (!Package)
		{
			OutEntry->SetStringField(TEXT("error"), FString::Printf(TEXT("创建包失败: %s"), *AssetPath));
			OutEntries.Add(MakeShared<FJsonValueObject>(OutEntry));
			return;
		}

		UBlackboardData* BB = NewObject<UBlackboardData>(Package, *AssetName, RF_Public | RF_Standalone);
		if (!BB)
		{
			OutEntry->SetStringField(TEXT("error"), TEXT("BlackboardData 创建失败"));
			OutEntries.Add(MakeShared<FJsonValueObject>(OutEntry));
			return;
		}

		FNexusAssetUtils::NotifyAndSaveCreated(Package, BB, AssetPath);

		OutEntry->SetStringField(TEXT("name"),      BB->GetName());
		OutEntry->SetStringField(TEXT("path"),      BB->GetPathName());
		OutEntry->SetBoolField(TEXT("success"),     true);
		OutEntries.Add(MakeShared<FJsonValueObject>(OutEntry));
	
	});
}

REGISTER_MCP_CAPABILITY(FCreateAssetBlackboardCapability)
