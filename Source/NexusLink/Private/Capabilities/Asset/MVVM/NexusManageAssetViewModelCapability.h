// Copyright byteyang. All Rights Reserved.

#pragma once
#include "CoreMinimal.h"
#if WITH_MVVM
#include "NexusCapability.h"

/** manage_asset_view_model：WBP MVVM ViewModel/Binding 增删。UE 5.5+。 */
class FManageAssetViewModelCapability : public FNexusCapability
{
protected:
	virtual void BuildDefinition(FNexusCapabilityDefinition& Out) const override;
	virtual FCapabilityResult Execute(const TSharedPtr<FJsonObject>& Arguments) const override;
};
#endif
