// Copyright byteyang. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"

/** 预定义标签常量，避免手写字符串导致拼写不一致。 */
struct FNexusMcpTags
{
	static constexpr const TCHAR* Readonly  = TEXT("readonly");
	static constexpr const TCHAR* Write     = TEXT("write");
	static constexpr const TCHAR* Editor    = TEXT("editor");
	static constexpr const TCHAR* Blueprint = TEXT("blueprint");
	static constexpr const TCHAR* Material  = TEXT("material");
	static constexpr const TCHAR* Struct    = TEXT("struct");
	static constexpr const TCHAR* Data      = TEXT("data");
	static constexpr const TCHAR* Widget    = TEXT("widget");
	static constexpr const TCHAR* Runtime   = TEXT("runtime");
	static constexpr const TCHAR* Gas       = TEXT("gas");
};

/**
 * MCP 工具定义结构 —— 对应 MCP 规范中的 Tool Object。
 */
struct FNexusMcpToolDefinition
{
	/** 工具唯一标识符。 */
	FString Name;

	/** 工具描述。 */
	FString Description;

	/**
	 * 输入参数 JSON Schema（直接存储为 JsonObject）。
	 * 典型结构：{ "type": "object", "properties": {...}, "required": [...] }
	 */
	TSharedPtr<FJsonObject> InputSchema;

	/**
	 * 工具标签（访问级别 + 功能分类）。
	 * 用于设置面板按标签过滤（如"只读模式"按 readonly 标签筛选）。
	 * 使用 FNexusMcpTags 中的 static constexpr 常量赋值。
	 */
	TArray<FString> Tags;

	bool HasTag(const FString& Tag) const { return Tags.Contains(Tag); }
};

/**
 * MCP 工具执行结果。
 *
 * 成功路径：只填 StructuredContent，Dispatcher 统一把它序列化到 content[0].text
 *           并放到 result.structuredContent。
 * 错误路径：bIsError = true + ErrorText；Dispatcher 把 ErrorText 放到 content[0].text。
 *
 * 极少数需要返回"预格式化文本"的工具可填
 * OutputText；非空时 Dispatcher 用它覆盖默认的 StructuredContent 序列化文本，但
 * structuredContent 字段仍然会一并返回。常规工具**不要**使用 OutputText。
 */
struct FNexusMcpToolResult
{
	/** 是否执行出错（工具级错误，非协议级）。 */
	bool bIsError = false;

	/** 错误描述文本（bIsError=true 时使用）。 */
	FString ErrorText;

	/** 结构化返回数据（成功路径的主要数据出口）。 */
	TSharedPtr<FJsonObject> StructuredContent;

	/**
	 * 可选：覆盖 content[0].text 的预格式化文本（如 markdown）。
	 * 仅用于工具输出本身就是给人读的纯文本；常规情况留空即可。
	 */
	FString OutputText;
};

/**
 * MCP 工具基类。
 *
 * 新增 MCP Tool：
 *   1. 继承此基类，实现 BuildDefinition() 和 ExecuteImpl()
 *   2. .cpp 末尾 REGISTER_MCP_TOOL(YourToolClass) 注册
 *
 * Capability 通过 REGISTER_MCP_CAPABILITY 自注册到全局表，
 * 由 search_capabilities / call_capability 元工具索引和驱动，不再绑定到 Tool。
 *
 * 基类负责：懒加载调用 BuildDefinition 并缓存 FNexusMcpToolDefinition。
 */
class NEXUSLINK_API FNexusMcpTool
{
public:
	virtual ~FNexusMcpTool() = default;

	/** 元数据：首次调用触发 BuildDefinition，后续使用实例级缓存。 */
	const FNexusMcpToolDefinition& GetDefinition() const;

	/**
	 * 填写本 Tool 的所有元数据：Out.Name / Out.Description / Out.InputSchema / Out.Tags。
	 * 仅在首次 GetDefinition() 时调用一次，结果被实例级缓存。
	 */
	virtual void BuildDefinition(FNexusMcpToolDefinition& Out) const = 0;

	/**
	 * 执行工具逻辑。错误通过返回值的 bIsError + ErrorText 上报，不抛异常。
	 *
	 * 默认实现返回兜底错误文案——子类未重写且全局表未注册对应 Capability 时
	 * 不会编译期报错，但运行时立即给出明确错误信号。
	 */
	virtual FNexusMcpToolResult Execute(const TSharedPtr<FJsonObject>& Arguments);

private:
	mutable TOptional<FNexusMcpToolDefinition> CachedDef;
};
