// Copyright byteyang. All Rights Reserved.

#include "Utils/NexusPythonRuntime.h"

#if WITH_NEXUS_PYTHON

#include "Utils/NexusVersionCompat.h"
#include "IPythonScriptPlugin.h"

bool FNexusPythonRuntime::Acquire(IPythonScriptPlugin*& OutPython, FString& OutError)
{
	OutPython = IPythonScriptPlugin::Get();
	if (!OutPython)
	{
		OutError = TEXT("Python plugin module not loaded; enable Python Editor Script Plugin and restart the editor");
		return false;
	}

	if (!OutPython->IsPythonAvailable())
	{
#if NX_UE_HAS_PYTHON_INIT_STATE_QUERY
		// 5.6+ 可区分「引擎编译时就没带 Python」与「带了但运行时被关掉」
		if (OutPython->IsPythonConfigured())
		{
			OutError = TEXT("Python is configured but disabled at runtime; enable it in Project Settings > Python or restart with Python enabled");
			OutPython = nullptr;
			return false;
		}
#endif
		OutError = TEXT("Python support is not available in this editor build");
		OutPython = nullptr;
		return false;
	}

#if NX_UE_HAS_PYTHON_INIT_STATE_QUERY
	// 已启用但解释器尚未完成初始化时执行会失败，且报错不解释原因
	if (!OutPython->IsPythonInitialized())
	{
		OutError = TEXT("Python is enabled but not initialized yet; retry after editor startup completes");
		OutPython = nullptr;
		return false;
	}
#endif

	return true;
}

#endif // WITH_NEXUS_PYTHON
