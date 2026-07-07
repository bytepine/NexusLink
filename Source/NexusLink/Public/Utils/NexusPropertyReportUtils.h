// Copyright byteyang. All Rights Reserved.

#pragma once

// Utils 层：Reflection
#include "CoreMinimal.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"

/**
 * UObject 可编辑属性报告公共工具。
 * 封装 TFieldIterator<FProperty> + CPF_Edit + ExportText + 分页 + 继承标记的通用模式，
 * 供 get_asset_data_asset / get_asset_blueprint(defaults) 等同构 Capability 复用。
 */
class NEXUSLINK_API FNexusPropertyReportUtils final
{
public:
	FNexusPropertyReportUtils() = delete;

	/**
	 * 构建 UObject 可编辑属性的分页 JSON 列表。
	 *
	 * @param Class             待迭代属性的 UClass（通常为 Object->GetClass()）
	 * @param Instance          属性值读取指针（通常为 CDO 或实例本身）
	 * @param LeafClass         用于判断 `inherited` 标记的叶类（属性所在 OwnerClass != LeafClass 时标记 inherited: true）；
	 *                          传 nullptr 则不输出 inherited 字段
	 * @param NameFilter        属性名过滤（空字符串 = 不过滤；支持 FNexusStringMatchUtils 规则）
	 * @param PropertyPaths     首段属性名白名单（空数组 = 不过滤；走 FNexusAssetUtils::MatchesPropertyPathsFilter）
	 * @param Offset            分页偏移
	 * @param Limit             分页大小
	 * @param OutTotal          输出：满足过滤条件的属性总数（分页前）
	 * @param SubobjectDepth    instanced 子对象递归深度（默认 0 = 不递归，保持旧行为零回归）；
	 *                          >0 时对 CPF_InstancedReference / CLASS_EditInlineNew 的非空 FObjectProperty 展开 subProperties
	 * @param SubobjectMaxCount 每层子对象最多展开的属性条数（防病态资产爆炸，默认 16）
	 * @return 当前页的属性 JSON 对象数组，每条含 name / type / value? / inherited? / subProperties?
	 */
	static TArray<TSharedPtr<FJsonValue>> BuildEditablePropsPage(
		UClass*                      Class,
		void*                        Instance,
		UClass*                      LeafClass,
		const FString&               NameFilter,
		const TArray<FString>&       PropertyPaths,
		int32                        Offset,
		int32                        Limit,
		int32&                       OutTotal,
		int32                        SubobjectDepth    = 0,
		int32                        SubobjectMaxCount = 16);
};
