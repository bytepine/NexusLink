// Copyright byteyang. All Rights Reserved.
#include "Capabilities/Editor/NexusControlEditorSceneCapability.h"
#include "Utils/NexusSceneTools.h"
#include "Utils/NexusEditorToolSchema.h"
#include "NexusCapabilityRegistry.h"
void FNexusControlEditorSceneCapability::BuildDefinition(FNexusCapabilityDefinition& Out) const
{
    Out.Name = TEXT("ControlEditorScene"); Out.Description = TEXT("Organize actors/folders, merge meshes, edit level instances and build navigation asynchronously.");
    Out.InputSchema = FNexusSceneTools::Schema(true); FNexusEditorToolSchema::ConfigureIndependent(Out, true, TEXT("NexusScene"));
    Out.ExtraSearchKeywords = {TEXT("scene"),TEXT("folder"),TEXT("selection"),TEXT("merge"),TEXT("level instance"),TEXT("navigation build"),TEXT("navmesh"),TEXT("collision refresh"),TEXT("recreate physics")};
}
FCapabilityResult FNexusControlEditorSceneCapability::Execute(const TSharedPtr<FJsonObject>& Args) const { return FNexusSceneTools::Execute(true, Args); }
REGISTER_MCP_CAPABILITY(FNexusControlEditorSceneCapability)
