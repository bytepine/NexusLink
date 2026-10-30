// Copyright byteyang. All Rights Reserved.
#include "Capabilities/Editor/Toolsets/NexusControlEditorNiagaraCapability.h"
#if WITH_NIAGARA
#include "Utils/NexusEditorToolSchema.h"

#include "Utils/NexusArgs.h"
#include "NexusCapabilityRegistry.h"

void FNexusControlEditorNiagaraCapability::BuildDefinition(FNexusCapabilityDefinition& Out) const
{
    Out.Name = TEXT("ControlEditorNiagara");
    Out.Description = TEXT("Edit Niagara renderers, nested dynamic inputs/data interfaces, emitter/system properties, user variables and set-parameter modules through native UE 5.8 or supported engine compatibility actions. Parameters use exact native camelCase schemas from GetProjectToolsets. Read schema and current values before editing; save assets explicitly. Inspect Result.OfficialResult for business success.");
    auto ActionSchema = FNexusSchema::Enum(TEXT("Deep Niagara write"), {TEXT("SetSystemData"), TEXT("SetEmitterData"), TEXT("AddRenderer"), TEXT("SetRendererData"), TEXT("RemoveRenderer"), TEXT("SetStackInputData"), TEXT("AddUserVariables"), TEXT("RemoveUserVariables"), TEXT("SetModuleEnabled"), TEXT("AddSetParametersModule"), TEXT("AddSetParameterEntry"), TEXT("RemoveSetParameterEntry")});
    Out.InputSchema = FNexusSchema::Object()
        .Required(TEXT("Action"), ActionSchema)
        .Required(TEXT("Parameters"), FNexusSchema::AnyObject(TEXT("Exact native inputSchema fields; empty object for parameterless actions. Use object references returned by inspection; never infer transient paths."))).Build();
    FNexusEditorToolSchema::ConfigureCompatible(Out, true, TEXT("NiagaraToolsets"));
    Out.SearchAssetTypes = {TEXT("NiagaraSystem")};
    Out.ExtraSearchKeywords = {TEXT("niagara"), TEXT("renderer"), TEXT("dynamic input"), TEXT("data interface"), TEXT("user variable")};
    Out.RelatedCapabilities = {TEXT("GetProjectToolsets"), TEXT("GetEditorNiagara"), TEXT("ManageAssetNiagaraSystem"), TEXT("GetEditorOperation")};
}
FCapabilityResult FNexusControlEditorNiagaraCapability::Execute(const TSharedPtr<FJsonObject>& Arguments) const
{
    const FString Action = FNexusArgs(Arguments).Str(TEXT("Action"));
    const TSet<FString> Actions = {TEXT("SetSystemData"), TEXT("SetEmitterData"), TEXT("AddRenderer"), TEXT("SetRendererData"), TEXT("RemoveRenderer"), TEXT("SetStackInputData"), TEXT("AddUserVariables"), TEXT("RemoveUserVariables"), TEXT("SetModuleEnabled"), TEXT("AddSetParametersModule"), TEXT("AddSetParameterEntry"), TEXT("RemoveSetParameterEntry")};
    const TSharedPtr<FJsonObject>* Parameters = nullptr;
    if (!Actions.Contains(Action) || !Arguments->TryGetObjectField(TEXT("Parameters"), Parameters)) return FCapabilityResult::MakeArgInvalid(TEXT("Unknown action or missing Parameters object"));
    auto Operation = FNexusEditorOperations::Create(TEXT("OfficialToolset"));
    FNexusNativeEditorTools::Call(TEXT("NiagaraToolsets"), Action, *Parameters, Operation);
    return FNexusEditorToolSchema::Result(Operation);
}
REGISTER_MCP_CAPABILITY(FNexusControlEditorNiagaraCapability)
#endif
