// Copyright byteyang. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "NexusCapability.h"
#include "NexusRuntimeCapability.h"

/** exec_command —— 执行 UE 控制台命令并捕获输出（GEngine->Exec，Dangerous）。*/
class FExecCommandCapability : public FNexusRuntimeCapability
{
protected:
	virtual void BuildDefinition(FNexusCapabilityDefinition& Out) const override;
	virtual FCapabilityResult Execute(const TSharedPtr<FJsonObject>& Arguments) const override;
};
