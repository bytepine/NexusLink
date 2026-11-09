// Copyright byteyang. All Rights Reserved.
#include "Capabilities/Editor/NexusControlEditorSceneCapability.h"
#include "Utils/NexusSceneTools.h"
#include "Utils/NexusEditorToolSchema.h"
#include "NexusCapabilityRegistry.h"
void FNexusControlEditorSceneCapability::BuildDefinition(FNexusCapabilityDefinition& Out) const
{
    Out.Name = TEXT("ControlEditorScene"); Out.Description = TEXT("Organize actors/folders, merge meshes, edit level instances and build navigation asynchronously.");
    Out.InputSchema = FNexusSceneTools::Schema(true); FNexusEditorToolSchema::ConfigureCompatible(Out, true, TEXT("editor_toolset.toolsets.scene.SceneTools"));
    Out.ExtraSearchKeywords = {TEXT("scene"),TEXT("folder"),TEXT("selection"),TEXT("merge"),TEXT("level instance"),TEXT("navigation build"),TEXT("navmesh")};
}
FCapabilityResult FNexusControlEditorSceneCapability::Execute(const TSharedPtr<FJsonObject>& Args) const { return FNexusSceneTools::Execute(true, Args); }
REGISTER_MCP_CAPABILITY(FNexusControlEditorSceneCapability)
