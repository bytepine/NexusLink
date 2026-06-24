// Copyright byteyang. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "NexusMultiSectionCapability.h"

/** get_editor_context：编辑器选中 Actor/资产与 Content Browser 路径（只读）。 */
class FGetEditorContextCapability : public FNexusMultiSectionCapability
{
protected:
	virtual void BuildDefinition(FNexusCapabilityDefinition& Out) const override;

	virtual TSharedPtr<FJsonObject> BuildCapabilitySchema() const override;
	virtual TArray<FString> GetSectionNames() const override;
	virtual TArray<FString> GetDefaultSectionNames() const override;
	virtual void ExecuteSection(const FString&                 SectionName,
	                            const TSharedPtr<FJsonObject>& Args,
	                            void*                          TargetOpaque,
	                            TSharedPtr<FJsonObject>&       InOutDetail,
	                            FString&                       OutError) const override;
};
