// Copyright byteyang. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

#if WITH_NEXUS_PYTHON

#include "NexusCapability.h"

/** exec_python —— 在编辑器内执行 Python 脚本，结构化返回 stdout / traceback。*/
class FExecPythonCapability : public FNexusCapability
{
protected:
	virtual void BuildDefinition(FNexusCapabilityDefinition& Out) const override;
	virtual FCapabilityResult Execute(const TSharedPtr<FJsonObject>& Arguments) const override;
};

#endif // WITH_NEXUS_PYTHON
