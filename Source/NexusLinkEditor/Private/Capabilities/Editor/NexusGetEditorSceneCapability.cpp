// Copyright byteyang. All Rights Reserved.
#include "Capabilities/Editor/NexusGetEditorSceneCapability.h"
#include "Utils/NexusSceneTools.h"
#include "Utils/NexusEditorToolSchema.h"
#include "NexusCapabilityRegistry.h"
void FNexusGetEditorSceneCapability::BuildDefinition(FNexusCapabilityDefinition& Out) const
{
    Out.Name = TEXT("GetEditorScene"); Out.Description = TEXT("Inspect folders, collision, BSP and navigation status/octree/tiles. Bounded read-only diagnostics.");
    Out.InputSchema = FNexusSceneTools::Schema(false); FNexusEditorToolSchema::ConfigureIndependent(Out, false, TEXT("NexusScene"));
    Out.ExtraSearchKeywords = {TEXT("scene"),TEXT("folder"),TEXT("selection"),TEXT("trace"),TEXT("navigation status"),TEXT("navmesh"),TEXT("octree"),TEXT("collision"),TEXT("bsp"),TEXT("level streaming"),TEXT("performance")};
}
FCapabilityResult FNexusGetEditorSceneCapability::Execute(const TSharedPtr<FJsonObject>& Args) const { return FNexusSceneTools::Execute(false, Args); }
REGISTER_MCP_CAPABILITY(FNexusGetEditorSceneCapability)
