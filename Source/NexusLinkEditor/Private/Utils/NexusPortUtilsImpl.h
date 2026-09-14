// Copyright byteyang. All Rights Reserved.

#pragma once

// 打开设置面板的真实实现（触碰 ISettingsModule，需要 Settings 编辑器模块）。
// 由 FNexusLinkEditorModule::StartupModule 安装进 FNexusEditorServices 钩子表。

/** 把打开设置面板的真实实现安装进 FNexusEditorServices 钩子表；仅编辑器宿主调用。 */
void NexusInstallPortUtilsHooks();
