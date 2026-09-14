// Copyright byteyang. All Rights Reserved.

#pragma once

// 危险 Capability 确认弹窗的真实实现（触碰 Slate / FSlateApplication，仅完整 Editor 宿主可用）。
// 由 FNexusLinkEditorModule::StartupModule 安装进 FNexusEditorServices 钩子表。

/** 把真实确认弹窗实现安装进 FNexusEditorServices 钩子表；仅编辑器宿主调用。 */
void NexusInstallDangerousCapGateHooks();
