// Copyright byteyang. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "NexusLinkSettings.generated.h"

/**
 * 响应输出格式。
 */
UENUM()
enum class ENexusContentMode : uint8
{
	/** content[0].text（完整 JSON 字符串），兼容所有客户端。 */
	Content,
	/** structuredContent（原生 JSON 对象），需客户端支持（如 Claude Desktop），省去文本序列化开销。 */
	StructuredContent,
};

/**
 * MCP 工具列表暴露模式。
 */
UENUM()
enum class ENexusToolsListMode : uint8
{
	/** 搜索工具模式（默认）：暴露 search_capabilities + call_capability + submit_feedback，AI 先搜索再调用。 */
	SearchMode,
	/** 多 Tool 模式：每个已启用 Capability 作为独立 MCP Tool 暴露，AI 可直接调用。 */
	MultiTool,
};

/**
 * NexusLink 插件配置。
 * 位于编辑器菜单：Edit → Editor Preferences → Plugins → NexusLink。
 */
UCLASS(Config = EditorPerProjectUserSettings, meta = (DisplayName = "NexusLink"))
class NEXUSLINK_API UNexusLinkSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	UNexusLinkSettings();

	/** 获取单例。 */
	static UNexusLinkSettings* Get();

	/**
	 * 是否启用 NexusLink MCP 服务器。
	 * 默认关闭；勾选后启动 HTTP/WebSocket 服务并注册实例供 IDE 代理发现。
	 * 可在编辑器偏好中随时切换，无需重启。
	 */
	UPROPERTY(Config, EditAnywhere, Category = "服务器",
		meta = (DisplayName = "启用 MCP 服务器",
			ToolTip = "勾选：启动 MCP/WebSocket 服务；取消：完全停止服务并注销实例"))
	bool bEnableMcpServer = false;

	/**
	 * MCP 服务器实际监听端口（只读）。
	 * 由插件启动时自动分配，端口冲突时自动顺延，无需手动配置。
	 */
	UPROPERTY(Transient, VisibleAnywhere, Category = "服务器",
		meta = (DisplayName = "MCP 端口（实际）"))
	int32 McpPort = 0;

	/**
	 * WebSocket 服务器实际监听端口（只读）。
	 * 由插件启动时自动分配，端口冲突时自动顺延，无需手动配置。
	 */
	UPROPERTY(Transient, VisibleAnywhere, Category = "服务器",
		meta = (DisplayName = "WebSocket 端口（实际）"))
	int32 WsPort = 0;

	/**
	 * 是否在编辑器状态栏和视口覆盖层中显示端口号。
	 * 关闭时完全隐藏，不显示任何内容；开启后显示 MCP/WS 实际端口。
	 */
	UPROPERTY(Config, EditAnywhere, Category = "服务器",
		meta = (DisplayName = "在状态栏显示端口号"))
	bool bShowPort = false;

	/**
	 * 是否启用 MCP 响应默认值压缩（list_actors / search_asset / get_asset / diff_actors 等）。
	 * 开启后：数组条目里主流值字段抽到顶部 <prefix>_defaults，条目内等值字段随之省略；
	 * 关闭后：工具返回原始完整条目，便于排查 AI 解析异常或做字节级对比。
	 */
	UPROPERTY(Config, EditAnywhere, Category = "服务器",
		meta = (DisplayName = "响应默认值压缩",
			ToolTip = "勾选：数组条目主流值字段抽到顶部 defaults 以节省 token；取消：工具返回完整条目"))
	bool bCompactResponseDefaults = true;

	/**
	 * 响应输出格式（下拉选择）。
	 *   - Content（默认）：仅输出 content[0].text（完整 JSON 字符串），兼容所有客户端。
	 *   - StructuredContent：仅输出 structuredContent（原生 JSON 对象），需客户端支持。
	 */
	UPROPERTY(Config, EditAnywhere, Category = "服务器",
		meta = (DisplayName = "响应输出格式",
			ToolTip = "Content=兼容模式（默认）；StructuredContent=原生 JSON（需客户端支持）"))
	ENexusContentMode ContentMode = ENexusContentMode::Content;

	/**
	 * MCP 工具列表暴露模式。
	 *   - SearchMode（默认）：暴露 search_capabilities + call_capability + submit_feedback，AI 先搜索再调用。
	 *   - MultiTool：将每个已启用 Capability 作为独立 MCP Tool 暴露，AI 可直接调用。
	 */
	UPROPERTY(Config, EditAnywhere, Category = "服务器",
		meta = (DisplayName = "工具列表模式",
			ToolTip = "SearchMode=搜索工具模式（默认）；MultiTool=每个 Capability 直接暴露为独立 Tool"))
	ENexusToolsListMode ToolsListMode = ENexusToolsListMode::SearchMode;

	/**
	 * 日志捕获白名单（日志分类名列表）。
	 * 为空时捕获全部日志（默认）；非空时仅捕获列表中的分类，
	 * 可大幅减少噪音、保留更多有效条目（缓冲区上限 2000 条）。
	 * 示例：LogTemp、LogBlueprintUserMessages、LogNexusLink
	 */
	UPROPERTY(Config, EditAnywhere, Category = "日志捕获",
		meta = (DisplayName = "日志分类白名单", ToolTip = "留空=捕获全部；填写后只捕获指定分类（大小写不敏感）"))
	TArray<FString> LogCaptureCategories;

	// ── AI 反馈 ────────────────────────────────────────────────────────────────

	/**
	 * 是否启用 Capability 使用反馈记录。
	 * 开启后：search_capabilities / call_capability 的失败事件及 AI 手动上报均写入
	 * .nexus-feedback/feedback.jsonl；关闭后所有埋点立即 return，不落盘任何数据。
	 */
	UPROPERTY(Config, EditAnywhere, Category = "AI 反馈",
		meta = (DisplayName = "启用反馈记录",
			ToolTip = "勾选：记录 Capability 搜索/调用失败事件与 AI 手动反馈；取消：完全关闭落盘"))
	bool bEnableFeedback = true;

	/**
	 * search_capabilities 结果数超过此阈值时记录 search_overflow 反馈事件。
	 * 较低的阈值能更灵敏地发现"搜索词过于宽泛"的问题；
	 * 提高阈值则只在命中极多时才触发（减少噪音）。
	 */
	UPROPERTY(Config, EditAnywhere, Category = "AI 反馈",
		meta = (DisplayName = "搜索过载阈值",
			ToolTip = "search_capabilities 结果数超过此值时触发 search_overflow 记录",
			ClampMin = "2", ClampMax = "50"))
	int32 SearchOverflowThreshold = 5;

	/**
	 * search_capabilities 单次最多返回的结果条数（兜底截断）。
	 * 即使评分逻辑命中多条，也最多返回此数量（按 score 降序取前 N）。
	 * 设为 0 表示不限制。
	 */
	UPROPERTY(Config, EditAnywhere, Category = "AI 反馈",
		meta = (DisplayName = "搜索结果数上限",
			ToolTip = "search_capabilities 最多返回条数，0 = 不限制",
			ClampMin = "0", ClampMax = "50"))
	int32 MaxSearchResults = 8;

	/**
	 * Capability 执行时间超过此阈值时记录 slow_call 反馈事件（毫秒）。
	 * 默认 1500ms；设为 0 可临时关闭 slow_call 采集。
	 */
	UPROPERTY(Config, EditAnywhere, Category = "AI 反馈",
		meta = (DisplayName = "慢调用阈值（毫秒）",
			ToolTip = "Capability 执行耗时超过此值时触发 slow_call 记录，0 = 关闭",
			ClampMin = "0", ClampMax = "30000"))
	int32 SlowCallThresholdMs = 1500;

	/**
	 * redundant_call 检测窗口（秒）。
	 * 同一 capability + identity 在此窗口内已做过 sections=["all"] 调用后，
	 * 再发子 section 即触发 redundant_call 记录。
	 */
	UPROPERTY(Config, EditAnywhere, Category = "AI 反馈",
		meta = (DisplayName = "冗余调用检测窗口（秒）",
			ToolTip = "同 cap+identity 窗口内重复发子 section 的检测时长，0 = 关闭",
			ClampMin = "0", ClampMax = "300"))
	int32 RedundantCallWindowSec = 30;

	/**
	 * 导出报告时归档目录保留的最近文件数上限。
	 * 超出的最旧归档自动删除，防止磁盘无限增长。
	 */
	UPROPERTY(Config, EditAnywhere, Category = "AI 反馈",
		meta = (DisplayName = "最大归档数量",
			ToolTip = "导出报告时保留最近 N 份归档，超出自动删除",
			ClampMin = "5", ClampMax = "200"))
	int32 MaxArchiveCount = 20;

	/**
	 * GitHub Issue 预填目标仓库。
	 * 支持 `owner/repo`（默认 bytepine/NexusLink）或完整 GitHub 仓库/Issue 页 URL。
	 * 用于设置面板「创建 GitHub Issue」与导出报告中的预填链接。
	 */
	UPROPERTY(Config, EditAnywhere, Category = "AI 反馈",
		meta = (DisplayName = "Issue 目标仓库",
			ToolTip = "owner/repo 或 https://github.com/owner/repo；留空则默认 bytepine/NexusLink"))
	FString FeedbackIssueRepo;

	/**
	 * 启动时是否自动在后台检查 NexusLink 版本更新。
	 * 有新版本时弹出非阻塞通知；无新版本或网络不通时静默。
	 */
	UPROPERTY(Config, EditAnywhere, Category = "插件信息",
		meta = (DisplayName = "启动时自动检查更新",
			ToolTip = "勾选：编辑器启动后后台静默查询 GitHub Releases，有新版本时弹出通知；取消：禁用自动检查"))
	bool bCheckUpdateOnStartup = true;

	/**
	 * 已禁用的 Capability 名集合（cap 名全局唯一，不再带 host 前缀）。
	 * 首次启动默认全部启用，可在设置面板按分类切换。
	 */
	UPROPERTY(Config)
	TSet<FString> DisabledCapabilities;

	/** 已应用过默认模式的 Capability 名集合；不在此集合中的新 cap 默认启用。 */
	UPROPERTY(Config)
	TSet<FString> KnownCapabilityKeys;

	/** 是否已经为现有 cap 应用过默认启用值（首次启动后置 true）。 */
	UPROPERTY(Config)
	bool bCapabilityDefaultsApplied = false;

	/** 判断指定 cap 是否启用（不在 DisabledCapabilities 中即为启用）。 */
	bool IsCapabilityEnabled(const FString& CapabilityName) const;

	/**
	 * 设置 cap 启用/禁用状态。
	 * @param bNotify 是否立即持久化；批量操作时传 false，最后调用 NotifyCapabilitiesChanged()。
	 */
	void SetCapabilityEnabled(const FString& CapabilityName, bool bEnabled, bool bNotify = true);

	/** 持久化 DisabledCapabilities。批量 SetCapabilityEnabled(…, false) 后统一调用。 */
	void NotifyCapabilitiesChanged();

	/** 首次启动时把当前已注册 cap 全部纳入 KnownCapabilityKeys（默认启用）。 */
	void EnsureDefaultCapabilityMode();

	// UDeveloperSettings interface
	virtual FName GetCategoryName() const override;
#if WITH_EDITOR
	// UE 4.26 起 `UDeveloperSettings::GetSectionText()` 带 `#if WITH_EDITOR` 声明，
	// 为保证新增 UPROPERTY 后跨版本编译稳定，这里对齐同样的守卫。
	virtual FText GetSectionText() const override;

	/** 属性修改后回调：白名单变更时同步更新日志捕获器过滤规则。 */
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif
};
