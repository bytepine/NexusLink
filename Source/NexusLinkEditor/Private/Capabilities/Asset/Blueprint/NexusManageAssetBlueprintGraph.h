// Copyright byteyang. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"

struct FNexusActionContext;

/** ManageAssetBlueprint 的图节点与连线操作。 */
struct FNexusBlueprintGraphOps final
{
	static void HandleGraphNode(const TSharedPtr<FJsonObject>& Op, FNexusActionContext& Ctx);
	static void HandleWire(const TSharedPtr<FJsonObject>& Op, FNexusActionContext& Ctx);
};
