# 运行时 UMG 交互

中文 · [English](runtime-widgets.en.md)

[文档导航](README.md) · [项目首页](../README.md)

适用于具有视口的 PIE / Game。Dedicated Server 没有 UMG；启动与实例选择见[运行时调试](usage-guide.md#241-独立-game--ds-mcp-debug-清单)。

## 定位并交互

1. 用 `ListRuntimeWidgets` 枚举当前控件，取得目标的完整实例 `Path`。
2. 将该值传给 `InteractRuntimeWidget.WidgetPath`，例如设置 `Action="Click"`。
3. 控件实例重建后重新枚举路径；显式路径不存在时不会回退到名称匹配。

读取 Actor 的空间控件时，在 `ListRuntimeWidgets` 指定 `ActorName`。返回的路径同样适用于 `GetRuntimeWidgetProperty.WidgetPath`、`SetRuntimeWidgetProperty.Updates[].WidgetPath` 与交互；组件必须已注册到当前 PIE / Game 世界。

也可用 `WidgetName` 子串匹配唯一目标。多匹配时返回 `Error` 和 `Candidates[].Path`，不执行交互；从候选中确认完整路径后再调用。

## 可见性与点击边界

仅操作当前 PIE / Game 视口或已注册 WidgetComponent 中仍挂载、且沿父级可见的控件。缓存中已脱离活动树的实例、隐藏 Actor/组件/控件层级和 WidgetSwitcher 非活动页均不可交互。

`Click` 触发 UMG 委托，不模拟鼠标坐标，也不验证遮挡。需要操作编辑器 Slate 界面时，使用[编辑器工具](editor-toolsets.md)。完整参数见[工具手册](tool-reference.zh.md#interactruntimewidget)。
