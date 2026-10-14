// Copyright byteyang. All Rights Reserved.

#include "Capabilities/Asset/NexusSearchAssetCapability.h"
#include "Utils/NexusCapabilityResultBuilder.h"
#include "Utils/NexusJsonUtils.h"
#include "Utils/NexusArgs.h"
#include "NexusCapabilityRegistry.h"
#include "NexusMcpSchemaBuilder.h"
#include "Utils/NexusVersionCompat.h"
#include "Utils/NexusPropertyUtils.h"
#include "Utils/NexusStringMatchUtils.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetRegistry/AssetData.h"
#include "Engine/Blueprint.h"
#include "Engine/DataTable.h"
#include "Engine/DataAsset.h"
#include "Materials/Material.h"
#include "Materials/MaterialInstanceConstant.h"
#if NX_UE_HAS_STRUCT_UTILS_HEADER
#include "StructUtils/UserDefinedStruct.h"
#else
#include "Engine/UserDefinedStruct.h"
#endif
#if WITH_EDITOR
#include "WidgetBlueprint.h"
#endif
#include "Animation/AnimMontage.h"
#include "Animation/AnimBlueprint.h"
#include "BehaviorTree/BehaviorTree.h"
#include "BehaviorTree/BlackboardData.h"
#include "Engine/Texture2D.h"
#include "Engine/StaticMesh.h"
#include "Engine/SkeletalMesh.h"
#include "Animation/AnimSequence.h"
#include "Animation/Skeleton.h"
#if NX_UE_HAS_BLEND_SPACE_BASE
#include "Animation/BlendSpaceBase.h"
#endif
#include "Animation/BlendSpace.h"
#include "Animation/BlendSpace1D.h"
#include "Sound/SoundWave.h"
#include "Sound/SoundCue.h"
#include "Engine/World.h"
#include "Curves/CurveFloat.h"
#include "Curves/CurveVector.h"
#include "Curves/CurveLinearColor.h"
#include "Engine/CurveTable.h"
#include "Engine/UserDefinedEnum.h"
#include "Animation/AnimComposite.h"
#include "PhysicalMaterials/PhysicalMaterial.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Sound/SoundClass.h"
#include "Sound/SoundAttenuation.h"
#include "Sound/SoundConcurrency.h"
#include "Sound/SoundSubmix.h"
#if WITH_NIAGARA
#include "NiagaraSystem.h"
#endif
#if WITH_STATETREE
#include "StateTree.h"
#endif
#if WITH_METASOUND
#include "MetasoundSource.h"
#include "Utils/NexusVersionCompat.h"
#if NX_UE_HAS_METASOUND_PATCH
#include "Metasound.h"
#endif
#endif
#if NX_UE_HAS_DATA_LAYER_ASSET
#include "WorldPartition/DataLayer/DataLayerAsset.h"
#endif
#if WITH_PCG
#include "PCGGraph.h"
#endif
#if WITH_POSE_SEARCH
#include "PoseSearch/PoseSearchDatabase.h"
#include "PoseSearch/PoseSearchSchema.h"
#endif
#include "NexusMcpTool.h"

/** query 空白分词 AND：每个 token 须在 name/path/AssetType/ParentClass/RowStruct/ParentMaterial 中至少一处命中。 */
static bool NexusAssetEntryMatchesQuery(
	const FString& Name,
	const FString& Path,
	const FString& Type,
	const FString& ParentClass,
	const FString& RowStruct,
	const FString& ParentMaterial,
	const TArray<FString>& Tokens)
{
	for (const FString& Tok : Tokens)
	{
		if (Tok.IsEmpty())
		{
			continue;
		}
		const bool bHit = FNexusStringMatchUtils::Matches(Name, Tok)
			|| FNexusStringMatchUtils::Matches(Path, Tok)
			|| FNexusStringMatchUtils::Matches(Type, Tok)
			|| (!ParentClass.IsEmpty() && FNexusStringMatchUtils::Matches(ParentClass, Tok))
			|| (!RowStruct.IsEmpty() && FNexusStringMatchUtils::Matches(RowStruct, Tok))
			|| (!ParentMaterial.IsEmpty() && FNexusStringMatchUtils::Matches(ParentMaterial, Tok));
		if (!bHit)
		{
			return false;
		}
	}
	return true;
}

/** 将 AI 常见复数/缩写归一为内置 AssetType shortcut（输入须已 ToLower）。 */
static FString NormalizeAssetTypeShortcut(const FString& TypeLower)
{
	if (TypeLower == TEXT("blueprints"))
	{
		return TEXT("blueprint");
	}
	if (TypeLower == TEXT("Widgets"))
	{
		return TEXT("Widget");
	}
#if WITH_GAS
	if (TypeLower == TEXT("ga"))
	{
		return TEXT("gameplayability");
	}
	if (TypeLower == TEXT("ge"))
	{
		return TEXT("gameplayeffect");
	}
#endif
#if WITH_STATETREE
	if (TypeLower == TEXT("st") || TypeLower == TEXT("StateTree"))
	{
		return TEXT("statetree");
	}
#endif
	if (TypeLower == TEXT("viewmodel") || TypeLower == TEXT("mvvm"))
	{
		return TEXT("Widget");
	}
	return TypeLower;
}

/**
 * SearchAsset 同构类型分支的表项：Aliases 命中（或 AssetType=all）即按 Class 查 AssetRegistry，
 * 写入 OutType。仅覆盖「class 加 path filter 到 AddEntry」这一形状；blueprint（排除 WidgetBlueprint）、
 * blendspace（按子类分派输出类型）、GAS 三项（按 ParentClass tag 过滤蓝图）等雪花分支保留为 Execute() 内显式代码。
 */
struct FNexusSearchTypeEntry final
{
	const TCHAR*    OutType;
	UClass*         Class;
	bool            bRecursiveClasses;
	TArray<FString> Aliases;
};

// ── 三张表：分别对应 Execute() 中 blueprint 之后、blendspace 之后、GAS 之后三段同构分支 ──
// StaticClass() 在静态初始化期不可调用，故用函数内 static + 立即调用 lambda 延迟到首次调用。

static const TArray<FNexusSearchTypeEntry>& GetSearchTypeTableEarly()
{
	static const TArray<FNexusSearchTypeEntry> Table = []
	{
		TArray<FNexusSearchTypeEntry> T;
#if WITH_EDITOR
		T.Add({ TEXT("Widget"), UWidgetBlueprint::StaticClass(), false, { TEXT("Widget") } });
#endif
		T.Add({ TEXT("Struct"), UUserDefinedStruct::StaticClass(), false, { TEXT("struct") } });
		T.Add({ TEXT("DataTable"), UDataTable::StaticClass(), true, { TEXT("datatable") } });
		T.Add({ TEXT("DataAsset"), UDataAsset::StaticClass(), true, { TEXT("dataasset") } });
		T.Add({ TEXT("Material"), UMaterial::StaticClass(), false, { TEXT("Material") } });
		T.Add({ TEXT("MaterialInstance"), UMaterialInstanceConstant::StaticClass(), true, { TEXT("materialinstance") } });
		T.Add({ TEXT("AnimMontage"), UAnimMontage::StaticClass(), true, { TEXT("animmontage"), TEXT("AnimMontage") } });
		T.Add({ TEXT("AnimBlueprint"), UAnimBlueprint::StaticClass(), true, { TEXT("animblueprint"), TEXT("AnimBlueprint") } });
		T.Add({ TEXT("BehaviorTree"), UBehaviorTree::StaticClass(), true, { TEXT("behaviortree"), TEXT("BehaviorTree") } });
		T.Add({ TEXT("Blackboard"), UBlackboardData::StaticClass(), true, { TEXT("Blackboard") } });
		T.Add({ TEXT("Texture2D"), UTexture2D::StaticClass(), true, { TEXT("texture2d"), TEXT("Texture") } });
		T.Add({ TEXT("StaticMesh"), UStaticMesh::StaticClass(), true, { TEXT("staticmesh"), TEXT("StaticMesh") } });
		T.Add({ TEXT("SkeletalMesh"), USkeletalMesh::StaticClass(), true, { TEXT("skeletalmesh"), TEXT("SkeletalMesh") } });
		T.Add({ TEXT("AnimSequence"), UAnimSequence::StaticClass(), true, { TEXT("animsequence"), TEXT("AnimSequence") } });
		return T;
	}();
	return Table;
}

static const TArray<FNexusSearchTypeEntry>& GetSearchTypeTableMid()
{
	static const TArray<FNexusSearchTypeEntry> Table = []
	{
		TArray<FNexusSearchTypeEntry> T;
		T.Add({ TEXT("Skeleton"), USkeleton::StaticClass(), true, { TEXT("Skeleton") } });
		T.Add({ TEXT("SoundWave"), USoundWave::StaticClass(), true, { TEXT("soundwave"), TEXT("SoundWave") } });
		T.Add({ TEXT("SoundCue"), USoundCue::StaticClass(), true, { TEXT("soundcue"), TEXT("SoundCue") } });
		T.Add({ TEXT("CurveFloat"), UCurveFloat::StaticClass(), true, { TEXT("curvefloat"), TEXT("CurveFloat"), TEXT("curve") } });
		T.Add({ TEXT("CurveVector"), UCurveVector::StaticClass(), true, { TEXT("curvevector"), TEXT("CurveVector") } });
		T.Add({ TEXT("CurveLinearColor"), UCurveLinearColor::StaticClass(), true, { TEXT("curvelinearcolor"), TEXT("CurveLinearColor") } });
		T.Add({ TEXT("CurveTable"), UCurveTable::StaticClass(), true, { TEXT("curvetable"), TEXT("CurveTable") } });
		T.Add({ TEXT("UserDefinedEnum"), UUserDefinedEnum::StaticClass(), true, { TEXT("userdefinedenum"), TEXT("enum"), TEXT("UserDefinedEnum") } });
		T.Add({ TEXT("AnimComposite"), UAnimComposite::StaticClass(), true, { TEXT("animcomposite"), TEXT("AnimComposite") } });
		T.Add({ TEXT("PhysicalMaterial"), UPhysicalMaterial::StaticClass(), true, { TEXT("physicalmaterial"), TEXT("PhysicalMaterial") } });
		T.Add({ TEXT("TextureRenderTarget2D"), UTextureRenderTarget2D::StaticClass(), true, { TEXT("rendertarget"), TEXT("RenderTarget"), TEXT("texturerendertarget2d") } });
		T.Add({ TEXT("SoundClass"), USoundClass::StaticClass(), true, { TEXT("soundclass"), TEXT("SoundClass") } });
		T.Add({ TEXT("SoundAttenuation"), USoundAttenuation::StaticClass(), true, { TEXT("soundattenuation"), TEXT("SoundAttenuation") } });
		T.Add({ TEXT("SoundConcurrency"), USoundConcurrency::StaticClass(), true, { TEXT("soundconcurrency"), TEXT("SoundConcurrency") } });
		T.Add({ TEXT("SoundSubmix"), USoundSubmix::StaticClass(), true, { TEXT("soundsubmix"), TEXT("SoundSubmix") } });
		T.Add({ TEXT("World"), UWorld::StaticClass(), true, { TEXT("World"), TEXT("Level"), TEXT("map") } });
#if WITH_NIAGARA
		T.Add({ TEXT("NiagaraSystem"), UNiagaraSystem::StaticClass(), true, { TEXT("niagarasystem"), TEXT("NiagaraSystem") } });
#endif
#if WITH_STATETREE
		T.Add({ TEXT("StateTree"), UStateTree::StaticClass(), true, { TEXT("statetree"), TEXT("StateTree") } });
#endif
#if WITH_METASOUND
		T.Add({ TEXT("MetaSoundSource"), UMetaSoundSource::StaticClass(), true, { TEXT("metasoundsource"), TEXT("MetaSoundSource"), TEXT("metasound") } });
#if NX_UE_HAS_METASOUND_PATCH
		T.Add({ TEXT("MetaSoundPatch"), UMetaSoundPatch::StaticClass(), true, { TEXT("metasoundpatch"), TEXT("MetaSoundPatch") } });
#endif
#endif
#if WITH_PCG
		T.Add({ TEXT("PCGGraph"), UPCGGraph::StaticClass(), true, { TEXT("pcggraph"), TEXT("PCGGraph"), TEXT("pcg") } });
#endif
#if WITH_POSE_SEARCH
		T.Add({ TEXT("PoseSearchDatabase"), UPoseSearchDatabase::StaticClass(), true, { TEXT("posesearchdatabase"), TEXT("PoseSearchDatabase"), TEXT("posesearch") } });
		T.Add({ TEXT("PoseSearchSchema"), UPoseSearchSchema::StaticClass(), true, { TEXT("posesearchschema"), TEXT("PoseSearchSchema") } });
#endif
		return T;
	}();
	return Table;
}

static const TArray<FNexusSearchTypeEntry>& GetSearchTypeTableLate()
{
	static const TArray<FNexusSearchTypeEntry> Table = []
	{
		TArray<FNexusSearchTypeEntry> T;
#if NX_UE_HAS_DATA_LAYER_ASSET
		T.Add({ TEXT("DataLayerAsset"), UDataLayerAsset::StaticClass(), true, { TEXT("datalayerasset"), TEXT("DataLayerAsset"), TEXT("datalayer") } });
#endif
		return T;
	}();
	return Table;
}

/** TypeLower 是否命中内置 shortcut（blueprint/blendspace/GAS 三项等雪花 + 三张同构表）；用于 UClass 动态回退前的判定。 */
static bool IsKnownSearchAssetType(const FString& TypeLower)
{
	if (TypeLower == TEXT("blueprint")) return true;
	if (TypeLower == TEXT("blendspace") || TypeLower == TEXT("BlendSpace")) return true;
#if WITH_GAS
	if (TypeLower == TEXT("gameplayability") || TypeLower == TEXT("GameplayAbility")) return true;
	if (TypeLower == TEXT("gameplayeffect") || TypeLower == TEXT("GameplayEffect")) return true;
	if (TypeLower == TEXT("attributeset") || TypeLower == TEXT("AttributeSet")) return true;
#endif
	for (const TArray<FNexusSearchTypeEntry>* Table : { &GetSearchTypeTableEarly(), &GetSearchTypeTableMid(), &GetSearchTypeTableLate() })
	{
		for (const FNexusSearchTypeEntry& Entry : *Table)
		{
			if (Entry.Aliases.Contains(TypeLower)) return true;
		}
	}
	return false;
}

void FNexusSearchAssetCapability::BuildDefinition(FNexusCapabilityDefinition& Out) const
{
	Out.Name = TEXT("SearchAsset");
	Out.Description = TEXT("Find asset paths. Call first; set AssetType+PathFilter. Returns assets + RecommendedGet/Manage.");
	Out.InputSchema = FNexusSchema::Object()
		.Prop(TEXT("AssetType"),  FNexusSchema::Str(TEXT("Blueprint/Widget/Material/AnimSequence/… or UClass; avoid all on large projects"), TEXT("Blueprint")))
		.Prop(TEXT("PathFilter"), FNexusSchema::Str(TEXT("Feature path prefix (avoid bare /Game/ on large projects)"), TEXT("/Game/Feature/")))
		.Prop(TEXT("Query"),      FNexusSchema::Str(TEXT("Token AND match; matches name/path/tags"), TEXT("")))
		.Prop(TEXT("NameFilter"), FNexusSchema::Str(TEXT("Asset name filter")))
		.Prop(TEXT("Offset"),     FNexusSchema::Int(TEXT("Pagination offset"), 0, 0))
		.Prop(TEXT("Limit"),      FNexusSchema::Int(TEXT("Max items per page"), 100, 1, 500))
		.Build();
	Out.Tags = {FNexusMcpTags::Readonly, FNexusMcpTags::Editor };
	Out.ExtraSearchKeywords = { TEXT("Content"), TEXT("browse"), TEXT("registry"), TEXT("scan"), TEXT("filter") };
	Out.RelatedCapabilities = { TEXT("GetAssetBlueprint"), TEXT("GetAssetRefs") };
}

FCapabilityResult FNexusSearchAssetCapability::Execute(const TSharedPtr<FJsonObject>& Arguments) const
{
	FString AssetType  = TEXT("All");
	FString PathFilter = TEXT("/Game/");
	FString NameFilter;
	FString Query;

	if (Arguments.IsValid())
	{
		const FNexusArgs CapArgs(Arguments);
		if (Arguments->HasField(TEXT("AssetType")))  AssetType  = CapArgs.Str(TEXT("AssetType"));
		if (Arguments->HasField(TEXT("PathFilter"))) PathFilter = CapArgs.Str(TEXT("PathFilter"));
		if (Arguments->HasField(TEXT("NameFilter"))) NameFilter = CapArgs.Str(TEXT("NameFilter"));
		if (Arguments->HasField(TEXT("Query")))      Query      = CapArgs.Str(TEXT("Query"));
	}

	AssetType.TrimStartAndEndInline();
	PathFilter.TrimStartAndEndInline();
	NameFilter.TrimStartAndEndInline();
	Query.TrimStartAndEndInline();

	const FString TypeLower = NormalizeAssetTypeShortcut(AssetType.ToLower());
	const bool bIsAllType = TypeLower.IsEmpty() || TypeLower == TEXT("All");
	const bool bBroadPath = PathFilter.IsEmpty()
		|| PathFilter == TEXT("/Game")
		|| PathFilter == TEXT("/Game/");
	const bool bNoTextFilter = NameFilter.IsEmpty() && Query.IsEmpty();

	if (bIsAllType && bBroadPath && bNoTextFilter)
	{
		return FCapabilityResult::MakeArgInvalid(
			TEXT("Overly broad search: AssetType=all with PathFilter=/Game/ scans the entire project. Specify AssetType (Widget/Blueprint/...) and a feature-level PathFilter (e.g. /Game/Feature/MyModule/), or add NameFilter/query."));
	}

	return FNexusCapabilityResultBuilder::Build([&](auto& OutEntries, auto& OutTop, auto& OutError)
	{
		const FNexusArgs CapArgs(Arguments);
		(void)OutEntries;

		int32 Offset = 0;
		int32 Limit  = 100;

		if (Arguments.IsValid())
		{
			if (Arguments->HasField(TEXT("Offset"))) Offset = FMath::Max(0, static_cast<int32>(CapArgs.Num(TEXT("Offset"))));
			if (Arguments->HasField(TEXT("Limit")))  Limit  = FMath::Clamp(static_cast<int32>(CapArgs.Num(TEXT("Limit"))), 1, 500);
		}

		IAssetRegistry& Registry = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry")).Get();

		struct FEntry { FString Name; FString Path; FString Type; FString ParentClass; FString RowStruct; FString ParentMaterial; };
		TArray<FEntry> AllEntries;
		TSet<FString> SeenPaths;

		auto AddEntry = [&](const FAssetData& A, const FString& Type)
		{
			const FString Pkg = A.PackageName.ToString();
			if (SeenPaths.Contains(Pkg)) return;
			SeenPaths.Add(Pkg);
			FEntry E;
			E.Name = A.AssetName.ToString();
			E.Path = Pkg;
			E.Type = Type;
			A.GetTagValue(TEXT("ParentClass"), E.ParentClass);
			if (Type == TEXT("DataTable")) A.GetTagValue(TEXT("RowStructure"), E.RowStruct);
			if (Type == TEXT("MaterialInstance")) A.GetTagValue(TEXT("Parent"), E.ParentMaterial);
			AllEntries.Add(E);
		};

		const bool bIsAll = (TypeLower == TEXT("All"));

		/** 表驱动分支执行：命中 Aliases（或 AssetType=all）时按 Entry.Class 查 AssetRegistry 写入 AddEntry。 */
		auto RunTypeEntry = [&](const FNexusSearchTypeEntry& Entry)
		{
			if (!Entry.Class) return;
			if (!bIsAll && !Entry.Aliases.Contains(TypeLower)) return;
			FARFilter Filter;
			NEXUS_FILTER_ADD_CLASS(Filter, Entry.Class);
			Filter.PackagePaths.Add(FName(*PathFilter));
			Filter.bRecursivePaths = true;
			Filter.bRecursiveClasses = Entry.bRecursiveClasses;
			TArray<FAssetData> Assets;
			Registry.GetAssets(Filter, Assets);
			for (const FAssetData& A : Assets) AddEntry(A, Entry.OutType);
		};

		// Blueprint 与 GAS 共用一次查询。all 模式下先记成 Blueprint，后续 GAS 命中被 SeenPaths 丢掉，语义不变。
		TArray<FAssetData> BlueprintAssets;
		bool bBlueprintAssetsLoaded = false;
		auto EnsureBlueprintAssets = [&]()
		{
			if (bBlueprintAssetsLoaded)
			{
				return;
			}
			bBlueprintAssetsLoaded = true;
			FARFilter Filter;
			NEXUS_FILTER_ADD_CLASS(Filter, UBlueprint::StaticClass());
			Filter.PackagePaths.Add(FName(*PathFilter));
			Filter.bRecursivePaths = true;
			Filter.bRecursiveClasses = true;
			Registry.GetAssets(Filter, BlueprintAssets);
		};

		if (bIsAll || TypeLower == TEXT("blueprint"))
		{
			EnsureBlueprintAssets();
			for (const FAssetData& A : BlueprintAssets)
			{
				if (NEXUS_ASSET_CLASS_NAME(A) == TEXT("WidgetBlueprint")) continue;
				AddEntry(A, TEXT("Blueprint"));
			}
		}

		// widget..animsequence：同构分支表驱动
		for (const FNexusSearchTypeEntry& Entry : GetSearchTypeTableEarly())
		{
			RunTypeEntry(Entry);
		}

		if (bIsAll || TypeLower == TEXT("blendspace") || TypeLower == TEXT("BlendSpace"))
		{
			FARFilter Filter;
			NEXUS_FILTER_ADD_CLASS(Filter, UBlendSpace::StaticClass());
			Filter.PackagePaths.Add(FName(*PathFilter));
			Filter.bRecursivePaths = true;
			Filter.bRecursiveClasses = true;
			TArray<FAssetData> Assets;
			Registry.GetAssets(Filter, Assets);
			for (const FAssetData& A : Assets)
			{
				const bool bIsBlendSpace1D = A.GetClass() && A.GetClass()->IsChildOf(UBlendSpace1D::StaticClass());
				AddEntry(A, bIsBlendSpace1D ? TEXT("BlendSpace1D") : TEXT("BlendSpace"));
			}
		}

		// skeleton..posesearchschema：同构分支表驱动（含可选模块守卫）
		for (const FNexusSearchTypeEntry& Entry : GetSearchTypeTableMid())
		{
			RunTypeEntry(Entry);
		}

#if WITH_GAS
		auto AddGasBlueprintEntries = [&](const TCHAR* ParentSubstr, const TCHAR* OutType)
		{
			EnsureBlueprintAssets();
			for (const FAssetData& A : BlueprintAssets)
			{
				FString ParentClass;
				if (!A.GetTagValue(TEXT("ParentClass"), ParentClass)) continue;
				if (!ParentClass.Contains(ParentSubstr)) continue;
				AddEntry(A, OutType);
			}
		};

		if (bIsAll || TypeLower == TEXT("gameplayability") || TypeLower == TEXT("GameplayAbility"))
		{
			AddGasBlueprintEntries(TEXT("GameplayAbility"), TEXT("GameplayAbility"));
		}
		if (bIsAll || TypeLower == TEXT("gameplayeffect") || TypeLower == TEXT("GameplayEffect"))
		{
			AddGasBlueprintEntries(TEXT("GameplayEffect"), TEXT("GameplayEffect"));
		}
		if (bIsAll || TypeLower == TEXT("attributeset") || TypeLower == TEXT("AttributeSet"))
		{
			AddGasBlueprintEntries(TEXT("AttributeSet"), TEXT("AttributeSet"));
		}
#endif

		// datalayerasset：表驱动（唯一项，含可选模块守卫）
		for (const FNexusSearchTypeEntry& Entry : GetSearchTypeTableLate())
		{
			RunTypeEntry(Entry);
		}

		if (!bIsAll && !IsKnownSearchAssetType(TypeLower))
		{
	#if NX_UE_HAS_FIND_FIRST_OBJECT
			UClass* TargetClass = FindFirstObject<UClass>(*AssetType, EFindFirstObjectOptions::NativeFirst);
	#else
			UClass* TargetClass = FindObject<UClass>(ANY_PACKAGE, *AssetType);
	#endif
			if (TargetClass)
			{
				FARFilter Filter;
				NEXUS_FILTER_ADD_CLASS(Filter, TargetClass);
				Filter.PackagePaths.Add(FName(*PathFilter));
				Filter.bRecursivePaths = true;
				Filter.bRecursiveClasses = true;
				TArray<FAssetData> Assets;
				Registry.GetAssets(Filter, Assets);
				for (const FAssetData& A : Assets) AddEntry(A, AssetType);
			}
		else
		{
			OutError = FString::Printf(
				TEXT("Unknown AssetType '%s'. Use a UClass name (e.g. AnimSequence, SkeletalMesh) or a shortcut (Blueprint/Widget/Struct/DataTable/DataAsset/Material/MaterialInstance/AnimMontage/AnimBlueprint/BehaviorTree/Blackboard/AnimSequence/SkeletalMesh/Skeleton/Texture2D/StaticMesh/GameplayAbility/…/all)."),
				*AssetType);
			return;
		}
		}

		if (!NameFilter.IsEmpty())
		{
			AllEntries = AllEntries.FilterByPredicate([&](const FEntry& E)
			{ return FNexusStringMatchUtils::Matches(E.Name, NameFilter); });
		}

		TArray<FString> QueryTokens;
		Query.TrimStartAndEndInline();
		Query.ParseIntoArrayWS(QueryTokens, nullptr, true);
		if (QueryTokens.Num() > 0)
		{
			AllEntries = AllEntries.FilterByPredicate([&](const FEntry& E)
			{
				return NexusAssetEntryMatchesQuery(
					E.Name, E.Path, E.Type, E.ParentClass, E.RowStruct, E.ParentMaterial, QueryTokens);
			});
		}

		const int32 Total = AllEntries.Num();
		int32 Start, End; FNexusJsonUtils::ComputeSlice(Total, Offset, Limit, Start, End);

		// 指定具体 AssetType（非 all）时，整页 recommended* 相同 → 提到顶层，避免逐条重复
		const bool bHoistRecommended = !bIsAll && Start < End;
		FString PageRecommendedGet, PageRecommendedManage;
		if (bHoistRecommended)
		{
			FNexusCapabilityRegistry::Get().ResolveSearchAssetRoute(
				AllEntries[Start].Type, PageRecommendedGet, PageRecommendedManage);
		}

		TArray<TSharedPtr<FJsonValue>> PageArray;
		for (int32 i = Start; i < End; ++i)
		{
			const FEntry& E = AllEntries[i];
			TSharedPtr<FJsonObject> EntryObj = MakeShared<FJsonObject>();
			EntryObj->SetStringField(TEXT("name"),      E.Name);
			EntryObj->SetStringField(TEXT("Path"),      E.Path);
			EntryObj->SetStringField(TEXT("AssetType"), E.Type);
			if (!bHoistRecommended)
			{
				// AssetType=all / 混合页：逐条保留推荐，避免顶层单一值误导
				FString RecommendedGet, RecommendedManage;
				FNexusCapabilityRegistry::Get().ResolveSearchAssetRoute(E.Type, RecommendedGet, RecommendedManage);
				if (!RecommendedGet.IsEmpty())
				{
					EntryObj->SetStringField(TEXT("RecommendedGet"), RecommendedGet);
				}
				if (!RecommendedManage.IsEmpty())
				{
					EntryObj->SetStringField(TEXT("RecommendedManage"), RecommendedManage);
				}
			}
			if (!E.ParentClass.IsEmpty())    EntryObj->SetStringField(TEXT("ParentClass"),    E.ParentClass);
			if (!E.RowStruct.IsEmpty())      EntryObj->SetStringField(TEXT("RowStruct"),      E.RowStruct);
			if (!E.ParentMaterial.IsEmpty()) EntryObj->SetStringField(TEXT("ParentMaterial"), E.ParentMaterial);
			PageArray.Add(MakeShared<FJsonValueObject>(EntryObj));
		}

		// 分页与 assets 放顶层（与旧版顶层 assets 兼容；避免 results[{assets}] 双层信封）
		OutTop->SetNumberField(TEXT("TotalCount"), Total);
		OutTop->SetNumberField(TEXT("Offset"),     Start);
		OutTop->SetNumberField(TEXT("Limit"),      Limit);
		if (bHoistRecommended)
		{
			if (!PageRecommendedGet.IsEmpty())
			{
				OutTop->SetStringField(TEXT("RecommendedGet"), PageRecommendedGet);
			}
			if (!PageRecommendedManage.IsEmpty())
			{
				OutTop->SetStringField(TEXT("RecommendedManage"), PageRecommendedManage);
			}
		}
		OutTop->SetArrayField(TEXT("Assets"), PageArray);
	
	});
}

REGISTER_MCP_CAPABILITY(FNexusSearchAssetCapability)
