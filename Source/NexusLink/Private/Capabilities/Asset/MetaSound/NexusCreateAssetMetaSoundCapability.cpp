// Copyright byteyang. All Rights Reserved.

#include "Capabilities/Asset/MetaSound/NexusCreateAssetMetaSoundCapability.h"

#if WITH_METASOUND

#include "Utils/NexusCapabilityResultBuilder.h"
#include "NexusCapabilityRegistry.h"
#include "NexusMcpSchemaBuilder.h"
#include "Utils/NexusAssetUtils.h"
#include "NexusMcpTool.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "UObject/Package.h"
#include "MetasoundSource.h"

void FCreateAssetMetaSoundCapability::BuildDefinition(FNexusCapabilityDefinition& Out) const
{
	Out.Name        = TEXT("create_asset_meta_sound");
	Out.Description = TEXT("创建 MetaSound Source 资产。读用 get_asset_meta_sound。");
	Out.InputSchema = FNexusSchema::Object()
		.Prop(TEXT("packagePath"), FNexusSchema::Str(TEXT("资产所在包路径，如 /Game/Audio")))
		.Prop(TEXT("assetName"),   FNexusSchema::Str(TEXT("资产名称")))
		.Required({ TEXT("packagePath"), TEXT("assetName") })
		.Build();
	Out.Tags = { FNexusMcpTags::Editor };
	Out.ExtraSearchKeywords = { TEXT("metasound"), TEXT("audio"), TEXT("sound"), TEXT("procedural") };
	Out.RelatedCapabilities = { TEXT("get_asset_meta_sound"), TEXT("manage_asset_meta_sound"), TEXT("search_asset") };
	Out.WhenToUse = TEXT("新建 MetaSound Source 资产");
}

FCapabilityResult FCreateAssetMetaSoundCapability::Execute(const TSharedPtr<FJsonObject>& Arguments) const
{
	return FNexusCapabilityResultBuilder::Build([&](auto& OutEntries, auto& OutTop, auto& OutError)
	{
		FString PackagePath, AssetName;
		if (!FNexusCapability::RequireString(Arguments, TEXT("packagePath"), PackagePath, OutEntries, {})) return;
		if (!FNexusCapability::RequireString(Arguments, TEXT("assetName"),   AssetName,   OutEntries, {})) return;

		// 规范化包路径
		if (!PackagePath.EndsWith(TEXT("/")))
			PackagePath += TEXT("/");
		const FString FullPath = PackagePath + AssetName;

		// 检查是否已存在
		if (UMetaSoundSource* Existing = FNexusAssetUtils::LoadAssetWithFallback<UMetaSoundSource>(FullPath))
		{
			TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
			Entry->SetStringField(TEXT("assetPath"), Existing->GetPathName());
			Entry->SetBoolField(TEXT("alreadyExists"), true);
			OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
			return;
		}

		UPackage* Package = CreatePackage(*FullPath);
		if (!Package)
		{
			FNexusCapability::EmitError(OutEntries, {{TEXT("packagePath"), PackagePath}},
				TEXT("无法创建 Package"));
			return;
		}

		UMetaSoundSource* Source = NewObject<UMetaSoundSource>(Package, *AssetName,
			RF_Public | RF_Standalone | RF_Transactional);
		if (!Source)
		{
			FNexusCapability::EmitError(OutEntries, {{TEXT("assetName"), AssetName}},
				TEXT("NewObject<UMetaSoundSource> 失败"));
			return;
		}

		Source->MarkPackageDirty();
		FAssetRegistryModule::AssetCreated(Source);
		FNexusAssetUtils::NotifyAndSaveCreated(Source);

		TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
		Entry->SetStringField(TEXT("assetPath"), Source->GetPathName());
		Entry->SetStringField(TEXT("assetType"), TEXT("MetaSoundSource"));
		Entry->SetBoolField(TEXT("created"), true);
		OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
	});
}

REGISTER_MCP_CAPABILITY(FCreateAssetMetaSoundCapability)

#endif // WITH_METASOUND
