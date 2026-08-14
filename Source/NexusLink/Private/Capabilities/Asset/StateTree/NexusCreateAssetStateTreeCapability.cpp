// Copyright byteyang. All Rights Reserved.

#include "Capabilities/Asset/StateTree/NexusCreateAssetStateTreeCapability.h"

#if WITH_STATETREE

#include "NexusCapabilityRegistry.h"
#include "NexusMcpSchemaBuilder.h"
#include "Utils/NexusAssetUtils.h"
#include "Utils/NexusCapabilityResultBuilder.h"
#include "StateTree.h"
#include "NexusMcpTool.h"
#if WITH_EDITOR
#include "StateTreeEditorData.h"
#endif

void FCreateAssetStateTreeCapability::BuildDefinition(FNexusCapabilityDefinition& Out) const
{
	Out.Name = TEXT("create_asset_state_tree");
	Out.Description = TEXT("创建空白 StateTree。UE 5.5+。");
	Out.InputSchema = FNexusSchema::Object()
		.Prop(TEXT("assetPath"), FNexusSchema::Str(TEXT("资产包路径")))
		.Required({ TEXT("assetPath") })
		.Build();
	Out.Tags = { FNexusMcpTags::Write };
	Out.ExtraSearchKeywords = { TEXT("statetree"), TEXT("state"), TEXT("ai") };
	Out.RelatedCapabilities = { TEXT("get_asset_state_tree"), TEXT("manage_asset_state_tree") };
	Out.WhenToUse = TEXT("新建 StateTree；结构用 manage_asset_state_tree");
}

FCapabilityResult FCreateAssetStateTreeCapability::Execute(const TSharedPtr<FJsonObject>& Arguments) const
{
	return FNexusCapabilityResultBuilder::Build([&](auto& OutEntries, auto& OutTop, auto& OutError)
	{
#if !WITH_EDITOR
		OutError = TEXT("create_asset_state_tree 仅在编辑器构建可用");
		return;
#else
		if (!Arguments.IsValid() || !Arguments->HasField(TEXT("assetPath")))
		{
			OutError = TEXT("缺少 assetPath");
			return;
		}
		const FString AssetPath = Arguments->GetStringField(TEXT("assetPath"));
		if (LoadObject<UStateTree>(nullptr, *AssetPath))
		{
			FNexusCapabilityResultBuilder::AddEntryError(OutEntries,
				FString::Printf(TEXT("StateTree already exists: %s"), *AssetPath));
			return;
		}
		UPackage* Package = CreatePackage(*AssetPath);
		if (!Package) { FNexusCapabilityResultBuilder::AddEntryError(OutEntries, TEXT("创建包失败")); return; }
		const FString AssetName = FPaths::GetBaseFilename(AssetPath);
		UStateTree* ST = NewObject<UStateTree>(Package, *AssetName, RF_Public | RF_Standalone);
		if (!ST) { FNexusCapabilityResultBuilder::AddEntryError(OutEntries, TEXT("创建失败")); return; }
		UStateTreeEditorData* Ed = NewObject<UStateTreeEditorData>(ST, NAME_None, RF_Transactional);
		ST->EditorData = Ed;
		FNexusAssetUtils::NotifyAndSaveCreated(Package, ST, AssetPath);
		TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
		Entry->SetStringField(TEXT("name"), ST->GetName());
		Entry->SetStringField(TEXT("path"), ST->GetPathName());
		OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
#endif
	});
}

REGISTER_MCP_CAPABILITY(FCreateAssetStateTreeCapability)

#endif
