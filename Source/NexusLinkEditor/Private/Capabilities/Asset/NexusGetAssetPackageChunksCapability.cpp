// Copyright byteyang. All Rights Reserved.

#include "Capabilities/Asset/NexusGetAssetPackageChunksCapability.h"

#if WITH_EDITOR

#include "Utils/NexusCapabilityResultBuilder.h"
#include "Utils/NexusArgs.h"
#include "Utils/NexusJsonUtils.h"
#include "Utils/NexusVersionCompat.h"
#include "NexusCapabilityRegistry.h"
#include "NexusMcpSchemaBuilder.h"
#include "NexusMcpTool.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetRegistry/AssetData.h"

static void NxCollectChunkIds(const FAssetData& Asset, TSet<int32>& OutIds)
{
#if NX_UE_HAS_ASSET_DATA_GET_CHUNK_IDS
	for (int32 Id : Asset.GetChunkIDs())
	{
		OutIds.Add(Id);
	}
#else
	for (int32 Id : Asset.ChunkIDs)
	{
		OutIds.Add(Id);
	}
#endif
}

static bool NxAssetHasChunk(const FAssetData& Asset, int32 ChunkId)
{
	TSet<int32> Ids;
	NxCollectChunkIds(Asset, Ids);
	return Ids.Contains(ChunkId);
}

void FNexusGetAssetPackageChunksCapability::BuildDefinition(FNexusCapabilityDefinition& Out) const
{
	Out.Name = TEXT("GetAssetPackageChunks");
	Out.Description = TEXT("Read AssetRegistry pak chunk ids for one package, or list packages in a chunk under PathFilter.");
	Out.InputSchema = FNexusSchema::Object()
		.Prop(TEXT("AssetPath"), FNexusSchema::Str(TEXT("Asset or package path. Returns that package's chunk ids.")))
		.Prop(TEXT("ChunkId"), FNexusSchema::Int(TEXT("When set without AssetPath, list packages in this chunk. PathFilter is required.")))
		.Prop(TEXT("PathFilter"), FNexusSchema::Str(TEXT("Package path prefix required when listing by ChunkId")))
		.Prop(TEXT("Offset"), FNexusSchema::Int(TEXT("Pagination offset"), 0, 0))
		.Prop(TEXT("Limit"), FNexusSchema::Int(TEXT("Max packages per page"), 100, 1, 500))
		.Build();
	Out.Tags = { FNexusMcpTags::Readonly, FNexusMcpTags::Editor };
	Out.ExtraSearchKeywords = { TEXT("chunk"), TEXT("pak"), TEXT("package"), TEXT("streaming") };
	Out.RelatedCapabilities = { TEXT("SearchAsset"), TEXT("GetAssetRefs") };
	Out.WhenToUse = TEXT("See which pak chunk a package belongs to");
}

FCapabilityResult FNexusGetAssetPackageChunksCapability::Execute(const TSharedPtr<FJsonObject>& Arguments) const
{
	const FNexusArgs A(Arguments);
	FString AssetPath = A.Str(TEXT("AssetPath"));
	const bool bHasChunk = Arguments.IsValid() && Arguments->HasField(TEXT("ChunkId"));
	const int32 ChunkId = bHasChunk ? static_cast<int32>(A.Num(TEXT("ChunkId"))) : INDEX_NONE;
	const FString PathFilter = A.Str(TEXT("PathFilter"));
	int32 Offset = 0;
	int32 Limit = 100;
	FNexusJsonUtils::ParseOffsetLimit(Arguments, Offset, Limit, 100, 500);

	if (AssetPath.IsEmpty() && !bHasChunk)
	{
		return FCapabilityResult::MakeArgInvalid(TEXT("GetAssetPackageChunks requires AssetPath or ChunkId"));
	}
	if (AssetPath.IsEmpty() && PathFilter.IsEmpty())
	{
		return FCapabilityResult::MakeArgInvalid(TEXT("Listing by ChunkId requires PathFilter"));
	}

	return FNexusCapabilityResultBuilder::Build([&](auto& OutEntries, auto& OutTop, auto& OutError)
	{
		IAssetRegistry& Registry = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry")).Get();
		if (!AssetPath.IsEmpty())
		{
			int32 DotIdx = INDEX_NONE;
			if (AssetPath.FindLastChar(TEXT('.'), DotIdx))
			{
				AssetPath = AssetPath.Left(DotIdx);
			}
			TArray<FAssetData> Assets;
			Registry.GetAssetsByPackageName(FName(*AssetPath), Assets);
			if (Assets.Num() == 0)
			{
				OutError = FString::Printf(TEXT("Asset not found: %s"), *AssetPath);
				return;
			}
			TSet<int32> Ids;
			for (const FAssetData& Asset : Assets)
			{
				NxCollectChunkIds(Asset, Ids);
			}
			TArray<int32> Sorted = Ids.Array();
			Sorted.Sort();
			TArray<TSharedPtr<FJsonValue>> IdJson;
			for (int32 Id : Sorted)
			{
				IdJson.Add(MakeShared<FJsonValueNumber>(Id));
			}
			TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
			Entry->SetStringField(TEXT("PackagePath"), AssetPath);
			Entry->SetArrayField(TEXT("ChunkIds"), IdJson);
			if (bHasChunk)
			{
				Entry->SetBoolField(TEXT("bInChunk"), Ids.Contains(ChunkId));
				Entry->SetNumberField(TEXT("ChunkId"), ChunkId);
			}
			OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
			return;
		}

		FARFilter Filter;
		Filter.bRecursivePaths = true;
		Filter.PackagePaths.Add(FName(*PathFilter));
		TArray<FAssetData> Assets;
		Registry.GetAssets(Filter, Assets);
		TSet<FString> Seen;
		TArray<FString> Packages;
		for (const FAssetData& Asset : Assets)
		{
			if (!NxAssetHasChunk(Asset, ChunkId))
			{
				continue;
			}
			const FString PackageName = Asset.PackageName.ToString();
			if (Seen.Contains(PackageName))
			{
				continue;
			}
			Seen.Add(PackageName);
			Packages.Add(PackageName);
		}
		Packages.Sort();
		const int32 Total = Packages.Num();
		int32 Start = 0;
		int32 End = 0;
		FNexusJsonUtils::ComputeSlice(Total, Offset, Limit, Start, End);
		TArray<TSharedPtr<FJsonValue>> Page;
		for (int32 Index = Start; Index < End; ++Index)
		{
			TSharedPtr<FJsonObject> Item = MakeShared<FJsonObject>();
			Item->SetStringField(TEXT("PackagePath"), Packages[Index]);
			Page.Add(MakeShared<FJsonValueObject>(Item));
		}
		OutTop->SetNumberField(TEXT("TotalCount"), Total);
		OutTop->SetNumberField(TEXT("Offset"), Start);
		OutTop->SetNumberField(TEXT("Limit"), Limit);
		OutTop->SetNumberField(TEXT("ChunkId"), ChunkId);
		OutTop->SetArrayField(TEXT("Packages"), Page);
	});
}

REGISTER_MCP_CAPABILITY(FNexusGetAssetPackageChunksCapability)

#endif // WITH_EDITOR
