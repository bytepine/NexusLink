// Copyright byteyang. All Rights Reserved.
#include "Capabilities/Editor/Toolsets/NexusControlEditorAnimationCapability.h"
#if WITH_EDITOR
#include "Utils/NexusEditorToolSchema.h"
#include "Utils/NexusAnimationToolActions.h"
#include "Utils/NexusAnimationAdvancedTools.h"
#include "Utils/NexusArgs.h"
#include "Utils/NexusEditorDomainTools.h"
#include "NexusCapabilityRegistry.h"

void FNexusControlEditorAnimationCapability::BuildDefinition(FNexusCapabilityDefinition& Out) const
{
    Out.Name = TEXT("ControlEditorAnimation");
    Out.Description = TEXT("Edit Sequencer sections, typed keys, Control Rig controls, baking and animation layers through independent engine APIs. Parameters use snake_case fields and refPath references; inspect GetProjectToolsets Actions for version support and input schemas.");
    auto ActionSchema = FNexusSchema::Str(TEXT("Fixed animation action"));
    TArray<FString> Actions; FNexusAnimationToolActions::Get(true).GetKeys(Actions); Actions.Sort();
    TArray<TSharedPtr<FJsonValue>> Choices; for (const auto& Action : Actions) Choices.Add(MakeShared<FJsonValueString>(Action));
    ActionSchema->SetArrayField(TEXT("enum"), Choices);
    Out.InputSchema = FNexusSchema::Object()
        .Required(TEXT("Action"), ActionSchema)
        .Required(TEXT("Parameters"), FNexusSchema::AnyObject(TEXT("Independent animation input fields; empty object for parameterless actions. Use object references returned by inspection; never infer transient paths."))).Build();
    FNexusEditorToolSchema::ConfigureIndependent(Out, true, TEXT("Animation"));
    Out.ExtraSearchKeywords = {TEXT("sequencer"), TEXT("control rig"), TEXT("animation layer"), TEXT("bake"), TEXT("keyframe")};
    Out.RelatedCapabilities = {TEXT("GetProjectToolsets"), TEXT("GetEditorAnimation"), TEXT("ControlEditorAnimation"), TEXT("GetEditorOperation")};
}
FCapabilityResult FNexusControlEditorAnimationCapability::Execute(const TSharedPtr<FJsonObject>& Arguments) const
{
    const FString* Route = FNexusAnimationToolActions::Get(true).Find(FNexusArgs(Arguments).Str(TEXT("Action")));
    const TSharedPtr<FJsonObject>* Parameters = nullptr;
    if (!Route || !Arguments->TryGetObjectField(TEXT("Parameters"), Parameters)) return FCapabilityResult::MakeArgInvalid(TEXT("Unknown action or missing Parameters object"));
    int32 Separator = INDEX_NONE; Route->FindLastChar(TEXT('.'), Separator);
    TSharedPtr<FJsonObject> Prepared; FString ValidationError;
    if (!FNexusAnimationAdvancedTools::PrepareArguments(Route->Left(Separator), Route->Mid(Separator + 1), *Parameters, Prepared, ValidationError) || !FNexusAnimationAdvancedTools::ValidateFileIO(Route->Mid(Separator+1), Prepared, ValidationError)) return FCapabilityResult::MakeArgInvalid(ValidationError);
    auto Operation = FNexusEditorOperations::Create(TEXT("Independent"));
    FNexusEditorDomainTools::Call(Route->Left(Separator), Route->Mid(Separator + 1), Prepared, Operation);
    return FNexusEditorToolSchema::Result(Operation);
}
REGISTER_MCP_CAPABILITY(FNexusControlEditorAnimationCapability)
#endif
