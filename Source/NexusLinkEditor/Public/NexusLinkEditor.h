// Copyright byteyang. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleManager.h"

/**
 * NexusLinkEditor 模块 —— 承载 195 个 EditorOnly Capability、编辑器 Utils、
 * Slate 设置定制面板 / 状态栏 / 版本检查。仅完整 Editor 宿主加载。
 *
 * StartupModule 里做两件 NexusLink（Runtime）本身做不了的事：
 *   1. 把真实编辑器实现装进 FNexusEditorServices 钩子表（事务 / 危险 Capability 确认 /
 *      资产 finalize / 包卸载 / 打开设置面板 / 服务器启停通知），供 Runtime 模块反向调用；
 *   2. 注册 PropertyEditor 定制、状态栏，并绑定自己的 OnPostEngineInit 做启动版本检查
 *      （不能塞进 NexusLink::OnPostEngineInit，因为 FNexusUpdateChecker 现在住在本模块）。
 */
class FNexusLinkEditorModule : public IModuleInterface
{
public:
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;

private:
	/** 启动一次性版本检查通知（仅 GIsEditor 且 Settings->bCheckUpdateOnStartup）。 */
	void OnPostEngineInit();
};
