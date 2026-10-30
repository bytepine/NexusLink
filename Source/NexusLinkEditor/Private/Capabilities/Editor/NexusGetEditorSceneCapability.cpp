// Copyright byteyang. All Rights Reserved.
#include "Capabilities/Editor/NexusGetEditorSceneCapability.h"
#include "Utils/NexusSceneTools.h"
#include "Utils/NexusEditorToolSchema.h"
#include "NexusCapabilityRegistry.h"
void FNexusGetEditorSceneCapability::BuildDefinition(FNexusCapabilityDefinition& Out) const
{
    Out.Name = TEXT("GetEditorScene"); Out.Description = TEXT("Inspect editor folders, selection, folder actors and visibility traces.");
    Out.InputSchema = FNexusSceneTools::Schema(false); FNexusEditorToolSchema::ConfigureCompatible(Out, false, TEXT("editor_toolset.toolsets.scene.SceneTools"));
    Out.ExtraSearchKeywords = {TEXT("scene"),TEXT("folder"),TEXT("selection"),TEXT("trace")};
}
FCapabilityResult FNexusGetEditorSceneCapability::Execute(const TSharedPtr<FJsonObject>& Args) const { return FNexusSceneTools::Execute(false, Args); }
REGISTER_MCP_CAPABILITY(FNexusGetEditorSceneCapability)
