// Copyright byteyang. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#if WITH_MOVIE_RENDER_PIPELINE && WITH_EDITOR
#include "NexusCapability.h"

/** ControlMoviePipeline：入队 / 查询 / 取消 MRQ，不阻塞整段渲染。UE5+。 */
class FNexusControlMoviePipelineCapability : public FNexusCapability
{
protected:
	virtual void BuildDefinition(FNexusCapabilityDefinition& Out) const override;
	virtual FCapabilityResult Execute(const TSharedPtr<FJsonObject>& Arguments) const override;
};
#endif
