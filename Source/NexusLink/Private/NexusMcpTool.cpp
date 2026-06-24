// Copyright byteyang. All Rights Reserved.

#include "NexusMcpTool.h"

const FNexusMcpToolDefinition& FNexusMcpTool::GetDefinition() const
{
	if (!CachedDef.IsSet())
	{
		FNexusMcpToolDefinition Def;
		BuildDefinition(Def);
		CachedDef.Emplace(MoveTemp(Def));
	}
	return CachedDef.GetValue();
}

FNexusMcpToolResult FNexusMcpTool::Execute(const TSharedPtr<FJsonObject>& /*Arguments*/)
{
	// 兜底文案：子类未重写 Execute 又没在全局表注册任何 Capability 时给出明确错误信号。
	FNexusMcpToolResult R;
	R.bIsError  = true;
	R.ErrorText = TEXT("工具未实现 Execute");
	return R;
}
