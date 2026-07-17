// Copyright byteyang. All Rights Reserved.

#include "Capabilities/Asset/Level/NexusGetAssetLevelCapability.h"
#include "NexusCapabilityRegistry.h"
#include "NexusMcpSchemaBuilder.h"
#include "Utils/NexusEditorLevelUtils.h"
#include "Engine/World.h"
#include "NexusMcpTool.h"

void FGetAssetLevelCapability::BuildDefinition(FNexusCapabilityDefinition& Out) const
{
	Out.Name = TEXT("get_asset_level");
	Out.SearchAssetTypes = {TEXT("World")};
	Out.Description = TEXT("检查关卡快照。Actor 列表/WorldSettings。写用 manage_asset_level。");
	Out.InputSchema = BuildSchemaWithSections();
	Out.Tags = { FNexusMcpTags::Readonly, FNexusMcpTags::Editor };
	Out.Prerequisites = { TEXT("editor_only") };
	Out.ExtraSearchKeywords = { TEXT("level"), TEXT("map"), TEXT("umap"), TEXT("world"), TEXT("actor") };
	Out.RelatedCapabilities = { TEXT("manage_asset_level"), TEXT("search_asset"), TEXT("list_runtime_actors"), TEXT("get_asset_refs") };
	Out.WhenToUse = TEXT("读磁盘关卡；WorldSettings 写用 manage_asset_level");
}

TSharedPtr<FJsonObject> FGetAssetLevelCapability::BuildCapabilitySchema() const
{
	return FNexusSchema::Object()
		.Prop(TEXT("assetPath"),  FNexusSchema::Str(TEXT("关卡资产路径（/Game/.../*.umap 包路径）")))
		.Prop(TEXT("classFilter"), FNexusSchema::Str(TEXT("Actor 类名过滤（可选）")))
		.Prop(TEXT("nameFilter"),  FNexusSchema::Str(TEXT("Actor 名/标签过滤（可选）")))
		.Prop(TEXT("tagFilter"),   FNexusSchema::Str(TEXT("Actor Tag 精确匹配（可选）")))
		.Prop(TEXT("offset"),      FNexusSchema::Int(TEXT("actors 段分页偏移"), 0, 0))
		.Prop(TEXT("limit"),       FNexusSchema::Int(TEXT("actors 段每页条数"), 100, 1, 500))
		.Required({ TEXT("assetPath") })
		.Build();
}

TArray<FString> FGetAssetLevelCapability::GetSectionNames() const
{
	return { TEXT("actors"), TEXT("settings") };
}

TArray<FString> FGetAssetLevelCapability::GetDefaultSectionNames() const
{
	return { TEXT("actors"), TEXT("settings") };
}

bool FGetAssetLevelCapability::PrepareEntry(const TSharedPtr<FJsonObject>& Args,
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

	bool bEditorWorld = false;
	UWorld* World = FNexusEditorLevelUtils::LoadLevelWorldForRead(AssetPath, bEditorWorld, OutError);
	if (!World)
	{
		return false;
	}

	OutEntry->SetStringField(TEXT("assetPath"), AssetPath);
	OutEntry->SetStringField(TEXT("packagePath"), FNexusEditorLevelUtils::NormalizeLevelPackagePath(AssetPath));
	OutEntry->SetStringField(TEXT("worldName"), World->GetName());
	OutEntry->SetBoolField(TEXT("isEditorWorld"), bEditorWorld);

	OutTargetOpaque = static_cast<void*>(World);
	return true;
}

void FGetAssetLevelCapability::ExecuteSection(const FString&                 SectionName,
                                              const TSharedPtr<FJsonObject>& Args,
                                              void*                          TargetOpaque,
                                              TSharedPtr<FJsonObject>&       InOutDetail,
                                              FString&                       OutError) const
{
	UWorld* World = static_cast<UWorld*>(TargetOpaque);
	if (!World)
	{
		OutError = TEXT("内部错误：World 为空");
		return;
	}

	if (SectionName == TEXT("actors"))
	{
		FNexusEditorLevelUtils::AppendLevelActorsSection(World, Args, InOutDetail);
	}
	else if (SectionName == TEXT("settings"))
	{
		FNexusEditorLevelUtils::AppendLevelSettingsSection(World, InOutDetail);
	}
	else
	{
		OutError = FString::Printf(TEXT("未知 section: %s"), *SectionName);
	}
}

REGISTER_MCP_CAPABILITY(FGetAssetLevelCapability)
