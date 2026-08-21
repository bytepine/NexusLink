// Copyright byteyang. All Rights Reserved.

#pragma once

// Utils 层：Asset
#include "CoreMinimal.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Misc/Paths.h"
#include "Misc/PackageName.h"
#include "UObject/Object.h"
#include "UObject/Package.h"
#include "UObject/Class.h"
#include "Utils/NexusVersionCompat.h"
#include "Utils/NexusPackageLedger.h"

class UWidgetBlueprint;
class UWidget;
class UTexture2D;
class UAnimSequence;
class UStaticMesh;
class USkeletalMesh;
class UBodySetup;
class UPhysicsAsset;

/**
 * 资产侧共用工具：get_asset 输出 JSON（propertyPaths / minimal）、UObject 路径加载、类名解析、Widget BP 辅助。
 * 属性反射请使用 `FNexusPropertyUtils`。
 */
class NEXUSLINK_API FNexusAssetUtils
{
public:
	/**
	 * propertyPaths 过滤：空数组放行；否则按首段名 case-insensitive 匹配 PropName。
	 * Blueprint defaults/variable 过滤共用。
	 */
	static bool MatchesPropertyPathsFilter(const TArray<FString>& Paths, const FString& PropName)
	{
		if (Paths.Num() == 0)
		{
			return true;
		}
		for (const FString& Path : Paths)
		{
			FString First;
			if (!Path.Split(TEXT("."), &First, nullptr))
			{
				First = Path;
			}
			if (First.Equals(PropName, ESearchCase::IgnoreCase))
			{
				return true;
			}
		}
		return false;
	}

	/** detail=minimal 时只保留 assetType/name/path 三字段写入 OutEntry；各 cap 的 Execute 末尾按需调用。 */
	static void ApplyDetailMinimal(const TSharedPtr<FJsonObject>& Full, TSharedPtr<FJsonObject>& OutEntry)
	{
		static const TArray<FString> IdKeys = { TEXT("assetType"), TEXT("name"), TEXT("path") };
		for (const FString& Key : IdKeys)
		{
			TSharedPtr<FJsonValue> V = Full->TryGetField(Key);
			if (V.IsValid())
			{
				OutEntry->SetField(Key, V);
			}
		}
	}

	/** 加载资产；自动 fallback 到 "Path.BaseFilename" 形式（UE 资产系统常见需求）。 */
	template <typename TAsset>
	static TAsset* LoadAssetWithFallback(const FString& AssetPath)
	{
		if (AssetPath.IsEmpty()) return nullptr;
		TAsset* Asset = LoadObject<TAsset>(nullptr, *AssetPath);
		if (!Asset)
		{
			const FString Fallback = AssetPath + TEXT(".") + FPaths::GetBaseFilename(AssetPath);
			Asset = LoadObject<TAsset>(nullptr, *Fallback);
		}
		return Asset;
	}

	/**
	 * 加载资产并记账到 FNexusPackageLedger（内存高水位批量驱逐机制）：
	 * 若加载前该包未驻留内存，视为"本次引入"，登记后交由台账在阈值命中时整批卸载 + GC。
	 * 行为等价 LoadAssetWithFallback，仅多一步记账；用户已打开/已加载的包不受影响。
	 */
	template <typename TAsset>
	static TAsset* LoadAssetTracked(const FString& AssetPath)
	{
		if (AssetPath.IsEmpty()) return nullptr;

		FString PackageName = AssetPath;
		int32 DotIdx;
		if (PackageName.FindChar(TEXT('.'), DotIdx))
		{
			PackageName = PackageName.Left(DotIdx);
		}
		const bool bAlreadyResident = (FindPackage(nullptr, *PackageName) != nullptr);

		TAsset* Asset = LoadAssetWithFallback<TAsset>(AssetPath);
		if (Asset && !bAlreadyResident)
		{
			if (UPackage* Pkg = Asset->GetOutermost())
			{
				FNexusPackageLedger::Get().NoteIntroduced(Pkg);
			}
		}
		return Asset;
	}

	/**
	 * 按类名（允许裸名，如 "StaticMeshComponent"）查找 UClass。
	 * 顺序：FindFirstObject / FindObject → 加 "U" 前缀重试 → 最后 LoadObject<UClass>。
	 */
	static UClass* FindClassWithUPrefix(const FString& ClassName);

	/** 通用 Widget Blueprint 加载（含双路径 fallback）。 */
	static UWidgetBlueprint* LoadWidgetBP(const FString& AssetPath);

	/** 在 WidgetBlueprint 的 WidgetTree 上按名字查找子 Widget。 */
	static UWidget* FindWidgetByName(UWidgetBlueprint* WBP, const FString& WidgetName);

	/**
	 * 保存新创建的资产到磁盘。
	 * @param Package  资产所在的 UPackage
	 * @param Asset    要保存的 UObject（传 nullptr 则保存 Package 本身）
	 * @param PackagePath  包路径（如 "/Game/AI/BT_Attack"），用于推算磁盘文件路径
	 * @return 是否保存成功
	 */
	static bool SaveNewAsset(UPackage* Package, UObject* Asset, const FString& PackagePath);

	/**
	 * save_asset 安全落盘：解析主资产后复用 SaveNewAsset。
	 * Live Coding 开启时仅 MarkPackageDirty，bOutDeferred=true。
	 * @return 是否已成功写入磁盘
	 */
	static bool SaveDirtyPackage(UPackage* Package, const FString& PackagePath, const FString& AssetPathHint, bool& bOutDeferred, FString& OutNote);

	/**
	 * 编译蓝图并保存到磁盘（Blueprint / AnimBlueprint / WidgetBlueprint 通用）。
	 * @param Package    资产所在的 UPackage
	 * @param Blueprint  要编译的蓝图
	 * @param PackagePath 包路径
	 * @return 是否保存成功
	 */
	static bool CompileAndSaveBlueprint(UPackage* Package, class UBlueprint* Blueprint, const FString& PackagePath);

	/**
	 * manage 收尾：按需编译（UBlueprint）与可选落盘。由 FNexusCapability::Run 在 Execute 成功后调用。
	 * 编译失败或包未找到时写入 compileError/saveError，不设 FatalError。
	 * @param AssetPath    入参 assetPath
	 * @param bCompile     是否编译（仅 BP/ABP/WBP 应传 true）
	 * @param bSaveToDisk  是否落盘
	 * @param OutTop       写入 compiled/saved/hasCompilerErrors 等顶层字段
	 */
	static void ApplyManageFinalize(
		const FString& AssetPath,
		bool bCompile,
		bool bSaveToDisk,
		TSharedPtr<FJsonObject>& OutTop);

	/**
	 * 从 UObject 的外部包取包路径字符串（常用于写入 JSON 的 "path" 字段）。
	 * 等价于 Obj->GetOutermost()->GetName()；Obj 为 nullptr 时返回空字符串。
	 */
	static FString PackagePathOf(const UObject* Obj);

	/** UTexture2D 逻辑尺寸（跨版本：GetSurface* / GetPlatformData / PlatformData）。Texture 为 nullptr 时输出 0。 */
	static void GetTexture2DSurfaceSize(const UTexture2D* Texture, int32& OutWidth, int32& OutHeight);

	/** 向 Entry 写入 AnimSequence 时长/帧数/帧率/loop（跨版本分支集中于此）。Seq 为 nullptr 时不写入。 */
	static void AppendAnimSequenceMetadataFields(const UAnimSequence* Seq, TSharedPtr<FJsonObject>& Entry);

	/** 向 Entry 写入 AnimSequence Notifies 列表（index/name/time/duration/notifyClass）。 */
	static void AppendAnimSequenceNotifyFields(const UAnimSequence* Seq, TSharedPtr<FJsonObject>& Entry);

	/** 向 Entry 写入 AnimSequence 浮点曲线列表（name/keyCount/keys[{time,value}]；跨版本 RawCurveData / DataModel）。 */
	static void AppendAnimSequenceCurveFields(const UAnimSequence* Seq, TSharedPtr<FJsonObject>& Entry);

	/** StaticMesh 材质槽列表（跨版本：GetStaticMaterials / StaticMaterials）。 */
	static const TArray<struct FStaticMaterial>& GetStaticMeshMaterials(const UStaticMesh& Mesh);

	/** StaticMesh 碰撞 BodySetup（跨版本：GetBodySetup / BodySetup）。Mesh 为 nullptr 时返回 nullptr。 */
	static UBodySetup* GetStaticMeshBodySetup(UStaticMesh* Mesh);

	/** SkeletalMesh 材质槽列表（跨版本：GetMaterials / Materials）。 */
	static const TArray<struct FSkeletalMaterial>& GetSkeletalMeshMaterials(const USkeletalMesh& Mesh);

	/** SkeletalMesh PhysicsAsset（跨版本：GetPhysicsAsset / PhysicsAsset）。Mesh 为 nullptr 时返回 nullptr。 */
	static UPhysicsAsset* GetSkeletalMeshPhysicsAsset(USkeletalMesh* Mesh);

	/**
	 * 按名称查找 UUserDefinedStruct；支持名称带 F 前缀或不带 F 前缀的情况。
	 * 优先 FindObject，其次 LoadObject，兼容路径格式（/Game/...）与短名。
	 * 未找到返回 nullptr。
	 */
	static class UUserDefinedStruct* FindStructByName(const FString& StructName);

	/**
	 * 资产创建结果：成功时 Asset 非空且 Error 为空。Error 为英文，可直接写入 entry/fatal。
	 */
	struct FAssetCreateOutcome
	{
		UObject* Asset = nullptr;
		FString Error;
		bool Ok() const { return Asset != nullptr && Error.IsEmpty(); }
	};

	/**
	 * 普通 UObject 资产创建：已存在检查 → CreatePackage → NewObject。
	 * bNotifyAndSave=true（默认）时立刻 NotifyAndSaveCreated；需在落盘前写字段时传 false，由调用方自行 NotifyAndSaveCreated。
	 * 错误消息为英文。
	 */
	static FAssetCreateOutcome CreatePlainAsset(
		const FString& AssetPath,
		UClass* AssetClass,
		EObjectFlags Flags = RF_Public | RF_Standalone,
		bool bNotifyAndSave = true);

	template <typename TAsset>
	static FAssetCreateOutcome CreatePlainAsset(
		const FString& AssetPath,
		EObjectFlags Flags = RF_Public | RF_Standalone,
		bool bNotifyAndSave = true)
	{
		return CreatePlainAsset(AssetPath, TAsset::StaticClass(), Flags, bNotifyAndSave);
	}

	/**
	 * 蓝图类资产创建：DoesPackageExist → 解析父类 → CreatePackage → CreateBlueprint → NotifyCompileAndSave。
	 * ExpectedBase 非空时父类须为其子类。BlueprintClass/GeneratedClass 默认 UBlueprint / UBlueprintGeneratedClass。
	 * 仅 WITH_EDITOR 可用；非编辑器返回 Error。
	 * bCompileAndSave=false 时只创建不编译落盘，供调用方补节点后再 NotifyCompileAndSave。
	 */
	static FAssetCreateOutcome CreateBlueprintAsset(
		const FString& AssetPath,
		const FString& ParentClassPath,
		UClass* ExpectedBase,
		UClass* BlueprintClass = nullptr,
		UClass* GeneratedClass = nullptr,
		bool bCompileAndSave = true);

	/**
	 * 新资产创建 finalize 三件套（P2 消除）：MarkPackageDirty + AssetCreated + SaveNewAsset。
	 * 等价于：
	 *   Package->MarkPackageDirty();
	 *   FAssetRegistryModule::AssetCreated(Asset);
	 *   FNexusAssetUtils::SaveNewAsset(Package, Asset, PackagePath);
	 */
	static bool NotifyAndSaveCreated(UPackage* Package, UObject* Asset, const FString& PackagePath);

	/**
	 * 新蓝图创建 finalize 三件套（P2 消除）：MarkPackageDirty + AssetCreated + CompileAndSaveBlueprint。
	 * 等价于：
	 *   Package->MarkPackageDirty();
	 *   FAssetRegistryModule::AssetCreated(Blueprint);
	 *   FNexusAssetUtils::CompileAndSaveBlueprint(Package, Blueprint, PackagePath);
	 */
	static bool NotifyCompileAndSave(UPackage* Package, class UBlueprint* Blueprint, const FString& PackagePath);

	/**
	 * 写入 blueprintType（normal/interface/…）与 implementedInterfaces[]（空数组也写出）。
	 * create/get_asset_blueprint 共用，供 BPI 与 Implement 接口回显。
	 */
	static void AppendBlueprintMetaFields(const class UBlueprint* BP, TSharedPtr<FJsonObject>& OutEntry);
};
