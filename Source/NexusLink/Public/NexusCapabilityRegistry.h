// Copyright byteyang. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "NexusCapability.h"

/**
 * Capability 注册记录 —— 注册期构建一次，运行期只读。
 *
 * 持有：
 *   - Def       : GetDefinition() 结果（含 InputSchema deep-clone）
 *   - Keywords  : 预算搜索关键词（Name/Desc 分词 + ExtraSearchKeywords + Tags 功能分类）
 *   - Instance  : 无状态单例实例（per-request new 已无必要）
 */
struct FCapRecord
{
	FNexusCapabilityDefinition   Def;
	TArray<FString>              Keywords;
	TSharedRef<FNexusCapability> Instance;

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
	 * 注册一个 cap 实例；Register() 内一次性构建 FCapRecord（含 Def + Keywords）。
	 * 重名时 ensureMsgf 提示后跳过，首个同名实例保持权威。
	 */
	void Register(TSharedRef<FNexusCapability> Cap);

	/** 按注册顺序返回全部 record；search_capabilities 在此基础上做禁用/可见性过滤。 */
	const TArray<FCapRecord>& GetAllRecords() const { return Records; }

	/**
	 * 按 cap 名（大小写不敏感）O(1) 查找 record；未找到返回 nullptr。
	 * call_capability 用此代替线性扫描。
	 */
	const FCapRecord* FindRecordByName(const FString& CapabilityName) const;

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
 */
struct FNexusCapabilityAutoRegister
{
	explicit FNexusCapabilityAutoRegister(TSharedRef<FNexusCapability> Cap)
	{
		FNexusCapabilityRegistry::Get().Register(MoveTemp(Cap));
	}
};

/**
 * Capability 自动注册宏。
 *
 * 用法（在 cap 实现 .cpp 文件末尾）：
 *   REGISTER_MCP_CAPABILITY(FGetActorPropertyCapability)
 *
 * 要求 CapClass 有默认构造函数且继承自 FNexusCapability；
 * Capability 的 GetName() 返回值在全局必须唯一。
 */
#define REGISTER_MCP_CAPABILITY(CapClass) \
	static FNexusCapabilityAutoRegister AutoRegisterCap_##CapClass( \
		MakeShared<CapClass>() \
	);
