// Copyright byteyang. All Rights Reserved.

#include "Capabilities/Asset/Media/NexusManageAssetMediaSourceCapability.h"
#include "NexusCapabilityRegistry.h"
#include "NexusMcpSchemaBuilder.h"
#include "Utils/NexusAssetUtils.h"
#include "Utils/NexusCapabilityResultBuilder.h"
#include "Utils/NexusJsonUtils.h"
#include "Utils/NexusPropertyUtils.h"
#include "FileMediaSource.h"
#include "NexusMcpTool.h"

void FManageAssetMediaSourceCapability::BuildDefinition(FNexusCapabilityDefinition& Out) const
{
	Out.Name = TEXT("manage_asset_media_source");
	Out.SearchAssetTypes = {TEXT("FileMediaSource"), TEXT("MediaSource")};
	Out.Description = TEXT("批量编辑 FileMediaSource。operations[].action=set_file_path/set_loop。");
	TSharedPtr<FJsonObject> OpSchema = FNexusSchema::Object()
		.Prop(TEXT("action"), FNexusSchema::Enum(TEXT("操作"), { TEXT("set_file_path"), TEXT("set_loop") }))
		.Prop(TEXT("mediaPath"), FNexusSchema::Str(TEXT("媒体文件路径（set_file_path）")))
		.Prop(TEXT("loop"), FNexusSchema::Bool(TEXT("是否循环（set_loop，反射字段）")))
		.Required({ TEXT("action") })
		.Build();
	Out.InputSchema = FNexusSchema::Object()
		.Prop(TEXT("assetPath"), FNexusSchema::Str(TEXT("MediaSource 资产路径")))
		.Prop(TEXT("operations"), FNexusSchema::ArrayOf(TEXT("批量操作（至少一项）"), OpSchema.ToSharedRef()))
		.Required({ TEXT("assetPath"), TEXT("operations") })
		.Build();
	Out.Tags = { FNexusMcpTags::Write, FNexusMcpTags::Data };
	Out.ExtraSearchKeywords = { TEXT("video"), TEXT("path"), TEXT("loop"), TEXT("media") };
	Out.RelatedCapabilities = { TEXT("get_asset_media_source"), TEXT("create_asset_media_source") };
}

FCapabilityResult FManageAssetMediaSourceCapability::Execute(const TSharedPtr<FJsonObject>& Arguments) const
{
	return FNexusCapabilityResultBuilder::Build([&](auto& OutEntries, auto& OutTop, auto& OutError)
	{
		FString AssetPath;
		if (!FNexusCapability::RequireString(Arguments, TEXT("assetPath"), AssetPath, OutEntries, {})) return;

		UFileMediaSource* Source = FNexusAssetUtils::LoadAssetWithFallback<UFileMediaSource>(AssetPath);
		if (!Source)
		{
			FNexusCapability::EmitError(OutEntries, {{TEXT("path"), AssetPath}},
				FString::Printf(TEXT("加载 FileMediaSource 失败: %s"), *AssetPath));
			return;
		}

		const TArray<TSharedPtr<FJsonValue>> Ops = FNexusJsonUtils::ExtractOperations(Arguments);
		if (Ops.Num() == 0)
		{
			FNexusCapability::EmitError(OutEntries, {{TEXT("path"), AssetPath}}, TEXT("缺少 operations 或为空"));
			return;
		}

		bool bDirty = false;
		for (const TSharedPtr<FJsonValue>& OpVal : Ops)
		{
			const TSharedPtr<FJsonObject>* OpPtr = nullptr;
			if (!OpVal.IsValid() || !OpVal->TryGetObject(OpPtr) || !OpPtr) continue;
			const TSharedPtr<FJsonObject>& Op = *OpPtr;
			FString Action;
			Op->TryGetStringField(TEXT("action"), Action);
			Action = Action.ToLower();

			TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
			Entry->SetStringField(TEXT("path"), AssetPath);
			Entry->SetStringField(TEXT("action"), Action);

			if (Action == TEXT("set_file_path"))
			{
				FString FilePath;
				if (!Op->TryGetStringField(TEXT("mediaPath"), FilePath) || FilePath.IsEmpty())
				{
					Entry->SetStringField(TEXT("error"), TEXT("set_file_path 需要 mediaPath"));
					OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
					continue;
				}
				Source->SetFilePath(FilePath);
				bDirty = true;
				Entry->SetStringField(TEXT("mediaPath"), Source->GetFilePath());
			}
			else if (Action == TEXT("set_loop"))
			{
				if (!Op->HasField(TEXT("loop")))
				{
					Entry->SetStringField(TEXT("error"), TEXT("set_loop 需要 loop"));
					OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
					continue;
				}
				const bool bLoop = Op->GetBoolField(TEXT("loop"));
				FString OldVal, ActualVal, Err;
				if (!FNexusPropertyUtils::WritePropertyAndEcho(
					Source, { TEXT("Loop") }, 0, bLoop ? TEXT("True") : TEXT("False"), OldVal, ActualVal, Err))
				{
					Entry->SetStringField(TEXT("error"),
						Err.IsEmpty() ? TEXT("无 Loop 字段") : Err);
					OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
					continue;
				}
				bDirty = true;
				Entry->SetBoolField(TEXT("loop"), bLoop);
			}
			else
			{
				Entry->SetStringField(TEXT("error"), FString::Printf(TEXT("不支持的操作: '%s'"), *Action));
			}
			OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
		}
		if (bDirty) Source->MarkPackageDirty();
	});
}

REGISTER_MCP_CAPABILITY(FManageAssetMediaSourceCapability)
