// Copyright byteyang. All Rights Reserved.

#pragma once

// 包整批卸载的真实实现（触碰 UPackageTools / AssetEditorSubsystem / GEditor，需要 UnrealEd）。
// 由 FNexusLinkEditorModule::StartupModule 安装进 FNexusEditorServices 钩子表。

/** 把真实卸载实现安装进 FNexusEditorServices 钩子表；仅编辑器宿主调用。 */
void NexusInstallPackageLedgerHooks();
