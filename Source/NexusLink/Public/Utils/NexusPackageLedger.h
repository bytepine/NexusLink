// Copyright byteyang. All Rights Reserved.

#pragma once

// Utils 层：Editor（内存管理）
#include "CoreMinimal.h"
#include "UObject/WeakObjectPtr.h"

class UPackage;

/**
 * 记账「本次由 NexusLink 引入」的包，累积到阈值（数量高水位 / 进程内存高水位）后整批卸载 + 一次 GC。
 * 解决批量读蓝图等资产时 LoadObject 后包常驻不释放导致编辑器内存暴涨的问题。
 *
 * 设计约束（务必遵守）：
 *   - 只持弱引用（TWeakObjectPtr），绝不 AddToRoot / 改对象 flag，不阻碍引擎自身 GC；
 *   - 只登记"加载前未驻留、本次由我引入"的包，绝不触碰用户已打开/已加载的工作集；
 *   - Flush 前逐条重新校验：已被引擎自动回收 → 静默剔除，不当错误；dirty / 编辑器已打开 → 跳过，留待下轮。
 *
 * GameThread-only 单例；随每次 call_capability 调用重置内存基线。
 */
class NEXUSLINK_API FNexusPackageLedger
{
public:
	/** GameThread-only 单例。 */
	static FNexusPackageLedger& Get();

	/** 登记一个由本次调用引入的包；调用方需先确认加载前该包未驻留内存。重复登记自动去重。 */
	void NoteIntroduced(UPackage* Package);

	/** 剔除已被引擎自动回收的陈旧条目后，返回仍驻留的引入包数量。 */
	int32 LiveCount();

	/** 重置内存基线与「本次调用禁止自动卸载」标记；每次 call_capability 调用开始时调用。 */
	void ResetBaseline();

	/** 本次调用是否整体禁止自动卸载（对应 keepLoaded=true）。 */
	void SetSuppressedForThisCall(bool bSuppressed) { bSuppressedForThisCall = bSuppressed; }
	bool IsSuppressedForThisCall() const { return bSuppressedForThisCall; }

	/**
	 * 数量或内存高水位阈值是否命中（任一命中即 true）。
	 * @param FlushThresholdCount 引入包数量阈值，<=0 表示该项关闭
	 * @param MemoryHighWaterMB   进程物理内存较基线增长阈值（MB），<=0 表示该项关闭
	 */
	bool ShouldFlush(int32 FlushThresholdCount, int32 MemoryHighWaterMB);

	struct FFlushStats
	{
		int32 Unloaded = 0;
		int32 Skipped = 0;
		int32 AlreadyCollected = 0;
	};

	/** 整批卸载台账中仍驻留、可安全卸载的包；bGC=true 时卸载后触发一次 KEEPFLAGS GC 回收级联依赖。 */
	FFlushStats Flush(bool bGC);

	/**
	 * 静态工具：对任意包列表做统一的安全过滤 + 整批卸载。供台账 Flush() 与手动 unload_asset
	 * Capability 共用同一套判定逻辑（dirty / 编辑器已打开 / 引擎内建包 一律跳过）。
	 * @param bSkipDirty  true 时 dirty 包跳过；false 时强制卸载（含未保存修改，慎用）
	 * @param OutSkipped  非空时回填被跳过（非 dirty/engine 原因以外）的包，供调用方决定是否保留记账
	 */
	static FFlushStats UnloadPackagesSafely(const TArray<UPackage*>& Packages, bool bSkipDirty, bool bGC,
		TArray<UPackage*>* OutSkipped = nullptr);

	/**
	 * 便捷入口：若未被 keepLoaded 抑制、且插件「自动卸载」设置开启、且阈值命中，则执行一次 Flush。
	 * 供各 Capability 循环内按需调用（读取 UNexusLinkSettings 阈值），无需自行拼装判断逻辑。
	 */
	static void MaybeFlush();

	/** 批尾强制清空台账（忽略阈值），仅在未被 keepLoaded 抑制时生效；call_capability 每次调用结束时调用。 */
	static void FlushRemainingUnlessSuppressed();

private:
	struct FEntry
	{
		FName PackageName;
		TWeakObjectPtr<UPackage> WeakPkg;
	};

	TArray<FEntry> Entries;
	uint64 BaselineUsedPhysical = 0;
	bool bSuppressedForThisCall = false;

	/** 移除弱引用已失效（引擎已自动回收）的条目。 */
	void PruneDead();
};
