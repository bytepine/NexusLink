// Copyright byteyang. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

#if WITH_NEXUS_PYTHON

#include "NexusCapability.h"

/** get_python_api —— 内省当前引擎 unreal 模块成员，返回签名与 docstring 首行。*/
class FGetPythonApiCapability : public FNexusCapability
{
protected:
	virtual void BuildDefinition(FNexusCapabilityDefinition& Out) const override;
	virtual FCapabilityResult Execute(const TSharedPtr<FJsonObject>& Arguments) const override;
};

#endif // WITH_NEXUS_PYTHON
