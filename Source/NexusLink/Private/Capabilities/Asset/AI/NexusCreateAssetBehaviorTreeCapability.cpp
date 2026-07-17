// Copyright byteyang. All Rights Reserved.

#include "Capabilities/Asset/AI/NexusCreateAssetBehaviorTreeCapability.h"
#include "Utils/NexusCapabilityResultBuilder.h"
#include "NexusCapabilityRegistry.h"
#include "NexusMcpSchemaBuilder.h"
#include "Utils/NexusAssetUtils.h"
#include "BehaviorTree/BehaviorTree.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "NexusMcpTool.h"

void FCreateAssetBehaviorTreeCapability::BuildDefinition(FNexusCapabilityDefinition& Out) const
{
	Out.Name = TEXT("create_asset_behavior_tree");
	Out.Description = TEXT("创建空白 BT。用 manage 的 set_blackboard 关联 BB 后填节点。");
	Out.InputSchema = FNexusSchema::Object()
		.Prop(TEXT("assetPath"), FNexusSchema::Str(TEXT("行为树包路径")))
		.Required({ TEXT("assetPath") })
		.Build();
	Out.Tags = {FNexusMcpTags::Write, FNexusMcpTags::Blueprint };
	Out.ExtraSearchKeywords = { TEXT("bt"), TEXT("new"), TEXT("behaviortree"), TEXT("ai"), TEXT("task") };
	Out.RelatedCapabilities = { TEXT("manage_asset_behavior_tree"), TEXT("create_asset_blackboard") };
	Out.WhenToUse = TEXT("创建空白 BT；无节点、未关联 BB");
}

FCapabilityResult FCreateAssetBehaviorTreeCapability::Execute(const TSharedPtr<FJsonObject>& Arguments) const
{

	return FNexusCapabilityResultBuilder::Build([&](auto& OutEntries, auto& OutTop, auto& OutError)
	{

		TSharedPtr<FJsonObject> OutEntry = MakeShared<FJsonObject>();

		if (!Arguments.IsValid() || !Arguments->HasField(TEXT("assetPath")))
		{
			OutError = TEXT("缺少 assetPath");
			return;
		}

		const FString BTPath  = Arguments->GetStringField(TEXT("assetPath"));
		const FString BTName  = FPaths::GetBaseFilename(BTPath);

		if (LoadObject<UBehaviorTree>(nullptr, *BTPath))
		{
			OutEntry->SetStringField(TEXT("error"), FString::Printf(TEXT("BehaviorTree already exists: %s"), *BTPath));
			OutEntries.Add(MakeShared<FJsonValueObject>(OutEntry));
			return;
		}

		UPackage* BTPackage = CreatePackage(*BTPath);
		if (!BTPackage) { FNexusCapabilityResultBuilder::AddEntryError(OutEntries, TEXT("创建 BehaviorTree 包失败")); return; }

		UBehaviorTree* BT = NewObject<UBehaviorTree>(BTPackage, *BTName, RF_Public | RF_Standalone);
		if (!BT) { FNexusCapabilityResultBuilder::AddEntryError(OutEntries, TEXT("BehaviorTree 创建失败")); return; }

		FNexusAssetUtils::NotifyAndSaveCreated(BTPackage, BT, BTPath);

		OutEntry->SetStringField(TEXT("name"),    BT->GetName());
		OutEntry->SetStringField(TEXT("path"),    BT->GetPathName());
		OutEntries.Add(MakeShared<FJsonValueObject>(OutEntry));
	
	});
}

REGISTER_MCP_CAPABILITY(FCreateAssetBehaviorTreeCapability)
