// Copyright byteyang. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"

class UWidget;
class SWidget;

/**
 * UMG / Slate 布局字段导出（AutoWrapText、PanelSlot 锚点等）。
 * 供 get_asset_user_widget、get_runtime_slate_widget、get_runtime_widget_property 共用。
 */
class NEXUSLINK_API FNexusWidgetLayoutUtils
{
public:
	/** 从 UWidget（编辑器或 PIE）导出 layout 对象；无 Slot 时仅导出控件自身字段。 */
	static void AppendUmgLayoutFields(UWidget* Widget, TSharedPtr<FJsonObject>& InOutEntry);

	/** 将 JSON 字段写入 CanvasPanelSlot（anchor/alignment/offset 等）。 */
	static bool ApplyCanvasSlotFields(UWidget* Widget, const TSharedPtr<FJsonObject>& Fields, FString& OutError);

#if WITH_EDITOR
	/** 从 SWidget 导出 layout：优先关联的 UWidget，否则解析 Slate 父级 Slot（BoxPanel 等）。 */
	static void AppendSlateLayoutFields(const TSharedPtr<SWidget>& Widget, TSharedPtr<FJsonObject>& InOutEntry);
#endif
};
