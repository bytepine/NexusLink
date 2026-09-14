// Copyright byteyang. All Rights Reserved.

#pragma once

// Utils 层：Editor（私有头，仅 NexusLink 模块内部使用）
#include "CoreMinimal.h"

#if WITH_NEXUS_PYTHON

class IPythonScriptPlugin;

/**
 * Python Script Plugin 可用性门面。
 * exec_python / get_python_api 共用，统一把「插件没加载 / 编译期无 Python /
 * 已配置未启用 / 已启用未初始化」四种状态翻译成可执行的排查提示。
 */
class FNexusPythonRuntime
{
public:
	/**
	 * 取可立即执行 Python 的插件实例。
	 * 返回 false 时 OutError 写入面向用户的原因，调用方直接返回即可。
	 */
	static bool Acquire(IPythonScriptPlugin*& OutPython, FString& OutError);
};

#endif // WITH_NEXUS_PYTHON
