// Copyright byteyang. All Rights Reserved.

#pragma once

// Utils 层（Asset）编辑器侧——触碰 Kismet2 / UMGEditor / LiveCoding，需要 UnrealEd。
// 阶段 2 模块拆分从 Public/Utils 一并 git mv 到 Source/NexusLinkEditor/。
// 启动时安装钩子到 NexusLink：由 FNexusLinkEditorModule::StartupModule 调用
// 本文件末尾的 NexusInstallAssetEditorUtilsHooks() 安装 ApplyManageFinalize 钩子。
#include "CoreMinimal.h"
#include "Dom/JsonObject.h"

class UWidgetBlueprint;
class UWidget;
class UBlueprint;
class UPackage;
class UClass;
class UObject;

/**
 * 资产侧编辑器工具（蓝图编译 / 落盘 / 创建、Widget Blueprint 辅助）。
 * 与 `FNexusAssetUtils`（运行时安全部分：加载 / 元数据 / CreatePlainAsset / SaveNewAsset）区分；
 * 本类所有方法都依赖 UnrealEd（Kismet2 编译、UMGEditor WidgetBlueprint、LiveCoding 检测），
 * 只能在编辑器宿主调用；非编辑器场景方法返回失败/错误，不崩溃。
 */
class NEXUSLINKEDITOR_API FNexusAssetEditorUtils
{
public:
	/** 通用 Widget Blueprint 加载（含路径 fallback）。仅 WITH_EDITOR 可用，非编辑器返回 nullptr。 */
	static UWidgetBlueprint* LoadWidgetBP(const FString& AssetPath);

	/** 在 WidgetBlueprint 的 WidgetTree 中按名字查找子 Widget。仅 WITH_EDITOR 可用，非编辑器返回 nullptr。 */
	static UWidget* FindWidgetByName(UWidgetBlueprint* WBP, const FString& WidgetName);

	/**
	 * save_asset 安全落盘：解析主资产后调用 FNexusAssetUtils::SaveNewAsset。
	 * Live Coding 开启时改为 MarkPackageDirty，bOutDeferred=true。
	 * @return 是否已成功写入磁盘
	 */
	static bool SaveDirtyPackage(UPackage* Package, const FString& PackagePath, const FString& AssetPathHint, bool& bOutDeferred, FString& OutNote);

	/**
	 * 编译蓝图并保存到磁盘（Blueprint / AnimBlueprint / WidgetBlueprint 通用）。
	 * @return 是否保存成功
	 */
	static bool CompileAndSaveBlueprint(UPackage* Package, UBlueprint* Blueprint, const FString& PackagePath);

	/**
	 * manage 收尾：按需编译（UBlueprint）与可选落盘。由 FNexusEditorServices::ApplyManageFinalize
	 * 钩子转发调用（真正调用方 FNexusCapability::Run 驻留 Runtime 模块）。
	 * 编译失败或找不到时写入 compileError/saveError，不视为 FatalError。
	 */
	static void ApplyManageFinalize(
		const FString& AssetPath,
		bool bCompile,
		bool bSaveToDisk,
		TSharedPtr<FJsonObject>& OutTop);

	/**
	 * 蓝图类资产创建：DoesPackageExist → 解析父类 → CreatePackage → CreateBlueprint → NotifyCompileAndSave。
	 * ExpectedBase 非空时父类必须为其子类。BlueprintClass/GeneratedClass 默认 UBlueprint / UBlueprintGeneratedClass。
	 * 仅 WITH_EDITOR 可用，非编辑器返回 Error。
	 * bCompileAndSave=false 时只创建不编译落盘，供调用方补节点后再 NotifyCompileAndSave。
	 */
	struct FAssetCreateOutcome
	{
		UObject* Asset = nullptr;
		FString Error;
		bool Ok() const { return Asset != nullptr && Error.IsEmpty(); }
	};
	static FAssetCreateOutcome CreateBlueprintAsset(
		const FString& AssetPath,
		const FString& ParentClassPath,
		UClass* ExpectedBase,
		UClass* BlueprintClass = nullptr,
		UClass* GeneratedClass = nullptr,
		bool bCompileAndSave = true);

	/**
	 * 新蓝图创建 finalize 三件套：MarkPackageDirty + AssetCreated + CompileAndSaveBlueprint。
	 */
	static bool NotifyCompileAndSave(UPackage* Package, UBlueprint* Blueprint, const FString& PackagePath);
};

/** 把 ApplyManageFinalize 安装进 FNexusEditorServices 钩子表；由编辑器宿主调用。 */
void NexusInstallAssetEditorUtilsHooks();
