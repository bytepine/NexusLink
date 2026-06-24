// Copyright byteyang. All Rights Reserved.

#pragma once

#include "NexusMcpTool.h"

/**
 * call_capability 元工具 —— 执行单条 Capability，或经 calls[] 顺序批量执行。
 * 全局按名检索；单条与批量互斥（不可同时传 capability 与 calls）。
 */
class FNexusMcpToolCallCapability : public FNexusMcpTool
{
protected:
	virtual void BuildDefinition(FNexusMcpToolDefinition& Out) const override;
	virtual FNexusMcpToolResult Execute(const TSharedPtr<FJsonObject>& Arguments) override;
};
