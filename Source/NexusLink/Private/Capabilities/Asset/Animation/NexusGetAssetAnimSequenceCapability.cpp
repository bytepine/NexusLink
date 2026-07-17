// Copyright byteyang. All Rights Reserved.

#include "Capabilities/Asset/Animation/NexusGetAssetAnimSequenceCapability.h"
#include "Utils/NexusCapabilityResultBuilder.h"
#include "NexusCapabilityRegistry.h"
#include "NexusMcpSchemaBuilder.h"
#include "Utils/NexusAssetUtils.h"
#include "Animation/AnimSequence.h"
#include "Animation/Skeleton.h"
#include "NexusMcpTool.h"

void FGetAssetAnimSequenceCapability::BuildDefinition(FNexusCapabilityDefinition& Out) const
{
	Out.Name = TEXT("get_asset_anim_sequence");
	Out.SearchAssetTypes = {TEXT("AnimSequence")};
	Out.Description = TEXT("检查 AnimSequence 快照。时长/帧率/帧数/骨骼/notifies。写用 manage_asset_anim_sequence。");
	Out.InputSchema = FNexusSchema::Object()
		.Prop(TEXT("assetPath"),  FNexusSchema::Str(TEXT("AnimSequence 资产路径")))
		.Prop(TEXT("assetPaths"), FNexusSchema::StrArr(TEXT("多个 AnimSequence 路径（批量）")))
		.Required({ TEXT("assetPath") })
		.Build();
	Out.Tags = { FNexusMcpTags::Readonly, FNexusMcpTags::Editor };
	Out.ExtraSearchKeywords = { TEXT("sequence"), TEXT("animation"), TEXT("clip"), TEXT("frame"), TEXT("skeleton") };
	Out.RelatedCapabilities = { TEXT("manage_asset_anim_sequence"), TEXT("search_asset"), TEXT("get_asset_skeleton"), TEXT("get_asset_anim_montage"), TEXT("get_asset_refs") };
	Out.WhenToUse = TEXT("读序列元数据；写用 manage_asset_anim_sequence");
}

static void CollectAnimSequencePaths(const TSharedPtr<FJsonObject>& Args, TArray<FString>& OutPaths)
{
	OutPaths.Reset();
	if (!Args.IsValid()) return;

	FString Single;
	if (Args->TryGetStringField(TEXT("assetPath"), Single) && !Single.IsEmpty())
	{
		OutPaths.Add(Single);
	}

	const TArray<TSharedPtr<FJsonValue>>* Arr = nullptr;
	if (Args->TryGetArrayField(TEXT("assetPaths"), Arr) && Arr)
	{
		for (const TSharedPtr<FJsonValue>& V : *Arr)
		{
			FString P;
			if (V.IsValid() && V->TryGetString(P) && !P.IsEmpty())
			{
				OutPaths.AddUnique(P);
			}
		}
	}
}

FCapabilityResult FGetAssetAnimSequenceCapability::Execute(const TSharedPtr<FJsonObject>& Arguments) const
{
	return FNexusCapabilityResultBuilder::Build([&](auto& OutEntries, auto& OutTop, auto& OutError)
	{
		TArray<FString> Paths;
		CollectAnimSequencePaths(Arguments, Paths);
		if (Paths.Num() == 0)
		{
			OutError = TEXT("需要 assetPath 或 assetPaths");
			return;
		}

		for (const FString& Path : Paths)
		{
			TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
			Entry->SetStringField(TEXT("path"), Path);

			UAnimSequence* Seq = FNexusAssetUtils::LoadAssetWithFallback<UAnimSequence>(Path);
			if (!Seq)
			{
				Entry->SetStringField(TEXT("error"), FString::Printf(TEXT("AnimSequence 未找到: %s"), *Path));
				OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
				continue;
			}

			Entry->SetStringField(TEXT("name"), Seq->GetName());
			Entry->SetStringField(TEXT("assetType"), TEXT("AnimSequence"));
			FNexusAssetUtils::AppendAnimSequenceMetadataFields(Seq, Entry);
			FNexusAssetUtils::AppendAnimSequenceNotifyFields(Seq, Entry);

			if (const USkeleton* Skel = Seq->GetSkeleton())
			{
				Entry->SetStringField(TEXT("skeleton"), Skel->GetPathName());
			}

			OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
		}
	});
}

REGISTER_MCP_CAPABILITY(FGetAssetAnimSequenceCapability)
