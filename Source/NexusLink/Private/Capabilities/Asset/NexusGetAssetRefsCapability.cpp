// Copyright byteyang. All Rights Reserved.

#include "Capabilities/Asset/NexusGetAssetRefsCapability.h"
#include "Utils/NexusJsonUtils.h"
#include "Utils/NexusCapabilityResultBuilder.h"
#include "NexusCapabilityRegistry.h"
#include "NexusMcpSchemaBuilder.h"
#include "Utils/NexusStringMatchUtils.h"
#include "Utils/NexusVersionCompat.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetRegistry/AssetData.h"
#include "Misc/PackageName.h"
#include "NexusMcpTool.h"

static void QueryOneAssetRefsImpl(
	IAssetRegistry& Registry,
	const FString& AssetPath,
	const FString& Direction,
	bool bRecursive,
	const FString& NameFilter,
	int32 Offset,
	int32 Limit,
	TSharedPtr<FJsonObject>& OutEntry)
{
	FString PackageName = AssetPath;
	if (PackageName.Contains(TEXT(".")))
		PackageName = FPackageName::ObjectPathToPackageName(AssetPath);

	TArray<FName> RawResults;
	const FName PackageFName(*PackageName);

	// TSet 查重复，避免 TArray::Contains 的 O(n) 开销
	auto GatherRecursive = [&](bool bReferencers)
	{
		TSet<FName> Visited;
		TArray<FName> Stack;
		Stack.Add(PackageFName);
		while (Stack.Num() > 0)
		{
			FName Current = Stack.Pop();
			if (Visited.Contains(Current)) continue;
			Visited.Add(Current);
			TArray<FName> Neighbors;
			if (bReferencers) Registry.GetReferencers(Current, Neighbors);
			else              Registry.GetDependencies(Current, Neighbors);
			for (const FName& N : Neighbors)
			{
				if (!Visited.Contains(N)) { RawResults.AddUnique(N); Stack.Add(N); }
			}
		}
	};

	if (Direction == TEXT("referencers"))
	{
		if (bRecursive) GatherRecursive(true);
		else            Registry.GetReferencers(PackageFName, RawResults);
	}
	else
	{
		if (bRecursive) GatherRecursive(false);
		else            Registry.GetDependencies(PackageFName, RawResults);
	}

	TArray<FString> Filtered;
	for (const FName& Name : RawResults)
	{
		const FString PathStr = Name.ToString();
		if (PathStr == PackageName) continue;
		if (!NameFilter.IsEmpty() && !FNexusStringMatchUtils::Matches(PathStr, NameFilter)) continue;
		Filtered.Add(PathStr);
	}
	Filtered.Sort();

	const int32 Total = Filtered.Num();
	int32 Start, End; FNexusJsonUtils::ComputeSlice(Total, Offset, Limit, Start, End);

	TArray<TSharedPtr<FJsonValue>> Page;
	for (int32 i = Start; i < End; ++i)
	{
		TSharedPtr<FJsonObject> Item = MakeShared<FJsonObject>();
		Item->SetStringField(TEXT("path"), Filtered[i]);
		TArray<FAssetData> Assets;
		Registry.GetAssetsByPackageName(*Filtered[i], Assets);
		if (Assets.Num() > 0)
		{
#if NX_UE_HAS_CLASS_PATHS
			Item->SetStringField(TEXT("assetType"), Assets[0].AssetClassPath.GetAssetName().ToString());
#else
			Item->SetStringField(TEXT("assetType"), Assets[0].AssetClass.ToString());
#endif
		}
		Page.Add(MakeShared<FJsonValueObject>(Item));
	}

	OutEntry->SetNumberField(TEXT("totalCount"), Total);
	OutEntry->SetArrayField(TEXT("refs"), Page);
}

void FGetAssetRefsCapability::BuildDefinition(FNexusCapabilityDefinition& Out) const
{
	Out.Name = TEXT("get_asset_refs");
	Out.Description = TEXT("查包依赖或引用方。direction=dependencies|referencers；可选递归。");
	Out.InputSchema = FNexusSchema::Object()
		.Prop(TEXT("assetPath"),  FNexusSchema::Str(TEXT("要查询的资产路径")))
		.Prop(TEXT("direction"),  FNexusSchema::Enum(TEXT("查询方向（默认 dependencies）"), { TEXT("dependencies"), TEXT("referencers") }))
		.Prop(TEXT("recursive"),  FNexusSchema::Bool(TEXT("递归收集"), false))
		.Prop(TEXT("nameFilter"), FNexusSchema::Str(TEXT("路径子串过滤")))
		.Prop(TEXT("offset"),     FNexusSchema::Int(TEXT("分页偏移"), 0, 0))
		.Prop(TEXT("limit"),      FNexusSchema::Int(TEXT("每页最大条数"), 100, 1, 500))
		.Required({ TEXT("assetPath") })
		.Build();
	Out.Tags = {FNexusMcpTags::Readonly, FNexusMcpTags::Editor };
	Out.ExtraSearchKeywords = { TEXT("references"), TEXT("deps"), TEXT("usage"), TEXT("links"), TEXT("callers") };
	Out.RelatedCapabilities = { TEXT("search_asset") };
}

FCapabilityResult FGetAssetRefsCapability::Execute(const TSharedPtr<FJsonObject>& Arguments) const
{

	return FNexusCapabilityResultBuilder::Build([&](auto& OutEntries, auto& OutTop, auto& OutError)
	{

		FString AssetPath;
		if (!Arguments.IsValid() || !Arguments->TryGetStringField(TEXT("assetPath"), AssetPath) || AssetPath.IsEmpty())
		{
			OutError = TEXT("assetPath 为必填项");
			return;
		}

		FString Direction = TEXT("dependencies");
		Arguments->TryGetStringField(TEXT("direction"), Direction);
		Direction = Direction.ToLower();
		if (Direction != TEXT("dependencies") && Direction != TEXT("referencers"))
		{
			OutError = FString::Printf(TEXT("无效的 direction '%s'；期望 dependencies 或 referencers"), *Direction);
			return;
		}
		const bool bRecursive = Arguments->HasField(TEXT("recursive")) && Arguments->GetBoolField(TEXT("recursive"));
		FString NameFilter;
		Arguments->TryGetStringField(TEXT("nameFilter"), NameFilter);
		int32 Offset = 0, Limit = 100;
		if (Arguments->HasField(TEXT("offset")))
			Offset = FMath::Max(0, static_cast<int32>(Arguments->GetNumberField(TEXT("offset"))));
		if (Arguments->HasField(TEXT("limit")))
			Limit = FMath::Clamp(static_cast<int32>(Arguments->GetNumberField(TEXT("limit"))), 1, 500);

		IAssetRegistry& Registry = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry")).Get();

		TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
		QueryOneAssetRefsImpl(Registry, AssetPath, Direction, bRecursive, NameFilter, Offset, Limit, Entry);
		OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
	
	});
}

REGISTER_MCP_CAPABILITY(FGetAssetRefsCapability)

