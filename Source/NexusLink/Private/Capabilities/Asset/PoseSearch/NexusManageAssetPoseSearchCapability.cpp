// Copyright byteyang. All Rights Reserved.

#include "Capabilities/Asset/PoseSearch/NexusManageAssetPoseSearchCapability.h"

#if WITH_POSE_SEARCH

#include "Utils/NexusCapabilityResultBuilder.h"
#include "NexusCapabilityRegistry.h"
#include "NexusMcpSchemaBuilder.h"
#include "Utils/NexusAssetUtils.h"
#include "NexusMcpTool.h"
#include "PoseSearch/PoseSearchDatabase.h"
#include "PoseSearch/PoseSearchSchema.h"

void FManageAssetPoseSearchCapability::BuildDefinition(FNexusCapabilityDefinition& Out) const
{
	Out.Name        = TEXT("manage_asset_pose_search");
	Out.SearchAssetTypes = {TEXT("PoseSearchDatabase"), TEXT("PoseSearchSchema")};
	Out.Description = TEXT("管理 PoseSearchDatabase：set_schema/add_tag/remove_tag（UE 5.4+）。");
	Out.InputSchema = FNexusSchema::Object()
		.Prop(TEXT("assetPath"),  FNexusSchema::Str(TEXT("PoseSearchDatabase 资产路径")))
		.Prop(TEXT("operations"), FNexusSchema::ArrOfObj(TEXT("操作列表")))
		.Required({ TEXT("assetPath"), TEXT("operations") })
		.Build();
	Out.Tags = { FNexusMcpTags::Editor };
	Out.ExtraSearchKeywords = { TEXT("pose"), TEXT("search"), TEXT("motion"), TEXT("matching"), TEXT("schema"), TEXT("tag") };
	Out.RelatedCapabilities = { TEXT("get_asset_pose_search"), TEXT("search_asset") };
	Out.WhenToUse = TEXT("设置 PoseSearch Database 的 Schema 或修改 Tags");
}

FCapabilityResult FManageAssetPoseSearchCapability::Execute(const TSharedPtr<FJsonObject>& Arguments) const
{
	return FNexusCapabilityResultBuilder::Build([&](auto& OutEntries, auto& OutTop, auto& OutError)
	{
		FString AssetPath;
		if (!FNexusCapability::RequireString(Arguments, TEXT("assetPath"), AssetPath, OutEntries, {})) return;

		UPoseSearchDatabase* DB = FNexusAssetUtils::LoadAssetWithFallback<UPoseSearchDatabase>(AssetPath);
		if (!DB)
		{
			FNexusCapability::EmitError(OutEntries, {{TEXT("path"), AssetPath}},
				FString::Printf(TEXT("PoseSearchDatabase 未找到: %s"), *AssetPath));
			return;
		}

		const TArray<TSharedPtr<FJsonValue>>* OpsArr = nullptr;
		if (!Arguments->TryGetArrayField(TEXT("operations"), OpsArr) || !OpsArr || OpsArr->IsEmpty())
		{
			FNexusCapability::EmitError(OutEntries, {{TEXT("path"), AssetPath}},
				TEXT("operations 数组为空"));
			return;
		}

		bool bDirty = false;

		for (const TSharedPtr<FJsonValue>& Val : *OpsArr)
		{
			const TSharedPtr<FJsonObject>* OpObj = nullptr;
			if (!Val->TryGetObject(OpObj) || !OpObj) continue;

			FString Action;
			(*OpObj)->TryGetStringField(TEXT("action"), Action);

			TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
			Result->SetStringField(TEXT("path"), AssetPath);
			Result->SetStringField(TEXT("action"), Action);

			if (Action == TEXT("set_schema"))
			{
				FString SchemaPath;
				(*OpObj)->TryGetStringField(TEXT("schemaPath"), SchemaPath);
				if (SchemaPath.IsEmpty())
				{
					Result->SetStringField(TEXT("error"), TEXT("set_schema 需要 schemaPath"));
					OutEntries.Add(MakeShared<FJsonValueObject>(Result));
					continue;
				}
				UPoseSearchSchema* Schema = FNexusAssetUtils::LoadAssetWithFallback<UPoseSearchSchema>(SchemaPath);
				if (!Schema)
				{
					Result->SetStringField(TEXT("error"),
						FString::Printf(TEXT("PoseSearchSchema 未找到: %s"), *SchemaPath));
					OutEntries.Add(MakeShared<FJsonValueObject>(Result));
					continue;
				}
				DB->Schema = Schema;
				bDirty = true;
				Result->SetStringField(TEXT("schemaPath"), SchemaPath);
			}
			else if (Action == TEXT("add_tag"))
			{
				FString TagStr;
				(*OpObj)->TryGetStringField(TEXT("tag"), TagStr);
				if (TagStr.IsEmpty())
				{
					Result->SetStringField(TEXT("error"), TEXT("add_tag 需要 tag 参数"));
					OutEntries.Add(MakeShared<FJsonValueObject>(Result));
					continue;
				}
				const FName TagName(*TagStr);
				if (!DB->Tags.Contains(TagName))
				{
					DB->Tags.Add(TagName);
					bDirty = true;
				}
				Result->SetStringField(TEXT("tag"), TagStr);
			}
			else if (Action == TEXT("remove_tag"))
			{
				FString TagStr;
				(*OpObj)->TryGetStringField(TEXT("tag"), TagStr);
				const FName TagName(*TagStr);
				const int32 Removed = DB->Tags.Remove(TagName);
				if (Removed > 0) bDirty = true;
				if (Removed == 0)
					Result->SetStringField(TEXT("error"), FString::Printf(TEXT("tag '%s' 不存在"), *TagStr));
			}
			else
			{
				Result->SetStringField(TEXT("error"),
					FString::Printf(TEXT("未知 action '%s'，支持: set_schema/add_tag/remove_tag"), *Action));
			}

			OutEntries.Add(MakeShared<FJsonValueObject>(Result));
		}

		if (bDirty) DB->MarkPackageDirty();
	});
}

REGISTER_MCP_CAPABILITY(FManageAssetPoseSearchCapability)

#endif // WITH_POSE_SEARCH
