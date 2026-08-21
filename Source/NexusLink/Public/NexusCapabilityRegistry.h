// Copyright byteyang. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "NexusCapability.h"

/**
 * Capability 注册记录 —— 注册期构建一次，运行期只读。
 *
 * 持有：
 *   - Def          : GetDefinition() 结果（含 InputSchema deep-clone）
 *   - Keywords     : 预算搜索关键词（Name/Desc 分词 + ExtraSearchKeywords + Tags 功能分类）
 *   - Instance     : 无状态单例实例（per-request new 已无必要）
 *   - SourceRelDir : 设置面板分组路径（注册期 __FILE__）
 */
struct FCapRecord
{
	FNexusCapabilityDefinition   Def;
	TArray<FString>              Keywords;
	TSharedRef<FNexusCapability> Instance;
	/** 相对 Private/Capabilities 的目录（如 Asset/Blueprint）；元工具为 Tools；空则设置面板按 tag 回退。 */
	FString                      SourceRelDir;

	explicit FCapRecord(TSharedRef<FNexusCapability> InInstance)
		: Instance(MoveTemp(InInstance))
	{}
};

/** search_asset 按 assetType 解析出的推荐读/写 Capability。 */
struct FNexusSearchAssetRoute
{
	FString RecommendedGet;
	FString RecommendedManage;
};

/**
 * Capability 全局注册表 —— 全局单例。
 *
 * Capability 与 Tool 完全解耦：cap .cpp 末尾用 REGISTER_MCP_CAPABILITY 自注册到此表，
 * 由 search_capabilities / call_capability 元工具消费。
 * cap 名（record.Def.Name）在全局必须唯一；重名将在 Register 时触发 ensureMsgf 警告。
 */
class NEXUSLINK_API FNexusCapabilityRegistry
{
public:
	static FNexusCapabilityRegistry& Get();

	/**
	 * 注册一个 cap 实例；Register() 内一次性构建 FCapRecord（含 Def + Keywords + 源码分组路径）。
	 * 重名时 ensureMsgf 提示后跳过，首个同名实例保持权威。
	 * @param SourceFile 传入 TEXT(__FILE__)；静态初始化期只做字符串处理，勿打日志。
	 */
	void Register(TSharedRef<FNexusCapability> Cap, const TCHAR* SourceFile = nullptr);

	/**
	 * 从编译期 __FILE__ 提取设置面板分组路径（纯字符串，可供静态初始化期调用）。
	 * Capabilities 下返回相对目录；Tools 下返回 "Tools"；无法识别则空。
	 */
	static FString MakeSettingsGroupPath(const TCHAR* SourceFile);

	/** 按注册顺序返回全部 record；search_capabilities 在此基础上做禁用/可见性过滤。 */
	const TArray<FCapRecord>& GetAllRecords() const { return Records; }

	/**
	 * 按 cap 名（大小写不敏感）O(1) 查找 record；未找到返回 nullptr。
	 * call_capability 用此代替线性扫描。
	 */
	const FCapRecord* FindRecordByName(const FString& CapabilityName) const;

	/** 读取 manage_* InputSchema 中 operations.items.action.enum；未注册或无该路径返回 false。 */
	static bool CollectOperationActions(const FString& CapName, TArray<FString>& OutActions);

	/**
	 * 按 search_asset 返回的 assetType 解析推荐 get/manage Capability。
	 * 索引来自各 cap BuildDefinition 声明的 SearchAssetTypes；无声明时 Out* 为空。
	 */
	void ResolveSearchAssetRoute(
		const FString& AssetType,
		FString& OutRecommendedGet,
		FString& OutRecommendedManage) const;

	/**
	 * 清空整个注册表（仅供测试使用：用例可临时注册若干 cap 跑 Execute，
	 * 结束后调用本方法清场，避免污染下一条用例的全局表状态）。
	 */
	void Reset();

private:
	void IndexSearchAssetTypes(const FNexusCapabilityDefinition& Def);

	TArray<FCapRecord>   Records;
	TMap<FString, int32> NameIndex; // key 为 lower(Name) → Records 下标
	TMap<FString, FNexusSearchAssetRoute> SearchAssetRouteIndex; // key 为 lower(assetType)
};

/**
 * 静态初始化期自动注册辅助类（与 FNexusMcpToolAutoRegister 同模式）。
 *
 * 约束：构造函数运行于 dyld / CRT 静态初始化阶段，GMalloc、TLS、Trace 尚未完全就绪，
 * 严禁调用 UE_LOG / CPU Profiler 相关宏（iOS 上会直接 EXC_BAD_ACCESS）。
 */
struct FNexusCapabilityAutoRegister
{
	explicit FNexusCapabilityAutoRegister(TSharedRef<FNexusCapability> Cap, const TCHAR* SourceFile = nullptr)
	{
		FNexusCapabilityRegistry::Get().Register(MoveTemp(Cap), SourceFile);
	}
};

/**
 * Capability 自动注册宏。
 *
 * 用法（在 cap 实现 .cpp 文件末尾）：
 *   REGISTER_MCP_CAPABILITY(FGetActorPropertyCapability)
 *
 * 要求 CapClass 有默认构造函数且继承自 FNexusCapability；
 * Capability 的 Out.Name 在全局必须唯一。宏传入 __FILE__ 供设置面板按源码目录分组。
 *
 * 主模块 Type 为 UncookedOnly，cooked 目标不编本模块。!WITH_EDITOR 下宏仍为空：
 * build_test Game 探针会临时改 Type=Runtime 编译这些 TU，须避免静态初始化分配。
 */
#if WITH_EDITOR
#define REGISTER_MCP_CAPABILITY(CapClass) \
	static FNexusCapabilityAutoRegister AutoRegisterCap_##CapClass( \
		MakeShared<CapClass>(), TEXT(__FILE__) \
	);
#else
#define REGISTER_MCP_CAPABILITY(CapClass)
#endif
