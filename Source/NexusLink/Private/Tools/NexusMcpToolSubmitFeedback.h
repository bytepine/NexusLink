// Copyright byteyang. All Rights Reserved.

#pragma once

#include "NexusMcpTool.h"

/**
 * submit_feedback 元工具 —— 供 AI 主动上报 Capability 使用痛点。
 * 调用本工具后数据落盘到 .nexus-feedback/feedback.jsonl，
 * 可在 Editor Preferences → NexusLink → AI 反馈 面板中导出报告给开发者。
 */
class FNexusMcpToolSubmitFeedback : public FNexusMcpTool
{
protected:
	virtual void BuildDefinition(FNexusMcpToolDefinition& Out) const override;
	virtual FNexusMcpToolResult Execute(const TSharedPtr<FJsonObject>& Arguments) override;
};
