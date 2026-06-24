// Copyright byteyang. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleManager.h"

/**
 * NexusLinkTests —— NexusLink 自动化测试宿主模块。
 *
 * 仅在 Editor 构建下启用；模块本身不做任何运行时工作，只作为
 * UE Automation Framework 收集 IMPLEMENT_SIMPLE_AUTOMATION_TEST 的宿主。
 *
 * 跑法：
 *   UEEditor-Cmd.exe <uproject> -ExecCmds="Automation RunTests NexusLink.; Quit" -unattended -nullrhi
 */
class FNexusLinkTestsModule : public IModuleInterface
{
public:
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;
};
