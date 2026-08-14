// Copyright byteyang. All Rights Reserved.

#include "Capabilities/Asset/Paper2D/NexusManageAssetPaperFlipbookCapability.h"
#if WITH_PAPER2D
#include "NexusCapabilityRegistry.h"
#include "NexusMcpSchemaBuilder.h"
#include "Utils/NexusAssetUtils.h"
#include "Utils/NexusCapabilityResultBuilder.h"
#include "Utils/NexusJsonUtils.h"
#include "PaperFlipbook.h"
#include "PaperSprite.h"
#include "NexusMcpTool.h"

void FManageAssetPaperFlipbookCapability::BuildDefinition(FNexusCapabilityDefinition& Out) const
{
	Out.Name = TEXT("manage_asset_paper_flipbook");
	Out.SearchAssetTypes = {TEXT("PaperFlipbook")};
	Out.Description = TEXT("批量编辑 PaperFlipbook。operations[].action=add_key/remove_key/set_frames_per_second。");
	TSharedPtr<FJsonObject> OpSchema = FNexusSchema::Object()
		.Prop(TEXT("action"), FNexusSchema::Enum(TEXT("操作"),
			{ TEXT("add_key"), TEXT("remove_key"), TEXT("set_frames_per_second") }))
		.Prop(TEXT("spritePath"), FNexusSchema::Str(TEXT("PaperSprite 路径（add_key）")))
		.Prop(TEXT("frameRun"), FNexusSchema::Int(TEXT("持续帧数（add_key）"), 1))
		.Prop(TEXT("keyIndex"), FNexusSchema::Int(TEXT("关键帧索引（remove_key）")))
		.Prop(TEXT("framesPerSecond"), FNexusSchema::Num(TEXT("帧率（set_frames_per_second）")))
		.Required({ TEXT("action") })
		.Build();
	Out.InputSchema = FNexusSchema::Object()
		.Prop(TEXT("assetPath"), FNexusSchema::Str(TEXT("PaperFlipbook 资产路径")))
		.Prop(TEXT("operations"), FNexusSchema::ArrayOf(TEXT("批量操作（至少一项）"), OpSchema.ToSharedRef()))
		.Required({ TEXT("assetPath"), TEXT("operations") })
		.Build();
	Out.Tags = { FNexusMcpTags::Write, FNexusMcpTags::Data };
	Out.ExtraSearchKeywords = { TEXT("flipbook"), TEXT("fps"), TEXT("frame") };
	Out.RelatedCapabilities = { TEXT("get_asset_paper_flipbook"), TEXT("create_asset_paper_flipbook") };
}

FCapabilityResult FManageAssetPaperFlipbookCapability::Execute(const TSharedPtr<FJsonObject>& Arguments) const
{
	return FNexusCapabilityResultBuilder::Build([&](auto& OutEntries, auto& OutTop, auto& OutError)
	{
		FString AssetPath;
		if (!FNexusCapability::RequireString(Arguments, TEXT("assetPath"), AssetPath, OutEntries, {})) return;
		UPaperFlipbook* Book = FNexusAssetUtils::LoadAssetWithFallback<UPaperFlipbook>(AssetPath);
		if (!Book)
		{
			FNexusCapability::EmitError(OutEntries, {{TEXT("path"), AssetPath}},
				FString::Printf(TEXT("加载 PaperFlipbook 失败: %s"), *AssetPath));
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
			if (Action == TEXT("add_key"))
			{
				FPaperFlipbookKeyFrame Kf;
				Kf.FrameRun = Op->HasField(TEXT("frameRun")) ? static_cast<int32>(Op->GetNumberField(TEXT("frameRun"))) : 1;
				FString SpritePath;
				if (Op->TryGetStringField(TEXT("spritePath"), SpritePath) && !SpritePath.IsEmpty())
				{
					Kf.Sprite = FNexusAssetUtils::LoadAssetWithFallback<UPaperSprite>(SpritePath);
				}
				Book->KeyFrames.Add(Kf);
				bDirty = true;
				Entry->SetNumberField(TEXT("keyCount"), Book->KeyFrames.Num());
			}
			else if (Action == TEXT("remove_key"))
			{
				if (!Op->HasField(TEXT("keyIndex")))
				{
					Entry->SetStringField(TEXT("error"), TEXT("remove_key 需要 keyIndex"));
					OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
					continue;
				}
				const int32 Idx = static_cast<int32>(Op->GetNumberField(TEXT("keyIndex")));
				if (!Book->KeyFrames.IsValidIndex(Idx))
				{
					Entry->SetStringField(TEXT("error"), TEXT("keyIndex 越界"));
					OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
					continue;
				}
				Book->KeyFrames.RemoveAt(Idx);
				bDirty = true;
				Entry->SetNumberField(TEXT("keyCount"), Book->KeyFrames.Num());
			}
			else if (Action == TEXT("set_frames_per_second"))
			{
				if (!Op->HasField(TEXT("framesPerSecond")))
				{
					Entry->SetStringField(TEXT("error"), TEXT("set_frames_per_second 需要 framesPerSecond"));
					OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
					continue;
				}
				Book->SetFramesPerSecond(static_cast<float>(Op->GetNumberField(TEXT("framesPerSecond"))));
				bDirty = true;
				Entry->SetNumberField(TEXT("framesPerSecond"), Book->GetFramesPerSecond());
			}
			else
			{
				Entry->SetStringField(TEXT("error"), FString::Printf(TEXT("不支持的操作: '%s'"), *Action));
			}
			OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
		}
		if (bDirty) Book->MarkPackageDirty();
	});
}

REGISTER_MCP_CAPABILITY(FManageAssetPaperFlipbookCapability)
#endif
