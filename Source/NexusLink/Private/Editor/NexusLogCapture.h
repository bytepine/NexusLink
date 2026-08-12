// Copyright byteyang. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Misc/OutputDevice.h"

/**
 * 日志条目结构体，记录单条输出日志的完整信息。
 */
struct FNexusLogEntry
{
	/** 日志分类名（如 LogTemp、LogBlueprintUserMessages） */
	FString Category;
	/** 详细程度（如 Log、Warning、Error、Fatal） */
	ELogVerbosity::Type Verbosity;
	/** 日志正文 */
	FString Message;
	/** 捕获时的时间戳（秒，相对于引擎启动） */
	double Timestamp = 0.0;
	/** 墙钟时间（UTC），供 AI 可读关联 */
	FDateTime WallTime;
	/** 写入序号（单调递增，用于 exec 期间增量截取） */
	int32 Sequence = 0;
};

/** 按分类聚合统计（诊断摘要用）。 */
struct FNexusLogCategoryStat
{
	FString Category;
	int32 Count = 0;
	int32 Errors = 0;
	int32 Warnings = 0;
};

/**
 * UE 日志捕获器，挂接到 GLog 输出管道，将日志缓存于内存环形缓冲区。
 * 支持分类白名单过滤：白名单为空时捕获全部，非空时只捕获指定分类；
 * Warning/Error 始终捕获（不受白名单限制），避免收窄后漏诊。
 * 在 NexusLink 模块启动时注册，关闭时注销。
 */
class FNexusLogCapture : public FOutputDevice
{
public:
	/** 最大缓存条目数 */
	static constexpr int32 MaxEntries = 2000;

	/** 摘要按分类返回的最大条数 */
	static constexpr int32 MaxSummaryCategories = 20;

	FNexusLogCapture();
	virtual ~FNexusLogCapture();

	/** 注册到 GLog */
	void Register();

	/** 从 GLog 注销 */
	void Unregister();

	/**
	 * 设置分类白名单（大小写不敏感）。
	 * 传入空数组 = 捕获全部日志。
	 * 注意：Warning/Error 始终写入，不受白名单限制。
	 */
	void SetCategoryWhitelist(const TArray<FString>& Categories);

	/** 获取当前白名单（副本）。 */
	TArray<FString> GetCategoryWhitelist() const;

	/**
	 * 首次安装 / 升级迁移用的诊断默认白名单（收窄噪声，保留业务与脚本相关分类）。
	 * 不含引擎高 churn 分类（如 LogSlate / LogDerivedDataCache）。
	 */
	static TArray<FString> GetDefaultDiagnosticCategories();

	/**
	 * 查询缓存的日志条目，支持分页、增量游标与倒序（最新优先）。
	 * @param Offset          跳过的条目数（沿排序方向，default: 0）
	 * @param Limit           每页最多返回条数（1~500，default: 100）
	 * @param CategoryFilter  分类子串过滤（空字符串=不过滤）
	 * @param VerbosityFilter 最低详细程度（ELogVerbosity::All=不过滤）
	 * @param TextFilters     正文子串过滤列表（OR 匹配任一即通过，空数组=不过滤，大小写不敏感）
	 * @param OutTotalCount   符合过滤条件的总条数（分页用）
	 * @param SinceSequence   仅返回 Sequence 严格大于此值的条目（< 0 = 不过滤，用于增量拉取）
	 * @param bNewestFirst    true=最新在前（诊断常用）；false=最旧在前（保持时间升序）
	 * @return                当前页日志条目（顺序由 bNewestFirst 决定）
	 */
	TArray<FNexusLogEntry> Query(
		int32 Offset,
		int32 Limit,
		const FString& CategoryFilter,
		ELogVerbosity::Type VerbosityFilter,
		const TArray<FString>& TextFilters,
		int32& OutTotalCount,
		int32 SinceSequence = -1,
		bool bNewestFirst = false) const;

	/**
	 * 对过滤后的全量匹配集做聚合（不受分页影响）。
	 * OutByCategory 按 Count 降序，最多 MaxSummaryCategories 条。
	 */
	void Summarize(
		const FString& CategoryFilter,
		ELogVerbosity::Type VerbosityFilter,
		const TArray<FString>& TextFilters,
		int32 SinceSequence,
		TArray<FNexusLogCategoryStat>& OutByCategory,
		TMap<ELogVerbosity::Type, int32>& OutByVerbosity) const;

	/** 最新一条日志的 Sequence；缓冲为空时返回 -1。可作为下次 SinceSequence 游标。 */
	int32 GetLatestSequence() const;

	/** 全局单例访问 */
	static FNexusLogCapture& Get();

	/**
	 * 手动写入一条日志（不经 GLog 管道）。
	 * 用于 exec_command 等场景：控制台输出默认不进 GLog，需镜像到缓冲区供 get_output_log 查询。
	 * 不受分类白名单限制。
	 */
	void AppendEntry(const FString& Category, ELogVerbosity::Type Verbosity, const FString& Message);

	/** 当前已写入总条数（可作为 CollectSince 快照标记）。 */
	int32 GetTotalWritten() const;

	/** 返回 Sequence 严格大于 SinceSequence 的条目（时间升序）。 */
	TArray<FNexusLogEntry> CollectSince(int32 SinceSequence) const;

protected:
	// FOutputDevice 接口
	virtual void Serialize(const TCHAR* V, ELogVerbosity::Type Verbosity, const FName& Category) override;
	virtual bool CanBeUsedOnMultipleThreads() const override { return true; }

private:
	/** 判断某分类是否在白名单内（白名单为空则全部通过） */
	bool IsAllowed(const FName& Category) const;

	/** 查询过滤：是否匹配当前条件（不含 SinceSequence，调用方自判） */
	static bool MatchesFilters(
		const FNexusLogEntry& E,
		const FString& CategoryFilter,
		ELogVerbosity::Type VerbosityFilter,
		const TArray<FString>& TextFilters);

	/** 在已持锁前提下写入环形缓冲区 */
	void WriteEntryLocked(const FString& Category, ELogVerbosity::Type Verbosity, const TCHAR* Message);

	/** 环形缓冲区（写入受 Mutex 保护） */
	TArray<FNexusLogEntry> Buffer;
	/** 当前写入位置 */
	int32 WriteIndex = 0;
	/** 已写入总条数（用于判断缓冲区是否已满） */
	int32 TotalWritten = 0;
	/** 多线程写入保护 */
	mutable FCriticalSection Mutex;
	/** 是否已注册 */
	bool bRegistered = false;

	/**
	 * 分类白名单（全大写存储，比较时统一转大写）。
	 * 空 = 捕获全部。
	 */
	TArray<FString> Whitelist;

	static FNexusLogCapture* Singleton;
};
