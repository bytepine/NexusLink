// Copyright byteyang. All Rights Reserved.

#include "NexusMcpTool.h"

// C++14 中按引用使用 constexpr 静态成员仍需要类外定义，并供其他模块链接。
constexpr const TCHAR* FNexusMcpTags::Readonly;
constexpr const TCHAR* FNexusMcpTags::Write;
constexpr const TCHAR* FNexusMcpTags::Dangerous;
constexpr const TCHAR* FNexusMcpTags::Editor;
constexpr const TCHAR* FNexusMcpTags::Blueprint;
constexpr const TCHAR* FNexusMcpTags::Material;
constexpr const TCHAR* FNexusMcpTags::Struct;
constexpr const TCHAR* FNexusMcpTags::Data;
constexpr const TCHAR* FNexusMcpTags::Widget;
constexpr const TCHAR* FNexusMcpTags::Runtime;
constexpr const TCHAR* FNexusMcpTags::Gas;

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
