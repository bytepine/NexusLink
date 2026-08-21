// Copyright byteyang. All Rights Reserved.

#include "Capabilities/Asset/NexusGetAssetRefsCapability.h"
#include "Utils/NexusJsonUtils.h"
#include "Utils/NexusArgs.h"
#include "Utils/NexusCapabilityResultBuilder.h"
#include "NexusCapabilityRegistry.h"
#include "NexusMcpSchemaBuilder.h"
#include "Utils/NexusStringMatchUtils.h"
#include "Utils/NexusVersionCompat.h"
#include "Utils/NexusPropertyUtils.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetRegistry/AssetData.h"
#include "Misc/PackageName.h"
#include "Engine/Blueprint.h"
#include "Materials/MaterialInstanceConstant.h"
#include "NexusMcpTool.h"

struct FNexusAssetRefItem
{
	FString Path;
	FString Name;
	FString AssetType;
	FString ParentClass;
	int32 Depth = 0;
};

static FString ToPackageName(const FString& AssetPath)
{
	if (AssetPath.Contains(TEXT(".")))
		return FPackageName::ObjectPathToPackageName(AssetPath);
	return AssetPath;
}

static FString GetAssetTypeString(const FAssetData& Asset)
{
#if NX_UE_HAS_CLASS_PATHS
	return Asset.AssetClassPath.GetAssetName().ToString();
#else
	return Asset.AssetClass.ToString();
#endif
}

static void FillFromAssetData(const FAssetData& Asset, FNexusAssetRefItem& Out)
{
	Out.Path = Asset.PackageName.ToString();
	Out.Name = Asset.AssetName.ToString();
	Out.AssetType = GetAssetTypeString(Asset);
	Asset.GetTagValue(TEXT("ParentClass"), Out.ParentClass);
	if (Out.ParentClass.IsEmpty())
		Asset.GetTagValue(TEXT("Parent"), Out.ParentClass);
}

static FString NormalizeParentObjectPath(const FString& ParentTag)
{
	if (ParentTag.IsEmpty()) return FString();
	FString ObjectPath = FPackageName::ExportTextPathToObjectPath(ParentTag);
	if (ObjectPath.IsEmpty()) ObjectPath = ParentTag;
	return ObjectPath;
}

/** ParentClass/Parent 标签是否指向 TargetPackage（BP 为 AssetName_C；材质为 AssetName）。 */
static bool ParentTagMatchesPackage(const FString& ParentTag, const FString& TargetPackage, const FString& TargetAssetName)
{
	if (ParentTag.IsEmpty() || TargetPackage.IsEmpty()) return false;

	const FString ObjectPath = NormalizeParentObjectPath(ParentTag);

	// 原生类路径：/Script/Module.ClassName
	if (TargetAssetName.IsEmpty())
	{
		if (ObjectPath.Equals(TargetPackage, ESearchCase::IgnoreCase))
			return true;
		if (ParentTag.Contains(TargetPackage))
			return true;
		return false;
	}

	const FString ExpectedGen = TargetPackage + TEXT(".") + TargetAssetName + TEXT("_C");
	const FString ExpectedAsset = TargetPackage + TEXT(".") + TargetAssetName;
	if (ObjectPath.Equals(ExpectedGen, ESearchCase::IgnoreCase)
			|| ObjectPath.Equals(ExpectedAsset, ESearchCase::IgnoreCase))
	{
		return true;
	}

	// 路径写法差异兜底
	if (ParentTag.Contains(ExpectedGen) || ParentTag.Contains(ExpectedAsset))
		return true;
	return false;
}

static FString ParentTagToPackage(const FString& ParentTag)
{
	const FString ObjectPath = NormalizeParentObjectPath(ParentTag);
	if (ObjectPath.IsEmpty()) return FString();
	if (ObjectPath.StartsWith(TEXT("/Script/")))
		return ObjectPath; // 原生类保留完整路径
	if (ObjectPath.Contains(TEXT(".")))
		return FPackageName::ObjectPathToPackageName(ObjectPath);
	return ObjectPath;
}

static bool PassesAssetTypeFilter(const FString& AssetType, const FString& AssetTypeFilter)
{
	if (AssetTypeFilter.IsEmpty()) return true;
	return FNexusStringMatchUtils::Matches(AssetType, AssetTypeFilter);
}

static TSharedPtr<FJsonObject> RefItemToJson(const FNexusAssetRefItem& Item, bool bIncludeDepth)
{
	TSharedPtr<FJsonObject> Obj = MakeShared<FJsonObject>();
	Obj->SetStringField(TEXT("path"), Item.Path);
	if (!Item.Name.IsEmpty())
		Obj->SetStringField(TEXT("name"), Item.Name);
	if (!Item.AssetType.IsEmpty())
		Obj->SetStringField(TEXT("assetType"), Item.AssetType);
	if (!Item.ParentClass.IsEmpty())
		Obj->SetStringField(TEXT("parentClass"), Item.ParentClass);
	if (bIncludeDepth)
		Obj->SetNumberField(TEXT("depth"), Item.Depth);
	return Obj;
}

static bool TryGetPrimaryAsset(IAssetRegistry& Registry, const FString& PackageName, FAssetData& OutAsset)
{
	TArray<FAssetData> Assets;
	Registry.GetAssetsByPackageName(FName(*PackageName), Assets);
	if (Assets.Num() == 0) return false;
	OutAsset = Assets[0];
	return true;
}

static void CollectBlueprintLikeAssets(IAssetRegistry& Registry, TArray<FAssetData>& OutAssets)
{
	FARFilter Filter;
	NEXUS_FILTER_ADD_CLASS(Filter, UBlueprint::StaticClass());
	Filter.bRecursiveClasses = true;
	Registry.GetAssets(Filter, OutAssets);
}

static void CollectMaterialInstanceAssets(IAssetRegistry& Registry, TArray<FAssetData>& OutAssets)
{
	FARFilter Filter;
	NEXUS_FILTER_ADD_CLASS(Filter, UMaterialInstanceConstant::StaticClass());
	Filter.bRecursiveClasses = true;
	Registry.GetAssets(Filter, OutAssets);
}

/** 直接子类：ParentClass/Parent 标签精确指向目标包。 */
static void GatherDirectChildren(
		IAssetRegistry& Registry,
		const FString& TargetPackage,
		const FString& TargetAssetName,
		const FString& TargetAssetType,
		TArray<FNexusAssetRefItem>& Out)
{
	const bool bMaterialLike = TargetAssetType.Contains(TEXT("Material"))
		&& !TargetAssetType.Contains(TEXT("MaterialInstance"));

	TArray<FAssetData> Candidates;
	if (bMaterialLike)
		CollectMaterialInstanceAssets(Registry, Candidates);
	else
		CollectBlueprintLikeAssets(Registry, Candidates);

	TSet<FString> Seen;
	for (const FAssetData& A : Candidates)
	{
		const FString Pkg = A.PackageName.ToString();
		if (Pkg == TargetPackage || Seen.Contains(Pkg)) continue;

		FString ParentTag;
		if (bMaterialLike)
			A.GetTagValue(TEXT("Parent"), ParentTag);
		else
			A.GetTagValue(TEXT("ParentClass"), ParentTag);

		if (!ParentTagMatchesPackage(ParentTag, TargetPackage, TargetAssetName))
			continue;

		Seen.Add(Pkg);
		FNexusAssetRefItem Item;
		FillFromAssetData(A, Item);
		Item.Depth = 1;
		Out.Add(MoveTemp(Item));
	}
}

/** 递归子孙：BFS，按直接父链扩展。 */
static void GatherDescendants(
		IAssetRegistry& Registry,
		const FString& RootPackage,
		const FString& RootAssetName,
		const FString& RootAssetType,
		TArray<FNexusAssetRefItem>& Out)
{
	struct FNode { FString Package; FString AssetName; FString AssetType; int32 Depth; };
	TArray<FNode> Queue;
	Queue.Add({ RootPackage, RootAssetName, RootAssetType, 0 });
	TSet<FString> Visited;
	Visited.Add(RootPackage);

	while (Queue.Num() > 0)
	{
		const FNode Current = Queue[0];
		Queue.RemoveAt(0);

		TArray<FNexusAssetRefItem> Direct;
		GatherDirectChildren(Registry, Current.Package, Current.AssetName, Current.AssetType, Direct);
		for (FNexusAssetRefItem& Child : Direct)
		{
			if (Visited.Contains(Child.Path)) continue;
			Visited.Add(Child.Path);
			Child.Depth = Current.Depth + 1;
			FNode Next;
			Next.Package = Child.Path;
			Next.AssetName = Child.Name;
			Next.AssetType = Child.AssetType;
			Next.Depth = Child.Depth;
			Out.Add(Child);
			Queue.Add(MoveTemp(Next));
		}
	}
}

/** 父类/祖先链：沿 ParentClass/Parent 标签向上。 */
static void GatherAncestors(
		IAssetRegistry& Registry,
		const FString& StartPackage,
		bool bDirectOnly,
		TArray<FNexusAssetRefItem>& Out)
{
	FString CurrentPackage = StartPackage;
	TSet<FString> Visited;
	int32 Depth = 0;
	const int32 MaxHops = bDirectOnly ? 1 : 64;

	while (Depth < MaxHops)
	{
		if (Visited.Contains(CurrentPackage)) break;
		Visited.Add(CurrentPackage);

		FAssetData Asset;
		FString ParentTag;
		if (TryGetPrimaryAsset(Registry, CurrentPackage, Asset))
		{
			Asset.GetTagValue(TEXT("ParentClass"), ParentTag);
			if (ParentTag.IsEmpty())
				Asset.GetTagValue(TEXT("Parent"), ParentTag);
		}
		if (ParentTag.IsEmpty()) break;

		++Depth;
		FNexusAssetRefItem Item;
		Item.Depth = Depth;
		Item.ParentClass = ParentTag;

		const FString ParentPkg = ParentTagToPackage(ParentTag);
		Item.Path = ParentPkg;

		FAssetData ParentAsset;
		if (!ParentPkg.StartsWith(TEXT("/Script/")) && TryGetPrimaryAsset(Registry, ParentPkg, ParentAsset))
		{
			FillFromAssetData(ParentAsset, Item);
			Item.Depth = Depth;
			Item.ParentClass = ParentTag;
			Out.Add(Item);
			CurrentPackage = ParentPkg;
		}
		else
		{
			// 原生类或未登记资产：作为链终点写出
			Item.Name = FPackageName::GetShortName(NormalizeParentObjectPath(ParentTag));
			Item.AssetType = TEXT("Class");
			Out.Add(Item);
			break;
		}

		if (bDirectOnly) break;
	}
}

static void GatherPackageRefs(
		IAssetRegistry& Registry,
		const FString& PackageName,
		bool bReferencers,
		bool bRecursive,
		TArray<FNexusAssetRefItem>& Out)
{
	TArray<FName> RawResults;
	const FName PackageFName(*PackageName);

	auto GatherRecursive = [&]()
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
				if (!Visited.Contains(N))
				{
					RawResults.AddUnique(N);
					Stack.Add(N);
				}
			}
		}
	};

	if (bRecursive) GatherRecursive();
	else if (bReferencers) Registry.GetReferencers(PackageFName, RawResults);
	else Registry.GetDependencies(PackageFName, RawResults);

	for (const FName& Name : RawResults)
	{
		const FString PathStr = Name.ToString();
		if (PathStr == PackageName) continue;
		FNexusAssetRefItem Item;
		Item.Path = PathStr;
		FAssetData Asset;
		if (TryGetPrimaryAsset(Registry, PathStr, Asset))
			FillFromAssetData(Asset, Item);
		Out.Add(MoveTemp(Item));
	}
}

static void EmitPagedRefs(
		const TArray<FNexusAssetRefItem>& All,
		const FString& NameFilter,
		const FString& AssetTypeFilter,
		int32 Offset,
		int32 Limit,
		bool bIncludeDepth,
		TSharedPtr<FJsonObject>& OutEntry)
{
	TArray<FNexusAssetRefItem> Filtered;
	Filtered.Reserve(All.Num());
	for (const FNexusAssetRefItem& Item : All)
	{
		if (!NameFilter.IsEmpty() && !FNexusStringMatchUtils::Matches(Item.Path, NameFilter)
				&& !FNexusStringMatchUtils::Matches(Item.Name, NameFilter))
		{
			continue;
		}
		if (!PassesAssetTypeFilter(Item.AssetType, AssetTypeFilter))
			continue;
		Filtered.Add(Item);
	}

	Filtered.Sort([](const FNexusAssetRefItem& A, const FNexusAssetRefItem& B)
		{
			if (A.Depth != B.Depth) return A.Depth < B.Depth;
			return A.Path < B.Path;
		});

	const int32 Total = Filtered.Num();
	int32 Start, End;
	FNexusJsonUtils::ComputeSlice(Total, Offset, Limit, Start, End);

	TArray<TSharedPtr<FJsonValue>> Page;
	for (int32 i = Start; i < End; ++i)
		Page.Add(MakeShared<FJsonValueObject>(RefItemToJson(Filtered[i], bIncludeDepth)));

	OutEntry->SetNumberField(TEXT("totalCount"), Total);
	OutEntry->SetArrayField(TEXT("refs"), Page);
}

static void QueryOneAssetRefsImpl(
	IAssetRegistry& Registry,
	const FString& AssetPath,
	const FString& Direction,
	bool bRecursive,
	const FString& NameFilter,
	const FString& AssetTypeFilter,
	int32 Offset,
	int32 Limit,
	TSharedPtr<FJsonObject>& OutEntry)
{

	const FString PackageName = ToPackageName(AssetPath);
	OutEntry->SetStringField(TEXT("assetPath"), PackageName);
	OutEntry->SetStringField(TEXT("direction"), Direction);

	FAssetData TargetAsset;
	FString TargetAssetName;
	FString TargetAssetType;
	if (TryGetPrimaryAsset(Registry, PackageName, TargetAsset))
	{
		TargetAssetName = TargetAsset.AssetName.ToString();
		TargetAssetType = GetAssetTypeString(TargetAsset);
		OutEntry->SetStringField(TEXT("assetType"), TargetAssetType);
		FString OwnParent;
		TargetAsset.GetTagValue(TEXT("ParentClass"), OwnParent);
		if (OwnParent.IsEmpty())
			TargetAsset.GetTagValue(TEXT("Parent"), OwnParent);
		if (!OwnParent.IsEmpty())
			OutEntry->SetStringField(TEXT("parentClass"), OwnParent);
	}
	else
	{
		TargetAssetName = FPackageName::GetShortName(PackageName);
	}

	TArray<FNexusAssetRefItem> Items;
	bool bIncludeDepth = false;

	if (Direction == TEXT("referencers") || Direction == TEXT("dependencies"))
	{
		GatherPackageRefs(Registry, PackageName, Direction == TEXT("referencers"), bRecursive, Items);
	}
	else if (Direction == TEXT("children"))
	{
		bIncludeDepth = true;
		if (bRecursive)
			GatherDescendants(Registry, PackageName, TargetAssetName, TargetAssetType, Items);
		else
			GatherDirectChildren(Registry, PackageName, TargetAssetName, TargetAssetType, Items);
	}
	else if (Direction == TEXT("descendants"))
	{
		bIncludeDepth = true;
		GatherDescendants(Registry, PackageName, TargetAssetName, TargetAssetType, Items);
	}
	else if (Direction == TEXT("parent"))
	{
		bIncludeDepth = true;
		GatherAncestors(Registry, PackageName, /*bDirectOnly=*/true, Items);
	}
	else if (Direction == TEXT("ancestors"))
	{
		bIncludeDepth = true;
		GatherAncestors(Registry, PackageName, /*bDirectOnly=*/false, Items);
	}

	EmitPagedRefs(Items, NameFilter, AssetTypeFilter, Offset, Limit, bIncludeDepth, OutEntry);
}

void FGetAssetRefsCapability::BuildDefinition(FNexusCapabilityDefinition& Out) const
{
	Out.Name = TEXT("get_asset_refs");
	Out.Description = TEXT("Query deps/refs/inheritance. direction includes children|ancestors; filter by type.");
	Out.InputSchema = FNexusSchema::Object()
		.Prop(TEXT("assetPath"),  FNexusSchema::Str(TEXT("Asset path to query")))
		.Prop(TEXT("direction"),  FNexusSchema::Enum(
			TEXT("dependencies=deps; referencers=referencers; children=direct subclasses; descendants=all descendants; parent=direct parent; ancestors=parent chain"),
			{ TEXT("dependencies"), TEXT("referencers"), TEXT("children"), TEXT("descendants"), TEXT("parent"), TEXT("ancestors") }))
		.Prop(TEXT("recursive"),  FNexusSchema::Bool(TEXT("Recursive package deps/refs; children equivalent to descendants"), false))
		.Prop(TEXT("nameFilter"), FNexusSchema::Str(TEXT("Path or name substring filter")))
		.Prop(TEXT("assetTypeFilter"), FNexusSchema::Str(TEXT("Filter by assetType substring, e.g. Blueprint/MaterialInstance")))
		.Prop(TEXT("offset"),     FNexusSchema::Int(TEXT("Pagination offset"), 0, 0))
		.Prop(TEXT("limit"),      FNexusSchema::Int(TEXT("Max items per page"), 100, 1, 500))
		.Required({ TEXT("assetPath") })
		.Build();
	Out.Tags = { FNexusMcpTags::Readonly, FNexusMcpTags::Editor };
	Out.ExtraSearchKeywords = {
		TEXT("references"), TEXT("deps"), TEXT("usage"), TEXT("links"), TEXT("callers"),
		TEXT("inheritance"), TEXT("subclass"), TEXT("parent"), TEXT("children"), TEXT("descendants")
	};
	Out.RelatedCapabilities = { TEXT("search_asset"), TEXT("get_asset_blueprint") };
	Out.WhenToUse = TEXT("Query refs/deps or Blueprint subclass/parent chain");
}

FCapabilityResult FGetAssetRefsCapability::Execute(const TSharedPtr<FJsonObject>& Arguments) const
{
	return FNexusCapabilityResultBuilder::Build([&](auto& OutEntries, auto& OutTop, auto& OutError)
	{
		const FNexusArgs A(Arguments);
		const FString AssetPath = A.Str(TEXT("assetPath"));

		FString Direction = TEXT("dependencies");
		Direction = FNexusArgs(Arguments).Str(TEXT("direction"), Direction);
		Direction = Direction.ToLower();
		const bool bValidDirection =
			Direction == TEXT("dependencies") || Direction == TEXT("referencers")
			|| Direction == TEXT("children") || Direction == TEXT("descendants")
			|| Direction == TEXT("parent") || Direction == TEXT("ancestors");
		if (!bValidDirection)
		{
			OutError = FString::Printf(
				TEXT("Invalid direction '%s'; expected dependencies|referencers|children|descendants|parent|ancestors"),
				*Direction);
			return;
		}

		const bool bRecursive = Arguments->HasField(TEXT("recursive")) && A.Bool(TEXT("recursive"));
		FString NameFilter;
		NameFilter = FNexusArgs(Arguments).Str(TEXT("nameFilter"), NameFilter);
		FString AssetTypeFilter;
		AssetTypeFilter = FNexusArgs(Arguments).Str(TEXT("assetTypeFilter"), AssetTypeFilter);

		int32 Offset = 0, Limit = 100;
		if (Arguments->HasField(TEXT("offset")))
			Offset = FMath::Max(0, static_cast<int32>(A.Num(TEXT("offset"))));
		if (Arguments->HasField(TEXT("limit")))
			Limit = FMath::Clamp(static_cast<int32>(A.Num(TEXT("limit"))), 1, 500);

		IAssetRegistry& Registry = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry")).Get();

		TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
		QueryOneAssetRefsImpl(Registry, AssetPath, Direction, bRecursive, NameFilter, AssetTypeFilter, Offset, Limit, Entry);
		OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
	});
}

REGISTER_MCP_CAPABILITY(FGetAssetRefsCapability)
