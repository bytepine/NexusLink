// Copyright byteyang. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "NexusLinkBuildConfig.h"

#if NEXUSLINK_WITH_SERVER

#include "Widgets/SCompoundWidget.h"
#include "Widgets/DeclarativeSyntaxSupport.h"

struct FCapRecord;
class SVerticalBox;

/**
 * 游戏内 MCP 调试面板（PIE / 独立包通用 Slate 视口叠加层，纯 Runtime，不依赖 UnrealEd）。
 *
 * 上：MCP 运行信息只读展示（来源/监听地址/鉴权）+ 开启/关闭/重启，直接调用与
 * `NexusLink.Mcp` 控制台命令相同的 FNexusMcpActivation + ApplyDesiredMcpState；
 * 下：当前宿主可见 Capability 列表，按 FCapRecord::SourceRelDir 分组折叠，支持过滤，
 * 勾选走 UNexusLinkSettings::SetSessionCapabilityEnabled 会话级覆盖（不写 Preferences/ini）。
 */
class SNexusMcpDebugPanel : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SNexusMcpDebugPanel) {}
		/** 面板请求关闭时回调（Esc 或点击关闭按钮）；由 FNexusMcpDebugOverlay 绑 Close。 */
		SLATE_EVENT(FSimpleDelegate, OnRequestClose)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

	virtual bool SupportsKeyboardFocus() const override { return true; }
	virtual FReply OnKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent) override;

private:
	// ── MCP 状态区 ──────────────────────────────────────────────────────────
	FText  GetMcpStatusText() const;
	FReply OnClickClose();
	FReply OnClickMcpOn();
	FReply OnClickMcpOff();
	FReply OnClickMcpRestart();

	// ── Capability 区 ───────────────────────────────────────────────────────

	/** 单个 cap 在面板里的一行；持有注册表内稳定指针（注册表在引擎初始化后不再新增/重排）。 */
	struct FCapRow
	{
		const FCapRecord* Record = nullptr;
	};

	/** 按 SourceRelDir 分组的一节。 */
	struct FCapGroup
	{
		FString        GroupName;
		TArray<FCapRow> Rows;
	};

	/** 首次构造时按当前宿主可见性扫描注册表一次；运行期 cap 集合不变，只有启停态会变。 */
	void RebuildCapabilityGroups();

	/** 按 FilterText 重新生成可见的分组/行 Slate 树（不改变 Groups 缓存）。 */
	void RebuildCapabilityListWidgets();

	TSharedRef<SWidget> BuildCapabilityGroupWidget(const FCapGroup& Group);
	TSharedRef<SWidget> BuildCapabilityRowWidget(const FCapRecord& Record);

	void OnFilterTextChanged(const FText& NewText);

	ECheckBoxState GetCapCheckState(FString CapName) const;
	void OnCapCheckStateChanged(ECheckBoxState NewState, FString CapName);

	FReply OnClickClearSessionOverrides();
	FText  GetSessionOverrideSummaryText() const;

	TArray<FCapGroup>          Groups;
	TSharedPtr<SVerticalBox>   CapabilityListBox;
	FString                    FilterText;
	FSimpleDelegate            OnRequestClose;
};

#endif // NEXUSLINK_WITH_SERVER
