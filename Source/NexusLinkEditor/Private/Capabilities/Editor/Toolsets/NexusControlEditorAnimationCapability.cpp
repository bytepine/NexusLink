// Copyright byteyang. All Rights Reserved.
#include "Capabilities/Editor/Toolsets/NexusControlEditorAnimationCapability.h"
#if WITH_EDITOR
#include "Utils/NexusEditorToolSchema.h"
#include "Utils/NexusAnimationToolActions.h"
#include "Utils/NexusArgs.h"
#include "NexusCapabilityRegistry.h"

void FNexusControlEditorAnimationCapability::BuildDefinition(FNexusCapabilityDefinition& Out) const
{
    Out.Name = TEXT("ControlEditorAnimation");
    Out.Description = TEXT("Edit Sequencer sections, typed keys, Control Rig controls, baking and animation layers through native UE 5.8 or supported engine compatibility animation actions. Parameters use exact native snake_case fields and refPath references; 5.8 native schemas use GetProjectToolsets; older engines support the documented common subset. Inspect Result.OfficialResult for the business outcome.");
    auto ActionSchema = FNexusSchema::Str(TEXT("Fixed animation action"));
    TArray<FString> Actions; FNexusAnimationToolActions::Get(true).GetKeys(Actions); Actions.Sort();
    TArray<TSharedPtr<FJsonValue>> Choices; for (const auto& Action : Actions) Choices.Add(MakeShared<FJsonValueString>(Action));
    ActionSchema->SetArrayField(TEXT("enum"), Choices);
    Out.InputSchema = FNexusSchema::Object()
        .Required(TEXT("Action"), ActionSchema)
        .Required(TEXT("Parameters"), FNexusSchema::AnyObject(TEXT("Exact native inputSchema fields; empty object for parameterless actions. Use object references returned by inspection; never infer transient paths."))).Build();
    FNexusEditorToolSchema::ConfigureCompatible(Out, true, TEXT("AnimationAssistantToolset"));
    Out.SearchAssetTypes = {TEXT("LevelSequence"), TEXT("ControlRigBlueprint")};
    Out.ExtraSearchKeywords = {TEXT("sequencer"), TEXT("control rig"), TEXT("animation layer"), TEXT("bake"), TEXT("keyframe")};
    Out.RelatedCapabilities = {TEXT("GetProjectToolsets"), TEXT("GetEditorAnimation"), TEXT("ControlEditorAnimation"), TEXT("GetEditorOperation")};
}
FCapabilityResult FNexusControlEditorAnimationCapability::Execute(const TSharedPtr<FJsonObject>& Arguments) const
{
    const FString* Route = FNexusAnimationToolActions::Get(true).Find(FNexusArgs(Arguments).Str(TEXT("Action")));
    const TSharedPtr<FJsonObject>* Parameters = nullptr;
    if (!Route || !Arguments->TryGetObjectField(TEXT("Parameters"), Parameters)) return FCapabilityResult::MakeArgInvalid(TEXT("Unknown action or missing Parameters object"));
    int32 Separator = INDEX_NONE; Route->FindLastChar(TEXT('.'), Separator);
    auto Operation = FNexusEditorOperations::Create(TEXT("OfficialToolset"));
    FNexusNativeEditorTools::Call(Route->Left(Separator), Route->Mid(Separator + 1), *Parameters, Operation);
    return FNexusEditorToolSchema::Result(Operation);
}
REGISTER_MCP_CAPABILITY(FNexusControlEditorAnimationCapability)
#endif
