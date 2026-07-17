// Copyright byteyang. All Rights Reserved.

#include "Capabilities/Asset/PCG/NexusCreateAssetPCGGraphCapability.h"

#if WITH_PCG

#include "Utils/NexusCapabilityResultBuilder.h"
#include "NexusCapabilityRegistry.h"
#include "NexusMcpSchemaBuilder.h"
#include "Utils/NexusAssetUtils.h"
#include "NexusMcpTool.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "UObject/Package.h"
#include "PCGGraph.h"

void FCreateAssetPCGGraphCapability::BuildDefinition(FNexusCapabilityDefinition& Out) const
{
	Out.Name        = TEXT("create_asset_pcg_graph");
	Out.Description = TEXT("创建 PCG Graph 资产。读用 get_asset_pcg_graph。");
	Out.InputSchema = FNexusSchema::Object()
		.Prop(TEXT("assetPath"), FNexusSchema::Str(TEXT("新 PCG Graph 资产完整路径，如 /Game/PCG/PCG_NewGraph")))
		.Required({ TEXT("assetPath") })
		.Build();
	Out.Tags = { FNexusMcpTags::Editor };
	Out.ExtraSearchKeywords = { TEXT("pcg"), TEXT("procedural"), TEXT("generation"), TEXT("graph") };
	Out.RelatedCapabilities = { TEXT("get_asset_pcg_graph"), TEXT("manage_asset_pcg_graph"), TEXT("search_asset") };
	Out.WhenToUse = TEXT("新建 PCG Graph 资产（UE 5.4+）");
}

FCapabilityResult FCreateAssetPCGGraphCapability::Execute(const TSharedPtr<FJsonObject>& Arguments) const
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

		if (UPCGGraph* Existing = FNexusAssetUtils::LoadAssetWithFallback<UPCGGraph>(FullPath))
		{
			TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
			Entry->SetStringField(TEXT("path"), Existing->GetPathName());
			Entry->SetBoolField(TEXT("alreadyExists"), true);
			OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
			return;
		}

		UPackage* Package = CreatePackage(*FullPath);
		if (!Package)
		{
			FNexusCapability::EmitError(OutEntries, {{TEXT("path"), FullPath}},
				TEXT("无法创建 Package"));
			return;
		}

		UPCGGraph* Graph = NewObject<UPCGGraph>(Package, *AssetName,
			RF_Public | RF_Standalone | RF_Transactional);
		if (!Graph)
		{
			FNexusCapability::EmitError(OutEntries, {{TEXT("assetName"), AssetName}},
				TEXT("NewObject<UPCGGraph> 失败"));
			return;
		}

		Graph->MarkPackageDirty();
		FAssetRegistryModule::AssetCreated(Graph);
		FNexusAssetUtils::NotifyAndSaveCreated(Graph);

		TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
		Entry->SetStringField(TEXT("path"), Graph->GetPathName());
		Entry->SetStringField(TEXT("assetType"), TEXT("PCGGraph"));
		Entry->SetBoolField(TEXT("created"), true);
		OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
	});
}

REGISTER_MCP_CAPABILITY(FCreateAssetPCGGraphCapability)

#endif // WITH_PCG
