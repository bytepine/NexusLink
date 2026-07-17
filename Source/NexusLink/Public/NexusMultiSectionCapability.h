// Copyright byteyang. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "NexusCapability.h"

/**
 * Multi-section Capability 基类。
 *
 * 继承此类即获得以下框架能力（无需子类手动实现）：
 *   - sections: string[] 入参（枚举 + "all"）自动注入 schema
 *   - 未传 / 空 → GetDefaultSectionNames() 兜底
 *   - "all" → GetSectionNames() 全集
 *   - ExpandPerEntry() 多 entry 批量展开
 *   - PrepareEntry() locator 写入 + Target 获取
 *   - ExecuteSection() 逐 section 执行，错误写入 entry.sectionErrors[]
 *   - entry.sections = 实际覆盖列表
 *
 * 子类必须实现：
 *   BuildDefinition（填写 Name/Description/Tags/ExtraSearchKeywords 等，
 *                   InputSchema 调用 BuildSchemaWithSections() 自动注入 sections 枚举）
 *   BuildCapabilitySchema / GetSectionNames / ExecuteSection
 *
 * 子类可选 override：
 *   GetDefaultSectionNames / PrepareEntry / ExpandPerEntry
 */
class NEXUSLINK_API FNexusMultiSectionCapability : public FNexusCapability
{
public:
	/**
	 * Execute 最终实现：走完整 multi-section 路径。
	 * 子类不可 override（用 ExecuteSection() 代替）。
	 */
	virtual FCapabilityResult Execute(const TSharedPtr<FJsonObject>& Arguments) const override final;

protected:
	// ── 子类代替 BuildSchema() 实现的方法 ────────────────────────────────────────

	/** 声明除 sections 以外的所有入参 schema；sections 字段由 BuildSchemaWithSections() 自动注入。 */
	virtual TSharedPtr<FJsonObject> BuildCapabilitySchema() const = 0;

	/**
	 * 辅助方法：调用 BuildCapabilitySchema() 并自动注入 sections 枚举数组字段。
	 * 子类在 BuildDefinition() 中通过 Out.InputSchema = BuildSchemaWithSections() 使用。
	 */
	TSharedPtr<FJsonObject> BuildSchemaWithSections() const;

	// ── Multi-section 钩子 ───────────────────────────────────────────────────────

	/** 全部支持的 section 名（snake_case）。决定 sections 字段 enum 范围 + "all" 展开值。 */
	virtual TArray<FString> GetSectionNames() const = 0;

	/**
	 * 未传 sections / 空数组时使用的默认 section 列表。
	 * 默认返回 GetSectionNames() 全集（即不传 = 全部）。
	 * 子类可 override 为常用子集（如 {"variable", "function"}）。
	 */
	virtual TArray<FString> GetDefaultSectionNames() const { return GetSectionNames(); }

	/**
	 * 解析入参，写 locator 字段（assetPath/actorName/...）并输出不透明 Target 指针。
	 * 失败时填 OutError + 返回 false；该 entry 跳过所有 section，仅输出 error 字段。
	 * 无需 target 的 cap 可不 override（默认返回 true，target = nullptr）。
	 */
	virtual bool PrepareEntry(const TSharedPtr<FJsonObject>& Args,
	                          TSharedPtr<FJsonObject>&       OutEntry,
	                          void*&                         OutTargetOpaque,
	                          FString&                       OutError) const { return true; }

	/**
	 * 执行单个 section，把字段平铺写入 InOutDetail。
	 * OutError 非空 → 该 section 错误追加到 entry.sectionErrors[]，不中断其他 section。
	 */
	virtual void ExecuteSection(const FString&                 SectionName,
	                            const TSharedPtr<FJsonObject>& Args,
	                            void*                          TargetOpaque,
	                            TSharedPtr<FJsonObject>&       InOutDetail,
	                            FString&                       OutError) const = 0;

	/**
	 * 多 entry 展开：将入参拆为若干条单 entry 参数对象（如 assetPaths → 多个含单 assetPath）。
	 * 返回空（默认）= 单 entry，不展开。
	 */
	virtual TArray<TSharedPtr<FJsonObject>> ExpandPerEntry(const TSharedPtr<FJsonObject>& Args) const { return {}; }

	/**
	 * 是否在每个 entry 处理完后检查内存高水位阈值、按需整批卸载本次调用引入的包
	 * （FNexusPackageLedger，见 NexusPackageLedger.h）。
	 * 默认：只读 Capability（带 FNexusMcpTags::Readonly 标签）为 true；写类 Capability 为 false
	 * （写操作可能故意留 dirty 待后续 save_asset，不应被自动卸载打断）。
	 * 子类通常无需 override：PrepareEntry/ExecuteSection 内改用 FNexusAssetUtils::LoadAssetTracked 加载即可接入本机制。
	 */
	virtual bool ShouldAutoUnloadIntrospected() const;

private:
	FCapabilityResult RunMultiSection(const TSharedPtr<FJsonObject>& Args) const;
};
