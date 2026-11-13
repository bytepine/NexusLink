# Runtime UMG interaction

[中文](runtime-widgets.md) · English

[Documentation](README.en.md) · [Home](../README.en.md)

For PIE / Game hosts with a viewport. Dedicated Server has no UMG; see [runtime debugging](usage-guide.en.md#241-独立-game--ds-mcp-debug-清单) for launch and instance selection.

## Locate and interact

1. Enumerate current widgets with `ListRuntimeWidgets` and obtain the target instance's full `Path`.
2. Pass it as `InteractRuntimeWidget.WidgetPath`, for example `Action="Click"`.
3. Rediscover paths after widgets are rebuilt. A missing explicit path never falls back to name matching.

For an Actor's spatial widgets, supply `ActorName` to `ListRuntimeWidgets`. Returned paths also work with `GetRuntimeWidgetProperty.WidgetPath`, `SetRuntimeWidgetProperty.Updates[].WidgetPath`, and interaction. Components must be registered in the current PIE / Game World.

Alternatively `WidgetName` substring matching must select exactly one target. Multiple matches return `Error` and `Candidates[].Path` without interacting; confirm a complete candidate path before calling again.

## Visibility and click boundaries

Only widgets still attached to the current PIE / Game viewport or a registered WidgetComponent through visible ancestors can be operated. Detached cached instances, hidden Actor/component/widget hierarchies, and inactive WidgetSwitcher pages are ineligible.

`Click` broadcasts UMG delegates. It does not simulate mouse coordinates or test occlusion. Use [editor tools](editor-toolsets.en.md) for editor Slate UI. Full parameters are in the [English reference](tool-reference.md#interactruntimewidget).
