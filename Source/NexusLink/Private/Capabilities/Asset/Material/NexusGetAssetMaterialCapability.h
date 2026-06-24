// Copyright byteyang. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"
#include "NexusMultiSectionCapability.h"
#include "Utils/NexusAssetUtils.h"

class UMaterialExpression;

/**
 * get_asset 的 Material / MaterialInstanceConstant / MaterialFunction 子能力。
 * 继承 FNexusMultiSectionCapability，sections=["overview","params","graph"]。
 * 默认 section：overview。支持 assetPaths[] 批量查询。
 */
class FGetAssetMaterialCapability : public FNexusMultiSectionCapability
{
public:
	/** Material / MaterialFunction 共用的 expression graph 序列化。 */
	static TSharedPtr<FJsonObject> BuildMaterialExpressionGraph(
		const TArray<UMaterialExpression*>& Expressions,
		const FString& NodeFilter, bool bIncludePins, bool bIncludeWires,
		int32 Offset, int32 Limit);

protected:
	virtual void BuildDefinition(FNexusCapabilityDefinition& Out) const override;

	virtual TSharedPtr<FJsonObject> BuildCapabilitySchema() const override;
	virtual TArray<FString> GetSectionNames() const override;
	virtual TArray<FString> GetDefaultSectionNames() const override;
	virtual bool PrepareEntry(const TSharedPtr<FJsonObject>& Args,
	                          TSharedPtr<FJsonObject>&       OutEntry,
	                          void*&                         OutTargetOpaque,
	                          FString&                       OutError) const override;
	virtual void ExecuteSection(const FString&                 SectionName,
	                            const TSharedPtr<FJsonObject>& Args,
	                            void*                          TargetOpaque,
	                            TSharedPtr<FJsonObject>&       InOutDetail,
	                            FString&                       OutError) const override;
	virtual TArray<TSharedPtr<FJsonObject>> ExpandPerEntry(const TSharedPtr<FJsonObject>& Args) const override;
};
