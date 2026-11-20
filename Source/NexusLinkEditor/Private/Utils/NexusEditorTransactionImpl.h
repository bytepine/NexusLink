// Copyright byteyang. All Rights Reserved.

#pragma once

// 编辑器事务钩子的真实实现（使用 GEditor 和 UTransBuffer，需要 UnrealEd）。
// 由 FNexusLinkEditorModule::StartupModule 安装进 FNexusEditorServices 钩子表。

/** 把真实事务实现安装进 FNexusEditorServices 钩子表；仅编辑器宿主调用。 */
void NexusInstallEditorTransactionHooks();
/** 卸下世界回收时的撤销清理。 */
void NexusShutdownEditorTransactionHooks();
