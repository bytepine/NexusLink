// Copyright byteyang. All Rights Reserved.
#include "Capabilities/Editor/NexusControlEditorGameFeatureCapability.h"
#if WITH_GAME_FEATURES
#include "Utils/NexusEditorToolSchema.h"
#include "Utils/NexusArgs.h"
#include "NexusCapabilityRegistry.h"
#include "GameFeaturesSubsystem.h"
#include "GameFeaturePluginOperationResult.h"
#include "GameFeatureTypes.h"
#include "Engine/Engine.h"
#include "Interfaces/IPluginManager.h"

// 等待所属会话退出后，底层请求仍可能运行；仅由引擎完成回调释放锁。
static TSet<FString> GNexusGameFeaturePending;
void FNexusControlEditorGameFeatureCapability::BuildDefinition(FNexusCapabilityDefinition& Out) const
{
    Out.Name = TEXT("ControlEditorGameFeature");
    Out.Description = TEXT("Activate/deactivate a discovered built-in Game Feature plugin for this editor session. Waits for the engine callback, reports errors and reads the final state. No arbitrary URLs, unload, uninstall or descriptor changes. Requires UE 5.7+ GameFeatures.");
    Out.InputSchema = FNexusSchema::Object()
        .Required(TEXT("Action"), FNexusSchema::Enum(TEXT("Session lifecycle request"), {TEXT("Activate"), TEXT("Deactivate")}))
        .Required(TEXT("PluginName"), FNexusSchema::Str(TEXT("Exact name from GetProjectGameFeatures.Plugins"))).Build();
    FNexusEditorToolSchema::Configure(Out, true);
    Out.Prerequisites = {TEXT("EditorOnly"), TEXT("PluginEnabled:GameFeatures")};
    Out.ExecutionInfo = []() { auto Info = MakeShared<FJsonObject>(); Info->SetStringField(TEXT("Backend"), TEXT("EngineGameFeatures")); Info->SetStringField(TEXT("Reason"), TEXT("Engine subsystem callback and final-state readback")); return Info; };
    Out.ExtraSearchKeywords = {TEXT("game features"), TEXT("activation"), TEXT("deactivation")}; Out.RelatedCapabilities = {TEXT("GetProjectGameFeatures"), TEXT("GetEditorOperation")};
}
FCapabilityResult FNexusControlEditorGameFeatureCapability::Execute(const TSharedPtr<FJsonObject>& Arguments) const
{
    const FNexusArgs A(Arguments); const FString Action = A.Str(TEXT("Action")), Name = A.Str(TEXT("PluginName"));
    if (Action != TEXT("Activate") && Action != TEXT("Deactivate")) return FCapabilityResult::MakeArgInvalid(TEXT("Action must be Activate or Deactivate"));
    auto* Subsystem = GEngine ? GEngine->GetEngineSubsystem<UGameFeaturesSubsystem>() : nullptr;
    const auto Plugin = IPluginManager::Get().FindPlugin(Name); FString URL;
    if (!Subsystem || !Plugin.IsValid() || !Subsystem->GetBuiltInGameFeaturePluginURL(Plugin.ToSharedRef(), URL)) return FCapabilityResult::MakeArgInvalid(TEXT("PluginName must identify a discovered built-in Game Feature plugin"));
    if (GNexusGameFeaturePending.Contains(Name)) return FCapabilityResult::MakeArgInvalid(TEXT("Another lifecycle operation is running for this plugin"));
    auto Operation = FNexusEditorOperations::Create(TEXT("EngineGameFeatures"));
    if (!Operation.IsValid()) return FNexusEditorToolSchema::Result(Operation);
    GNexusGameFeaturePending.Add(Name);
    const TWeakObjectPtr<UGameFeaturesSubsystem> WeakSubsystem(Subsystem); const bool bActivate = Action == TEXT("Activate");
    auto Complete = [Operation, WeakSubsystem, URL, Name, bActivate](const UE::GameFeatures::FResult& Result)
    {
        GNexusGameFeaturePending.Remove(Name);
        if (!Operation->IsRunning()) return;
        auto* Current = WeakSubsystem.Get(); auto Business = MakeShared<FJsonObject>(); Business->SetStringField(TEXT("PluginName"), Name);
        const bool bActive = Current && Current->IsGameFeaturePluginActive(URL);
        Business->SetBoolField(TEXT("bActive"), bActive); Business->SetStringField(TEXT("State"), Current ? UE::GameFeatures::ToString(Current->GetPluginState(URL)) : TEXT("Unavailable"));
        const bool bApplied = Current && !Result.HasError() && bActive == bActivate; Business->SetBoolField(TEXT("bApplied"), bApplied);
        auto Payload = MakeShared<FJsonObject>(); Payload->SetObjectField(TEXT("OfficialResult"), Business);
        FNexusEditorOperations::Finish(Operation, bApplied ? TEXT("Completed") : TEXT("Failed"), Payload,
            Result.HasError() ? Result.GetError() : bApplied ? FString() : TEXT("Requested final state was not reached"));
    };
    if (bActivate) Subsystem->LoadAndActivateGameFeaturePlugin(URL, FGameFeaturePluginLoadComplete::CreateLambda(Complete));
    else Subsystem->DeactivateGameFeaturePlugin(URL, FGameFeaturePluginDeactivateComplete::CreateLambda(Complete));
    return FNexusEditorToolSchema::Result(Operation);
}
REGISTER_MCP_CAPABILITY(FNexusControlEditorGameFeatureCapability)
#endif
