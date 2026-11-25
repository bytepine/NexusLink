// Copyright byteyang. All Rights Reserved.

#pragma once
#include "CoreMinimal.h"

#if WITH_MVVM

#include "NexusCapability.h"

/** GetAssetViewModel — 只读检查 Widget 蓝图上挂载的 MVVM ViewModel 与 Binding。UE 5.5+。 */
class FNexusGetAssetViewModelCapability : public FNexusCapability
{
protected:
	virtual void BuildDefinition(FNexusCapabilityDefinition& Out) const override;
	virtual FCapabilityResult Execute(const TSharedPtr<FJsonObject>& Arguments) const override;
};

#endif // WITH_MVVM
