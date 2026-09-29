// Copyright byteyang. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Misc/OutputDevice.h"
#include "Utils/NexusStringMatchUtils.h"

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
	/** 写入序号（单调递增，用于 exec 期间增量截取）。连续合并时更新为最后一次。 */
	int32 Sequence = 0;
	/** 连续相同（category + verbosity + 正文）的条数。1 = 没有合并。 */
	int32 Repeat = 1;
};

/** 会话 watch：只进旁路缓冲，不改主环、不写配置。后一次 arm 覆盖上一次。 */
struct FNexusLogWatchSpec
{
	/** 分类子串；空 = 全部分类。 */
	TArray<FString> Categories;
	/** 正文子串，命中任一即通过；空 = 不限制正文。 */
	TArray<FString> TextIncludes;
	/** 正文子串，命中任一即丢弃。 */
	TArray<FString> TextExcludes;
	/** 最低详细程度（数值更大的更啰嗦，会被丢掉）。All = 不限制。 */
	ELogVerbosity::Type MinVerbosity = ELogVerbosity::Log;
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
 * 与上一条完全相同的日志不占新槽，只累加 Repeat。
 * 另有一块会话 watch 旁路缓冲：只收 arm 时声明的筛选，Warning/Error 也要命中。
 * 在 NexusLink 模块启动时注册，关闭时注销。
 */
class NEXUSLINK_API FNexusLogCapture : public FOutputDevice
{
public:
	/** 最大缓存条目数 */
	static constexpr int32 MaxEntries = 2000;

	/** 摘要按分类返回的最大条数 */
	static constexpr int32 MaxSummaryCategories = 20;

	/** watch 旁路缓冲容量。溢出时覆盖最旧并累计 Dropped。 */
	static constexpr int32 MaxWatchEntries = 1000;

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

	/**
	 * 下一条 Sequence（上次写入的 Sequence + 1，连续合并也递增）。
	 * exec_command 用它做 CollectSince 游标。
	 */
	int32 GetTotalWritten() const;

	/** 装上会话 watch 并清空旁路缓冲。返回值是此时主缓冲的 latestSequence。 */
	int32 ArmWatch(const FNexusLogWatchSpec& Spec);

	/** 卸下 watch 并丢掉旁路缓冲。 */
	void DisarmWatch();

	bool IsWatchArmed() const;

	/** 拷贝 watch 缓冲（时间升序）。未 arm 时返回空。 */
	TArray<FNexusLogEntry> CopyWatchEntries(int32& OutDropped, int32& OutArmedLatestSequence) const;

	/** 返回 Sequence 严格大于 SinceSequence 的条目（时间升序）。 */
	TArray<FNexusLogEntry> CollectSince(int32 SinceSequence) const;

protected:
	// FOutputDevice 接口
	virtual void Serialize(const TCHAR* V, ELogVerbosity::Type Verbosity, const FName& Category) override;
	virtual bool CanBeUsedOnMultipleThreads() const override { return true; }

private:
	/** 判断某分类是否在白名单内（白名单为空则全部通过）。须已持锁。 */
	bool IsAllowed(const FName& Category) const;

	/** 查询过滤：是否匹配当前条件（不含 SinceSequence，调用方自判） */
	static bool MatchesFilters(
		const FNexusLogEntry& E,
		const FNexusCompiledStringPattern& CategoryFilter,
		ELogVerbosity::Type VerbosityFilter,
		const TArray<FNexusCompiledStringPattern>& TextFilters);

	/** 拷贝当前环形缓冲有效区（持锁）；调用方在锁外过滤，避免 Query 卡住 Serialize。 */
	void CopyFilledEntries(TArray<FNexusLogEntry>& Out) const;

	/** 在已持锁前提下写入主环 */
	void WriteEntryLocked(const FString& Category, ELogVerbosity::Type Verbosity, const TCHAR* Message);

	/** 在已持锁前提下，命中 watch 筛选才写入旁路缓冲 */
	void TryWriteWatchLocked(const FString& Category, ELogVerbosity::Type Verbosity, const TCHAR* Message);

	/**
	 * 写入一块环。与上一条完全相同则只累加 Repeat 并推进 SequenceClock，不占新槽。
	 * ponytail: 只合并紧邻上一条。A/B/A/B 仍各占槽；要挡这种再按正文哈希合并。
	 */
	static void WriteRingLocked(
		TArray<FNexusLogEntry>& Ring,
		int32 Capacity,
		int32& WriteIndex,
		int32& SlotWrites,
		int32& SequenceClock,
		int32& Dropped,
		bool bCountDrops,
		const FString& Category,
		ELogVerbosity::Type Verbosity,
		const TCHAR* Message);

	/** 主环（写入受 Mutex 保护） */
	TArray<FNexusLogEntry> Buffer;
	/** 主环下一次写入槽 */
	int32 WriteIndex = 0;
	/** 主环占用过的槽次数（合并不增加；超过 MaxEntries 表示已卷绕） */
	int32 SlotWrites = 0;
	/** 下一条 Sequence。GetTotalWritten 返回它。 */
	int32 NextSequence = 0;

	/** watch 旁路环 */
	TArray<FNexusLogEntry> WatchBuffer;
	int32 WatchWriteIndex = 0;
	int32 WatchSlots = 0;
	int32 WatchSequence = 0;
	int32 WatchDropped = 0;
	int32 ArmedLatestSequence = -1;
	bool bWatchArmed = false;
	FNexusLogWatchSpec WatchSpec;
	/** 多线程写入保护 */
	mutable FCriticalSection Mutex;
	/** 是否已注册 */
	bool bRegistered = false;

	/**
	 * 分类白名单（全大写存储，子串回退比较用）。
	 * 空 = 捕获全部。ExactWhitelist 为 FName 精确命中（无堆分配）。
	 */
	TArray<FString> Whitelist;
	TSet<FName> ExactWhitelist;

	static FNexusLogCapture* Singleton;
};
