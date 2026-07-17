// Copyright byteyang. All Rights Reserved.

#include "Capabilities/Asset/NexusSearchAssetCapability.h"
#include "Utils/NexusCapabilityResultBuilder.h"
#include "Utils/NexusJsonUtils.h"
#include "NexusCapabilityRegistry.h"
#include "NexusMcpSchemaBuilder.h"
#include "Utils/NexusVersionCompat.h"
#include "Utils/NexusPropertyUtils.h"
#include "Utils/NexusStringMatchUtils.h"
#include "Utils/NexusAssetUtils.h"
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

/** query 空白分词 AND：每个 token 须在 name/path/assetType/parentClass/rowStruct/parentMaterial 中至少一处命中。 */
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

/** 将 AI 常见复数/缩写归一为内置 assetType shortcut（输入须已 ToLower）。 */
static FString NormalizeAssetTypeShortcut(const FString& TypeLower)
{
	if (TypeLower == TEXT("blueprints"))
	{
		return TEXT("blueprint");
	}
	if (TypeLower == TEXT("widgets"))
	{
		return TEXT("widget");
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
	if (TypeLower == TEXT("st") || TypeLower == TEXT("state_tree"))
	{
		return TEXT("statetree");
	}
#endif
	if (TypeLower == TEXT("viewmodel") || TypeLower == TEXT("mvvm"))
	{
		return TEXT("widget");
	}
	return TypeLower;
}

void FSearchAssetCapability::BuildDefinition(FNexusCapabilityDefinition& Out) const
{
	Out.Name = TEXT("search_asset");
	Out.Description = TEXT("查找资产路径。必须先调；指定 assetType+pathFilter；禁止猜 /Game 路径。每条返回 assetType + recommendedGet/recommendedManage（读/写 Capability 提示）。");
	Out.InputSchema = FNexusSchema::Object()
		.Prop(TEXT("assetType"),  FNexusSchema::Str(TEXT("Blueprint/Widget/Material/AnimSequence/SkeletalMesh/Skeleton/… 或 UClass；大项目避免 all"), TEXT("Blueprint")))
		.Prop(TEXT("pathFilter"), FNexusSchema::Str(TEXT("功能级路径前缀（大项目勿用裸 /Game/）"), TEXT("/Game/Feature/")))
		.Prop(TEXT("query"),      FNexusSchema::Str(TEXT("分词 AND 匹配；匹配名称/路径/标签"), TEXT("")))
		.Prop(TEXT("nameFilter"), FNexusSchema::Str(TEXT("资产名称过滤")))
		.Prop(TEXT("offset"),     FNexusSchema::Int(TEXT("分页偏移"), 0, 0))
		.Prop(TEXT("limit"),      FNexusSchema::Int(TEXT("每页最大条数"), 100, 1, 500))
		.Build();
	Out.Tags = {FNexusMcpTags::Readonly, FNexusMcpTags::Editor };
	Out.ExtraSearchKeywords = { TEXT("content"), TEXT("browse"), TEXT("registry"), TEXT("scan"), TEXT("filter") };
	Out.RelatedCapabilities = { TEXT("get_asset_blueprint"), TEXT("get_asset_refs") };
}

FCapabilityResult FSearchAssetCapability::Execute(const TSharedPtr<FJsonObject>& Arguments) const
{
	FString AssetType  = TEXT("all");
	FString PathFilter = TEXT("/Game/");
	FString NameFilter;
	FString Query;

	if (Arguments.IsValid())
	{
		if (Arguments->HasField(TEXT("assetType")))  AssetType  = Arguments->GetStringField(TEXT("assetType"));
		if (Arguments->HasField(TEXT("pathFilter"))) PathFilter = Arguments->GetStringField(TEXT("pathFilter"));
		if (Arguments->HasField(TEXT("nameFilter"))) NameFilter = Arguments->GetStringField(TEXT("nameFilter"));
		if (Arguments->HasField(TEXT("query")))      Query      = Arguments->GetStringField(TEXT("query"));
	}

	AssetType.TrimStartAndEndInline();
	PathFilter.TrimStartAndEndInline();
	NameFilter.TrimStartAndEndInline();
	Query.TrimStartAndEndInline();

	const FString TypeLower = NormalizeAssetTypeShortcut(AssetType.ToLower());
	const bool bIsAllType = TypeLower.IsEmpty() || TypeLower == TEXT("all");
	const bool bBroadPath = PathFilter.IsEmpty()
		|| PathFilter == TEXT("/Game")
		|| PathFilter == TEXT("/Game/");
	const bool bNoTextFilter = NameFilter.IsEmpty() && Query.IsEmpty();

	if (bIsAllType && bBroadPath && bNoTextFilter)
	{
		return FCapabilityResult::MakeArgInvalid(
			TEXT("Overly broad search: assetType=all with pathFilter=/Game/ scans the entire project. Specify assetType (Widget/Blueprint/...) and a feature-level pathFilter (e.g. /Game/Feature/MyModule/), or add nameFilter/query."));
	}

	return FNexusCapabilityResultBuilder::Build([&](auto& OutEntries, auto& OutTop, auto& OutError)
	{

		TSharedPtr<FJsonObject> OutEntry = MakeShared<FJsonObject>();

		int32 Offset = 0;
		int32 Limit  = 100;

		if (Arguments.IsValid())
		{
			if (Arguments->HasField(TEXT("offset"))) Offset = FMath::Max(0, static_cast<int32>(Arguments->GetNumberField(TEXT("offset"))));
			if (Arguments->HasField(TEXT("limit")))  Limit  = FMath::Clamp(static_cast<int32>(Arguments->GetNumberField(TEXT("limit"))), 1, 500);
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

		const bool bIsAll = (TypeLower == TEXT("all"));

		if (bIsAll || TypeLower == TEXT("blueprint"))
		{
			FARFilter Filter;
			NEXUS_FILTER_ADD_CLASS(Filter, UBlueprint::StaticClass());
			Filter.PackagePaths.Add(FName(*PathFilter));
			Filter.bRecursivePaths = true;
			Filter.bRecursiveClasses = true;
			TArray<FAssetData> Assets;
			Registry.GetAssets(Filter, Assets);
			for (const FAssetData& A : Assets)
			{
				if (NEXUS_ASSET_CLASS_NAME(A) == TEXT("WidgetBlueprint")) continue;
				AddEntry(A, TEXT("Blueprint"));
			}
		}

#if WITH_EDITOR
		if (bIsAll || TypeLower == TEXT("widget"))
		{
			FARFilter Filter;
			NEXUS_FILTER_ADD_CLASS(Filter, UWidgetBlueprint::StaticClass());
			Filter.PackagePaths.Add(FName(*PathFilter));
			Filter.bRecursivePaths = true;
			TArray<FAssetData> Assets;
			Registry.GetAssets(Filter, Assets);
			for (const FAssetData& A : Assets) AddEntry(A, TEXT("Widget"));
		}
#endif

		if (bIsAll || TypeLower == TEXT("struct"))
		{
			FARFilter Filter;
			NEXUS_FILTER_ADD_CLASS(Filter, UUserDefinedStruct::StaticClass());
			Filter.PackagePaths.Add(FName(*PathFilter));
			Filter.bRecursivePaths = true;
			TArray<FAssetData> Assets;
			Registry.GetAssets(Filter, Assets);
			for (const FAssetData& A : Assets) AddEntry(A, TEXT("Struct"));
		}

		if (bIsAll || TypeLower == TEXT("datatable"))
		{
			FARFilter Filter;
			NEXUS_FILTER_ADD_CLASS(Filter, UDataTable::StaticClass());
			Filter.PackagePaths.Add(FName(*PathFilter));
			Filter.bRecursivePaths = true;
			Filter.bRecursiveClasses = true;
			TArray<FAssetData> Assets;
			Registry.GetAssets(Filter, Assets);
			for (const FAssetData& A : Assets) AddEntry(A, TEXT("DataTable"));
		}

		if (bIsAll || TypeLower == TEXT("dataasset"))
		{
			FARFilter Filter;
			NEXUS_FILTER_ADD_CLASS(Filter, UDataAsset::StaticClass());
			Filter.PackagePaths.Add(FName(*PathFilter));
			Filter.bRecursivePaths = true;
			Filter.bRecursiveClasses = true;
			TArray<FAssetData> Assets;
			Registry.GetAssets(Filter, Assets);
			for (const FAssetData& A : Assets) AddEntry(A, TEXT("DataAsset"));
		}

		if (bIsAll || TypeLower == TEXT("material"))
		{
			FARFilter Filter;
			NEXUS_FILTER_ADD_CLASS(Filter, UMaterial::StaticClass());
			Filter.PackagePaths.Add(FName(*PathFilter));
			Filter.bRecursivePaths = true;
			TArray<FAssetData> Assets;
			Registry.GetAssets(Filter, Assets);
			for (const FAssetData& A : Assets) AddEntry(A, TEXT("Material"));
		}

		if (bIsAll || TypeLower == TEXT("materialinstance"))
		{
			FARFilter Filter;
			NEXUS_FILTER_ADD_CLASS(Filter, UMaterialInstanceConstant::StaticClass());
			Filter.PackagePaths.Add(FName(*PathFilter));
			Filter.bRecursivePaths = true;
			Filter.bRecursiveClasses = true;
			TArray<FAssetData> Assets;
			Registry.GetAssets(Filter, Assets);
			for (const FAssetData& A : Assets) AddEntry(A, TEXT("MaterialInstance"));
		}

		if (bIsAll || TypeLower == TEXT("animmontage") || TypeLower == TEXT("anim_montage"))
		{
			FARFilter Filter;
			NEXUS_FILTER_ADD_CLASS(Filter, UAnimMontage::StaticClass());
			Filter.PackagePaths.Add(FName(*PathFilter));
			Filter.bRecursivePaths = true;
			Filter.bRecursiveClasses = true;
			TArray<FAssetData> Assets;
			Registry.GetAssets(Filter, Assets);
			for (const FAssetData& A : Assets) AddEntry(A, TEXT("AnimMontage"));
		}

		if (bIsAll || TypeLower == TEXT("animblueprint") || TypeLower == TEXT("anim_blueprint"))
		{
			FARFilter Filter;
			NEXUS_FILTER_ADD_CLASS(Filter, UAnimBlueprint::StaticClass());
			Filter.PackagePaths.Add(FName(*PathFilter));
			Filter.bRecursivePaths = true;
			Filter.bRecursiveClasses = true;
			TArray<FAssetData> Assets;
			Registry.GetAssets(Filter, Assets);
			for (const FAssetData& A : Assets) AddEntry(A, TEXT("AnimBlueprint"));
		}

		if (bIsAll || TypeLower == TEXT("behaviortree") || TypeLower == TEXT("behavior_tree"))
		{
			FARFilter Filter;
			NEXUS_FILTER_ADD_CLASS(Filter, UBehaviorTree::StaticClass());
			Filter.PackagePaths.Add(FName(*PathFilter));
			Filter.bRecursivePaths = true;
			Filter.bRecursiveClasses = true;
			TArray<FAssetData> Assets;
			Registry.GetAssets(Filter, Assets);
			for (const FAssetData& A : Assets) AddEntry(A, TEXT("BehaviorTree"));
		}

		if (bIsAll || TypeLower == TEXT("blackboard"))
		{
			FARFilter Filter;
			NEXUS_FILTER_ADD_CLASS(Filter, UBlackboardData::StaticClass());
			Filter.PackagePaths.Add(FName(*PathFilter));
			Filter.bRecursivePaths = true;
			Filter.bRecursiveClasses = true;
			TArray<FAssetData> Assets;
			Registry.GetAssets(Filter, Assets);
			for (const FAssetData& A : Assets) AddEntry(A, TEXT("Blackboard"));
		}

		if (bIsAll || TypeLower == TEXT("texture2d") || TypeLower == TEXT("texture"))
		{
			FARFilter Filter;
			NEXUS_FILTER_ADD_CLASS(Filter, UTexture2D::StaticClass());
			Filter.PackagePaths.Add(FName(*PathFilter));
			Filter.bRecursivePaths = true;
			Filter.bRecursiveClasses = true;
			TArray<FAssetData> Assets;
			Registry.GetAssets(Filter, Assets);
			for (const FAssetData& A : Assets) AddEntry(A, TEXT("Texture2D"));
		}

		if (bIsAll || TypeLower == TEXT("staticmesh") || TypeLower == TEXT("static_mesh"))
		{
			FARFilter Filter;
			NEXUS_FILTER_ADD_CLASS(Filter, UStaticMesh::StaticClass());
			Filter.PackagePaths.Add(FName(*PathFilter));
			Filter.bRecursivePaths = true;
			Filter.bRecursiveClasses = true;
			TArray<FAssetData> Assets;
			Registry.GetAssets(Filter, Assets);
			for (const FAssetData& A : Assets) AddEntry(A, TEXT("StaticMesh"));
		}

		if (bIsAll || TypeLower == TEXT("skeletalmesh") || TypeLower == TEXT("skeletal_mesh"))
		{
			FARFilter Filter;
			NEXUS_FILTER_ADD_CLASS(Filter, USkeletalMesh::StaticClass());
			Filter.PackagePaths.Add(FName(*PathFilter));
			Filter.bRecursivePaths = true;
			Filter.bRecursiveClasses = true;
			TArray<FAssetData> Assets;
			Registry.GetAssets(Filter, Assets);
			for (const FAssetData& A : Assets) AddEntry(A, TEXT("SkeletalMesh"));
		}

		if (bIsAll || TypeLower == TEXT("animsequence") || TypeLower == TEXT("anim_sequence"))
		{
			FARFilter Filter;
			NEXUS_FILTER_ADD_CLASS(Filter, UAnimSequence::StaticClass());
			Filter.PackagePaths.Add(FName(*PathFilter));
			Filter.bRecursivePaths = true;
			Filter.bRecursiveClasses = true;
			TArray<FAssetData> Assets;
			Registry.GetAssets(Filter, Assets);
			for (const FAssetData& A : Assets) AddEntry(A, TEXT("AnimSequence"));
		}

		if (bIsAll || TypeLower == TEXT("blendspace") || TypeLower == TEXT("blend_space"))
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
				const bool b1D = A.GetClass() && A.GetClass()->IsChildOf(UBlendSpace1D::StaticClass());
				AddEntry(A, b1D ? TEXT("BlendSpace1D") : TEXT("BlendSpace"));
			}
		}

		if (bIsAll || TypeLower == TEXT("skeleton"))
		{
			FARFilter Filter;
			NEXUS_FILTER_ADD_CLASS(Filter, USkeleton::StaticClass());
			Filter.PackagePaths.Add(FName(*PathFilter));
			Filter.bRecursivePaths = true;
			Filter.bRecursiveClasses = true;
			TArray<FAssetData> Assets;
			Registry.GetAssets(Filter, Assets);
			for (const FAssetData& A : Assets) AddEntry(A, TEXT("Skeleton"));
		}

		if (bIsAll || TypeLower == TEXT("soundwave") || TypeLower == TEXT("sound_wave"))
		{
			FARFilter Filter;
			NEXUS_FILTER_ADD_CLASS(Filter, USoundWave::StaticClass());
			Filter.PackagePaths.Add(FName(*PathFilter));
			Filter.bRecursivePaths = true;
			Filter.bRecursiveClasses = true;
			TArray<FAssetData> Assets;
			Registry.GetAssets(Filter, Assets);
			for (const FAssetData& A : Assets) AddEntry(A, TEXT("SoundWave"));
		}

		if (bIsAll || TypeLower == TEXT("soundcue") || TypeLower == TEXT("sound_cue"))
		{
			FARFilter Filter;
			NEXUS_FILTER_ADD_CLASS(Filter, USoundCue::StaticClass());
			Filter.PackagePaths.Add(FName(*PathFilter));
			Filter.bRecursivePaths = true;
			Filter.bRecursiveClasses = true;
			TArray<FAssetData> Assets;
			Registry.GetAssets(Filter, Assets);
			for (const FAssetData& A : Assets) AddEntry(A, TEXT("SoundCue"));
		}

		if (bIsAll || TypeLower == TEXT("curvefloat") || TypeLower == TEXT("curve_float") || TypeLower == TEXT("curve"))
		{
			FARFilter Filter;
			NEXUS_FILTER_ADD_CLASS(Filter, UCurveFloat::StaticClass());
			Filter.PackagePaths.Add(FName(*PathFilter));
			Filter.bRecursivePaths = true;
			Filter.bRecursiveClasses = true;
			TArray<FAssetData> Assets;
			Registry.GetAssets(Filter, Assets);
			for (const FAssetData& A : Assets) AddEntry(A, TEXT("CurveFloat"));
		}

		if (bIsAll || TypeLower == TEXT("curvevector") || TypeLower == TEXT("curve_vector"))
		{
			FARFilter Filter;
			NEXUS_FILTER_ADD_CLASS(Filter, UCurveVector::StaticClass());
			Filter.PackagePaths.Add(FName(*PathFilter));
			Filter.bRecursivePaths = true;
			Filter.bRecursiveClasses = true;
			TArray<FAssetData> Assets;
			Registry.GetAssets(Filter, Assets);
			for (const FAssetData& A : Assets) AddEntry(A, TEXT("CurveVector"));
		}

		if (bIsAll || TypeLower == TEXT("curvelinearcolor") || TypeLower == TEXT("curve_linear_color"))
		{
			FARFilter Filter;
			NEXUS_FILTER_ADD_CLASS(Filter, UCurveLinearColor::StaticClass());
			Filter.PackagePaths.Add(FName(*PathFilter));
			Filter.bRecursivePaths = true;
			Filter.bRecursiveClasses = true;
			TArray<FAssetData> Assets;
			Registry.GetAssets(Filter, Assets);
			for (const FAssetData& A : Assets) AddEntry(A, TEXT("CurveLinearColor"));
		}

		if (bIsAll || TypeLower == TEXT("curvetable") || TypeLower == TEXT("curve_table"))
		{
			FARFilter Filter;
			NEXUS_FILTER_ADD_CLASS(Filter, UCurveTable::StaticClass());
			Filter.PackagePaths.Add(FName(*PathFilter));
			Filter.bRecursivePaths = true;
			Filter.bRecursiveClasses = true;
			TArray<FAssetData> Assets;
			Registry.GetAssets(Filter, Assets);
			for (const FAssetData& A : Assets) AddEntry(A, TEXT("CurveTable"));
		}

		if (bIsAll || TypeLower == TEXT("userdefinedelnum") || TypeLower == TEXT("enum") || TypeLower == TEXT("user_defined_enum"))
		{
			FARFilter Filter;
			NEXUS_FILTER_ADD_CLASS(Filter, UUserDefinedEnum::StaticClass());
			Filter.PackagePaths.Add(FName(*PathFilter));
			Filter.bRecursivePaths = true;
			Filter.bRecursiveClasses = true;
			TArray<FAssetData> Assets;
			Registry.GetAssets(Filter, Assets);
			for (const FAssetData& A : Assets) AddEntry(A, TEXT("UserDefinedEnum"));
		}

		if (bIsAll || TypeLower == TEXT("animcomposite") || TypeLower == TEXT("anim_composite"))
		{
			FARFilter Filter;
			NEXUS_FILTER_ADD_CLASS(Filter, UAnimComposite::StaticClass());
			Filter.PackagePaths.Add(FName(*PathFilter));
			Filter.bRecursivePaths = true;
			Filter.bRecursiveClasses = true;
			TArray<FAssetData> Assets;
			Registry.GetAssets(Filter, Assets);
			for (const FAssetData& A : Assets) AddEntry(A, TEXT("AnimComposite"));
		}

		if (bIsAll || TypeLower == TEXT("physicalmaterial") || TypeLower == TEXT("physical_material"))
		{
			FARFilter Filter;
			NEXUS_FILTER_ADD_CLASS(Filter, UPhysicalMaterial::StaticClass());
			Filter.PackagePaths.Add(FName(*PathFilter));
			Filter.bRecursivePaths = true;
			Filter.bRecursiveClasses = true;
			TArray<FAssetData> Assets;
			Registry.GetAssets(Filter, Assets);
			for (const FAssetData& A : Assets) AddEntry(A, TEXT("PhysicalMaterial"));
		}

		if (bIsAll || TypeLower == TEXT("rendertarget") || TypeLower == TEXT("render_target") || TypeLower == TEXT("texturerendertarget2d"))
		{
			FARFilter Filter;
			NEXUS_FILTER_ADD_CLASS(Filter, UTextureRenderTarget2D::StaticClass());
			Filter.PackagePaths.Add(FName(*PathFilter));
			Filter.bRecursivePaths = true;
			Filter.bRecursiveClasses = true;
			TArray<FAssetData> Assets;
			Registry.GetAssets(Filter, Assets);
			for (const FAssetData& A : Assets) AddEntry(A, TEXT("TextureRenderTarget2D"));
		}

		if (bIsAll || TypeLower == TEXT("soundclass") || TypeLower == TEXT("sound_class"))
		{
			FARFilter Filter;
			NEXUS_FILTER_ADD_CLASS(Filter, USoundClass::StaticClass());
			Filter.PackagePaths.Add(FName(*PathFilter));
			Filter.bRecursivePaths = true;
			Filter.bRecursiveClasses = true;
			TArray<FAssetData> Assets;
			Registry.GetAssets(Filter, Assets);
			for (const FAssetData& A : Assets) AddEntry(A, TEXT("SoundClass"));
		}

		if (bIsAll || TypeLower == TEXT("soundattenuation") || TypeLower == TEXT("sound_attenuation"))
		{
			FARFilter Filter;
			NEXUS_FILTER_ADD_CLASS(Filter, USoundAttenuation::StaticClass());
			Filter.PackagePaths.Add(FName(*PathFilter));
			Filter.bRecursivePaths = true;
			Filter.bRecursiveClasses = true;
			TArray<FAssetData> Assets;
			Registry.GetAssets(Filter, Assets);
			for (const FAssetData& A : Assets) AddEntry(A, TEXT("SoundAttenuation"));
		}

		if (bIsAll || TypeLower == TEXT("soundconcurrency") || TypeLower == TEXT("sound_concurrency"))
		{
			FARFilter Filter;
			NEXUS_FILTER_ADD_CLASS(Filter, USoundConcurrency::StaticClass());
			Filter.PackagePaths.Add(FName(*PathFilter));
			Filter.bRecursivePaths = true;
			Filter.bRecursiveClasses = true;
			TArray<FAssetData> Assets;
			Registry.GetAssets(Filter, Assets);
			for (const FAssetData& A : Assets) AddEntry(A, TEXT("SoundConcurrency"));
		}

		if (bIsAll || TypeLower == TEXT("soundsubmix") || TypeLower == TEXT("sound_submix"))
		{
			FARFilter Filter;
			NEXUS_FILTER_ADD_CLASS(Filter, USoundSubmix::StaticClass());
			Filter.PackagePaths.Add(FName(*PathFilter));
			Filter.bRecursivePaths = true;
			Filter.bRecursiveClasses = true;
			TArray<FAssetData> Assets;
			Registry.GetAssets(Filter, Assets);
			for (const FAssetData& A : Assets) AddEntry(A, TEXT("SoundSubmix"));
		}

		if (bIsAll || TypeLower == TEXT("world") || TypeLower == TEXT("level") || TypeLower == TEXT("map"))
		{
			FARFilter Filter;
			NEXUS_FILTER_ADD_CLASS(Filter, UWorld::StaticClass());
			Filter.PackagePaths.Add(FName(*PathFilter));
			Filter.bRecursivePaths = true;
			Filter.bRecursiveClasses = true;
			TArray<FAssetData> Assets;
			Registry.GetAssets(Filter, Assets);
			for (const FAssetData& A : Assets) AddEntry(A, TEXT("World"));
		}

#if WITH_NIAGARA
		if (bIsAll || TypeLower == TEXT("niagarasystem") || TypeLower == TEXT("niagara_system"))
		{
			FARFilter Filter;
			NEXUS_FILTER_ADD_CLASS(Filter, UNiagaraSystem::StaticClass());
			Filter.PackagePaths.Add(FName(*PathFilter));
			Filter.bRecursivePaths = true;
			Filter.bRecursiveClasses = true;
			TArray<FAssetData> Assets;
			Registry.GetAssets(Filter, Assets);
			for (const FAssetData& A : Assets) AddEntry(A, TEXT("NiagaraSystem"));
		}
#endif

#if WITH_STATETREE
		if (bIsAll || TypeLower == TEXT("statetree") || TypeLower == TEXT("state_tree"))
		{
			FARFilter Filter;
			NEXUS_FILTER_ADD_CLASS(Filter, UStateTree::StaticClass());
			Filter.PackagePaths.Add(FName(*PathFilter));
			Filter.bRecursivePaths = true;
			Filter.bRecursiveClasses = true;
			TArray<FAssetData> Assets;
			Registry.GetAssets(Filter, Assets);
			for (const FAssetData& A : Assets) AddEntry(A, TEXT("StateTree"));
		}
#endif

#if WITH_METASOUND
		if (bIsAll || TypeLower == TEXT("metasoundsource") || TypeLower == TEXT("meta_sound_source") || TypeLower == TEXT("metasound"))
		{
			FARFilter Filter;
			NEXUS_FILTER_ADD_CLASS(Filter, UMetaSoundSource::StaticClass());
			Filter.PackagePaths.Add(FName(*PathFilter));
			Filter.bRecursivePaths = true;
			Filter.bRecursiveClasses = true;
			TArray<FAssetData> Assets;
			Registry.GetAssets(Filter, Assets);
			for (const FAssetData& A : Assets) AddEntry(A, TEXT("MetaSoundSource"));
		}
#if NX_UE_HAS_METASOUND_PATCH
		if (bIsAll || TypeLower == TEXT("metasoundpatch") || TypeLower == TEXT("meta_sound_patch"))
		{
			FARFilter Filter;
			NEXUS_FILTER_ADD_CLASS(Filter, UMetaSoundPatch::StaticClass());
			Filter.PackagePaths.Add(FName(*PathFilter));
			Filter.bRecursivePaths = true;
			Filter.bRecursiveClasses = true;
			TArray<FAssetData> Assets;
			Registry.GetAssets(Filter, Assets);
			for (const FAssetData& A : Assets) AddEntry(A, TEXT("MetaSoundPatch"));
		}
#endif // NX_UE_HAS_METASOUND_PATCH
#endif // WITH_METASOUND

#if WITH_PCG
		if (bIsAll || TypeLower == TEXT("pcggraph") || TypeLower == TEXT("pcg_graph") || TypeLower == TEXT("pcg"))
		{
			FARFilter Filter;
			NEXUS_FILTER_ADD_CLASS(Filter, UPCGGraph::StaticClass());
			Filter.PackagePaths.Add(FName(*PathFilter));
			Filter.bRecursivePaths = true;
			Filter.bRecursiveClasses = true;
			TArray<FAssetData> Assets;
			Registry.GetAssets(Filter, Assets);
			for (const FAssetData& A : Assets) AddEntry(A, TEXT("PCGGraph"));
		}
#endif

#if WITH_POSE_SEARCH
		if (bIsAll || TypeLower == TEXT("posesearchdatabase") || TypeLower == TEXT("pose_search_database") || TypeLower == TEXT("posesearch"))
		{
			FARFilter Filter;
			NEXUS_FILTER_ADD_CLASS(Filter, UPoseSearchDatabase::StaticClass());
			Filter.PackagePaths.Add(FName(*PathFilter));
			Filter.bRecursivePaths = true;
			Filter.bRecursiveClasses = true;
			TArray<FAssetData> Assets;
			Registry.GetAssets(Filter, Assets);
			for (const FAssetData& A : Assets) AddEntry(A, TEXT("PoseSearchDatabase"));
		}
		if (bIsAll || TypeLower == TEXT("posesearchschema") || TypeLower == TEXT("pose_search_schema"))
		{
			FARFilter Filter;
			NEXUS_FILTER_ADD_CLASS(Filter, UPoseSearchSchema::StaticClass());
			Filter.PackagePaths.Add(FName(*PathFilter));
			Filter.bRecursivePaths = true;
			Filter.bRecursiveClasses = true;
			TArray<FAssetData> Assets;
			Registry.GetAssets(Filter, Assets);
			for (const FAssetData& A : Assets) AddEntry(A, TEXT("PoseSearchSchema"));
		}
#endif

#if WITH_GAS
		auto AddGasBlueprintEntries = [&](const TCHAR* ParentSubstr, const TCHAR* OutType)
		{
			FARFilter Filter;
			NEXUS_FILTER_ADD_CLASS(Filter, UBlueprint::StaticClass());
			Filter.PackagePaths.Add(FName(*PathFilter));
			Filter.bRecursivePaths = true;
			Filter.bRecursiveClasses = true;
			TArray<FAssetData> Assets;
			Registry.GetAssets(Filter, Assets);
			for (const FAssetData& A : Assets)
			{
				FString ParentClass;
				if (!A.GetTagValue(TEXT("ParentClass"), ParentClass)) continue;
				if (!ParentClass.Contains(ParentSubstr)) continue;
				AddEntry(A, OutType);
			}
		};

		if (bIsAll || TypeLower == TEXT("gameplayability") || TypeLower == TEXT("gameplay_ability"))
		{
			AddGasBlueprintEntries(TEXT("GameplayAbility"), TEXT("GameplayAbility"));
		}
		if (bIsAll || TypeLower == TEXT("gameplayeffect") || TypeLower == TEXT("gameplay_effect"))
		{
			AddGasBlueprintEntries(TEXT("GameplayEffect"), TEXT("GameplayEffect"));
		}
		if (bIsAll || TypeLower == TEXT("attributeset") || TypeLower == TEXT("attribute_set"))
		{
			AddGasBlueprintEntries(TEXT("AttributeSet"), TEXT("AttributeSet"));
		}
#endif

#if NX_UE_HAS_DATA_LAYER_ASSET
		if (bIsAll || TypeLower == TEXT("datalayerasset") || TypeLower == TEXT("data_layer_asset") || TypeLower == TEXT("datalayer"))
		{
			FARFilter Filter;
			NEXUS_FILTER_ADD_CLASS(Filter, UDataLayerAsset::StaticClass());
			Filter.PackagePaths.Add(FName(*PathFilter));
			Filter.bRecursivePaths = true;
			Filter.bRecursiveClasses = true;
			TArray<FAssetData> Assets;
			Registry.GetAssets(Filter, Assets);
			for (const FAssetData& A : Assets) AddEntry(A, TEXT("DataLayerAsset"));
		}
#endif

		if (!bIsAll
			&& TypeLower != TEXT("blueprint") && TypeLower != TEXT("widget")
			&& TypeLower != TEXT("struct") && TypeLower != TEXT("datatable")
			&& TypeLower != TEXT("dataasset") && TypeLower != TEXT("material")
			&& TypeLower != TEXT("materialinstance")
			&& TypeLower != TEXT("animmontage") && TypeLower != TEXT("anim_montage")
			&& TypeLower != TEXT("animblueprint") && TypeLower != TEXT("anim_blueprint")
			&& TypeLower != TEXT("behaviortree") && TypeLower != TEXT("behavior_tree")
			&& TypeLower != TEXT("blackboard")
			&& TypeLower != TEXT("texture2d") && TypeLower != TEXT("texture")
			&& TypeLower != TEXT("staticmesh") && TypeLower != TEXT("static_mesh")
			&& TypeLower != TEXT("skeletalmesh") && TypeLower != TEXT("skeletal_mesh")
			&& TypeLower != TEXT("animsequence") && TypeLower != TEXT("anim_sequence")
			&& TypeLower != TEXT("skeleton")
			&& TypeLower != TEXT("soundwave") && TypeLower != TEXT("sound_wave")
			&& TypeLower != TEXT("soundcue") && TypeLower != TEXT("sound_cue")
			&& TypeLower != TEXT("curvefloat") && TypeLower != TEXT("curve_float") && TypeLower != TEXT("curve")
			&& TypeLower != TEXT("curvevector") && TypeLower != TEXT("curve_vector")
			&& TypeLower != TEXT("curvelinearcolor") && TypeLower != TEXT("curve_linear_color")
			&& TypeLower != TEXT("curvetable") && TypeLower != TEXT("curve_table")
			&& TypeLower != TEXT("userdefinedelnum") && TypeLower != TEXT("enum") && TypeLower != TEXT("user_defined_enum")
			&& TypeLower != TEXT("animcomposite") && TypeLower != TEXT("anim_composite")
			&& TypeLower != TEXT("physicalmaterial") && TypeLower != TEXT("physical_material")
			&& TypeLower != TEXT("rendertarget") && TypeLower != TEXT("render_target") && TypeLower != TEXT("texturerendertarget2d")
			&& TypeLower != TEXT("soundclass") && TypeLower != TEXT("sound_class")
			&& TypeLower != TEXT("soundattenuation") && TypeLower != TEXT("sound_attenuation")
			&& TypeLower != TEXT("soundconcurrency") && TypeLower != TEXT("sound_concurrency")
			&& TypeLower != TEXT("soundsubmix") && TypeLower != TEXT("sound_submix")
			&& TypeLower != TEXT("world") && TypeLower != TEXT("level") && TypeLower != TEXT("map")
#if WITH_NIAGARA
			&& TypeLower != TEXT("niagarasystem") && TypeLower != TEXT("niagara_system")
#endif
#if WITH_STATETREE
			&& TypeLower != TEXT("statetree") && TypeLower != TEXT("state_tree")
#endif
#if WITH_METASOUND
			&& TypeLower != TEXT("metasoundsource") && TypeLower != TEXT("meta_sound_source") && TypeLower != TEXT("metasound")
			&& TypeLower != TEXT("metasoundpatch") && TypeLower != TEXT("meta_sound_patch")
#endif
#if WITH_PCG
			&& TypeLower != TEXT("pcggraph") && TypeLower != TEXT("pcg_graph") && TypeLower != TEXT("pcg")
#endif
#if WITH_POSE_SEARCH
			&& TypeLower != TEXT("posesearchdatabase") && TypeLower != TEXT("pose_search_database") && TypeLower != TEXT("posesearch")
			&& TypeLower != TEXT("posesearchschema") && TypeLower != TEXT("pose_search_schema")
#endif
#if WITH_GAS
			&& TypeLower != TEXT("gameplayability") && TypeLower != TEXT("gameplay_ability")
			&& TypeLower != TEXT("gameplayeffect") && TypeLower != TEXT("gameplay_effect")
			&& TypeLower != TEXT("attributeset") && TypeLower != TEXT("attribute_set")
#endif
#if NX_UE_HAS_DATA_LAYER_ASSET
			&& TypeLower != TEXT("datalayerasset") && TypeLower != TEXT("data_layer_asset") && TypeLower != TEXT("datalayer")
#endif
			)
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
				TEXT("Unknown assetType '%s'. Use a UClass name (e.g. AnimSequence, SkeletalMesh) or a shortcut (Blueprint/Widget/Struct/DataTable/DataAsset/Material/MaterialInstance/AnimMontage/AnimBlueprint/BehaviorTree/Blackboard/AnimSequence/SkeletalMesh/Skeleton/Texture2D/StaticMesh/GameplayAbility/…/all)."),
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

		TArray<TSharedPtr<FJsonValue>> PageArray;
		for (int32 i = Start; i < End; ++i)
		{
			const FEntry& E = AllEntries[i];
			TSharedPtr<FJsonObject> EntryObj = MakeShared<FJsonObject>();
			EntryObj->SetStringField(TEXT("name"),      E.Name);
			EntryObj->SetStringField(TEXT("path"),      E.Path);
			EntryObj->SetStringField(TEXT("assetType"), E.Type);
			FString RecommendedGet, RecommendedManage;
			FNexusAssetUtils::ResolveRecommendedCapabilities(E.Type, RecommendedGet, RecommendedManage);
			if (!RecommendedGet.IsEmpty())
			{
				EntryObj->SetStringField(TEXT("recommendedGet"), RecommendedGet);
			}
			if (!RecommendedManage.IsEmpty())
			{
				EntryObj->SetStringField(TEXT("recommendedManage"), RecommendedManage);
			}
			if (!E.ParentClass.IsEmpty())    EntryObj->SetStringField(TEXT("parentClass"),    E.ParentClass);
			if (!E.RowStruct.IsEmpty())      EntryObj->SetStringField(TEXT("rowStruct"),      E.RowStruct);
			if (!E.ParentMaterial.IsEmpty()) EntryObj->SetStringField(TEXT("parentMaterial"), E.ParentMaterial);
			PageArray.Add(MakeShared<FJsonValueObject>(EntryObj));
		}

		OutEntry->SetNumberField(TEXT("totalCount"), Total);
		OutEntry->SetNumberField(TEXT("offset"),     Start);
		OutEntry->SetNumberField(TEXT("limit"),      Limit);
		OutEntry->SetArrayField(TEXT("assets"),      PageArray);
		OutEntries.Add(MakeShared<FJsonValueObject>(OutEntry));
	
	});
}

REGISTER_MCP_CAPABILITY(FSearchAssetCapability)

