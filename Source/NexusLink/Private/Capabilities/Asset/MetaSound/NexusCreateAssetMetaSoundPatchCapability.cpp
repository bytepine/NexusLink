// Copyright byteyang. All Rights Reserved.

#include "Capabilities/Asset/MetaSound/NexusCreateAssetMetaSoundPatchCapability.h"

#if WITH_METASOUND

#include "Utils/NexusVersionCompat.h"

#if NX_UE_HAS_METASOUND_PATCH

#include "Utils/NexusCapabilityResultBuilder.h"
#include "NexusCapabilityRegistry.h"
#include "NexusMcpSchemaBuilder.h"
#include "Utils/NexusAssetUtils.h"
#include "NexusMcpTool.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "UObject/Package.h"
#include "Metasound.h"

void FCreateAssetMetaSoundPatchCapability::BuildDefinition(FNexusCapabilityDefinition& Out) const
{
	Out.Name        = TEXT("create_asset_meta_sound_patch");
	Out.Description = TEXT("创建 MetaSound Patch 资产（可复用子图，≥UE5.1）。读用 get_asset_meta_sound。");
	Out.InputSchema = FNexusSchema::Object()
		.Prop(TEXT("assetPath"), FNexusSchema::Str(TEXT("新 MetaSound Patch 资产完整路径，如 /Game/Audio/MSP_NewPatch")))
		.Required({ TEXT("assetPath") })
		.Build();
	Out.Tags = { FNexusMcpTags::Editor };
	Out.ExtraSearchKeywords = { TEXT("metasound"), TEXT("patch"), TEXT("audio"), TEXT("subgraph") };
	Out.RelatedCapabilities = { TEXT("get_asset_meta_sound"), TEXT("manage_asset_meta_sound"), TEXT("search_asset") };
	Out.WhenToUse = TEXT("新建可复用 MetaSound Patch 子图资产（≥UE5.1）");
}

FCapabilityResult FCreateAssetMetaSoundPatchCapability::Execute(const TSharedPtr<FJsonObject>& Arguments) const
{
	return FNexusCapabilityResultBuilder::Build([&](auto& OutEntries, auto& OutTop, auto& OutError)
	{
		FString FullPath;
		if (!Arguments.IsValid() || !Arguments->TryGetStringField(TEXT("assetPath"), FullPath) || FullPath.IsEmpty())
		{
			// 兼容旧字段（过渡期）
			FString PackagePath, AssetName;
			if (!FNexusCapability::RequireString(Arguments, TEXT("packagePath"), PackagePath, OutEntries, {})) return;
			if (!FNexusCapability::RequireString(Arguments, TEXT("assetName"),   AssetName,   OutEntries, {})) return;
			if (!PackagePath.EndsWith(TEXT("/")))
				PackagePath += TEXT("/");
			FullPath = PackagePath + AssetName;
		}
		const FString AssetName = FPaths::GetBaseFilename(FullPath);

		if (FNexusAssetUtils::LoadAssetWithFallback<UMetaSoundPatch>(FullPath))
		{
			TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
			Entry->SetStringField(TEXT("path"),    FullPath);
			Entry->SetStringField(TEXT("assetType"),    TEXT("MetaSoundPatch"));
			Entry->SetBoolField(TEXT("alreadyExists"),  true);
			OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
			return;
		}

		UPackage* Package = CreatePackage(*FullPath);
		if (!Package)
		{
			FNexusCapability::EmitError(OutEntries, {{TEXT("path"), FullPath}}, TEXT("无法创建 Package"));
			return;
		}

		UMetaSoundPatch* Patch = NewObject<UMetaSoundPatch>(Package, *AssetName,
			RF_Public | RF_Standalone | RF_Transactional);
		if (!Patch)
		{
			FNexusCapability::EmitError(OutEntries, {{TEXT("assetName"), AssetName}}, TEXT("NewObject<UMetaSoundPatch> 失败"));
			return;
		}

		Patch->MarkPackageDirty();
		FAssetRegistryModule::AssetCreated(Patch);
		FNexusAssetUtils::NotifyAndSaveCreated(Package, Patch, FullPath);

		TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
		Entry->SetStringField(TEXT("path"), Patch->GetPathName());
		Entry->SetStringField(TEXT("assetType"), TEXT("MetaSoundPatch"));
		Entry->SetBoolField(TEXT("created"),     true);
		OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
	});
}

REGISTER_MCP_CAPABILITY(FCreateAssetMetaSoundPatchCapability)

#else // NX_UE_HAS_METASOUND_PATCH

// UE5.0 及更低版本不支持 MetaSoundPatch，提供空实现
void FCreateAssetMetaSoundPatchCapability::BuildDefinition(FNexusCapabilityDefinition& Out) const
{
	Out.Name        = TEXT("create_asset_meta_sound_patch");
	Out.Description = TEXT("（当前引擎版本不支持 MetaSoundPatch，需要 UE5.1+）");
	Out.InputSchema = FNexusSchema::Object().Build();
	Out.Tags = { FNexusMcpTags::Editor };
}

FCapabilityResult FCreateAssetMetaSoundPatchCapability::Execute(const TSharedPtr<FJsonObject>&) const
{
	return FNexusCapabilityResultBuilder::Build([](auto& OutEntries, auto&, auto& OutError)
	{
		OutError = TEXT("create_asset_meta_sound_patch 需要 UE5.1+");
	});
}

REGISTER_MCP_CAPABILITY(FCreateAssetMetaSoundPatchCapability)

#endif // NX_UE_HAS_METASOUND_PATCH

#endif // WITH_METASOUND
