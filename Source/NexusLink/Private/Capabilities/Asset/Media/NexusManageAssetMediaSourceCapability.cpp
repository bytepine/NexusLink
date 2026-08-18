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
	Out.Description = TEXT("Batch edit FileMediaSource. action=set_file_path/set_loop.");
	TSharedPtr<FJsonObject> OpSchema = FNexusSchema::Object()
		.Prop(TEXT("action"), FNexusSchema::Enum(TEXT("Action"), { TEXT("set_file_path"), TEXT("set_loop") }))
		.Prop(TEXT("mediaPath"), FNexusSchema::Str(TEXT("Media file path (set_file_path)")))
		.Prop(TEXT("loop"), FNexusSchema::Bool(TEXT("Loop flag (set_loop, reflected field)")))
		.Required({ TEXT("action") })
		.Build();
	Out.InputSchema = FNexusSchema::Object()
		.Prop(TEXT("assetPath"), FNexusSchema::Str(TEXT("MediaSource asset path")))
		.Prop(TEXT("operations"), FNexusSchema::ArrayOf(TEXT("Batch ops (at least one)"), OpSchema.ToSharedRef()))
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
				FString::Printf(TEXT("Failed to load FileMediaSource: %s"), *AssetPath));
			return;
		}

		const TArray<TSharedPtr<FJsonValue>> Ops = FNexusJsonUtils::ExtractOperations(Arguments);
		if (Ops.Num() == 0)
		{
			FNexusCapability::EmitError(OutEntries, {{TEXT("path"), AssetPath}}, TEXT("Missing or empty operations"));
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
					Entry->SetStringField(TEXT("error"), TEXT("set_file_path requires mediaPath"));
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
					Entry->SetStringField(TEXT("error"), TEXT("set_loop requires loop"));
					OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
					continue;
				}
				const bool bLoop = Op->GetBoolField(TEXT("loop"));
				FString OldVal, ActualVal, Err;
				if (!FNexusPropertyUtils::WritePropertyAndEcho(
					Source, { TEXT("Loop") }, 0, bLoop ? TEXT("True") : TEXT("False"), OldVal, ActualVal, Err))
				{
					Entry->SetStringField(TEXT("error"),
						Err.IsEmpty() ? TEXT("no Loop field") : Err);
					OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
					continue;
				}
				bDirty = true;
				Entry->SetBoolField(TEXT("loop"), bLoop);
			}
			else
			{
				Entry->SetStringField(TEXT("error"), FString::Printf(TEXT("Unsupported operation: '%s'"), *Action));
			}
			OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
		}
		if (bDirty) Source->MarkPackageDirty();
	});
}

REGISTER_MCP_CAPABILITY(FManageAssetMediaSourceCapability)
