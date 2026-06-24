// Copyright byteyang. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "NexusMultiSectionCapability.h"

class UWidgetBlueprint;

/**
 * get_asset_user_widget — 读取 WidgetBlueprint 控件树与 UMG 动画列表。
 * sections=["widgets"]（默认）| ["animations"] | ["all"]。
 */
class FGetAssetUserWidgetCapability : public FNexusMultiSectionCapability
{
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
};
