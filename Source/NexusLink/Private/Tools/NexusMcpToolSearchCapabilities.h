// Copyright byteyang. All Rights Reserved.

#pragma once

#include "NexusMcpTool.h"

/**
 * search_capabilities 元工具 —— 输入一句话描述意图，返回匹配的 Capability 列表（含名称、描述、参数）。
 * 评分/目录/参数提取逻辑统一由 FNexusCapabilityIndexUtils 承载。
 */
class FNexusMcpToolSearchCapabilities : public FNexusMcpTool
{
protected:
	virtual void BuildDefinition(FNexusMcpToolDefinition& Out) const override;
	virtual FNexusMcpToolResult Execute(const TSharedPtr<FJsonObject>& Arguments) override;
};
