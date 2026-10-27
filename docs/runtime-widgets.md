# 运行时 UMG 交互 / Runtime UMG interaction

[文档导航 / Documentation](README.md) · [项目首页 / Home](../README.md)

适用于具有视口的 PIE / Game。Dedicated Server 没有 UMG；启动与实例选择见[运行时调试](usage-guide.md#241-独立-game--ds-mcp-debug-清单)。

## 定位并交互

1. 用 `ListRuntimeWidgets` 枚举当前控件，取得目标的完整实例 `Path`。
2. 将该值传给 `InteractRuntimeWidget.WidgetPath`，例如设置 `Action="Click"`。
3. 控件实例重建后重新枚举路径；显式路径不存在时不会回退到名称匹配。

也可用 `WidgetName` 子串匹配唯一目标。多匹配时返回 `Error` 和 `Candidates[].Path`，不执行交互；从候选中确认完整路径后再调用。

## 可见性与点击边界

仅操作当前 PIE / Game 视口中仍挂载、且沿父级可见的控件。缓存中已脱离视口的实例、隐藏层级和 WidgetSwitcher 非活动页均不可交互。

`Click` 触发 UMG 委托，不模拟鼠标坐标，也不验证遮挡。需要操作编辑器 Slate 界面时，使用[编辑器工具](editor-toolsets.md)。完整参数见[工具手册](tool-reference.zh.md#interactruntimewidget)。

## English

In PIE / Game with a viewport, call `ListRuntimeWidgets` and pass the live instance's full `Path` as `InteractRuntimeWidget.WidgetPath`, for example with `Action="Click"`. Refresh paths after instances are recreated. An explicit path never falls back to a name.

`WidgetName` substring matching must identify one target. Multiple matches return an `Error` and `Candidates[].Path` without interacting. Only widgets attached to the current viewport through visible ancestors are eligible; detached caches, hidden branches, and inactive WidgetSwitcher pages are excluded. Click broadcasts the UMG delegate; it does not simulate mouse coordinates or test occlusion. Dedicated Server has no UMG. See the [English reference](tool-reference.md#interactruntimewidget) for parameters.
