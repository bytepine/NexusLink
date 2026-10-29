// Copyright byteyang. All Rights Reserved.
#include "Capabilities/Editor/NexusGetProjectGameFeaturesCapability.h"
#if WITH_GAME_FEATURES
#include "NexusCapabilityRegistry.h"
#include "NexusMcpSchemaBuilder.h"
#include "NexusMcpTool.h"
#include "Utils/NexusArgs.h"
#include "GameFeaturesSubsystem.h"
#include "GameFeatureTypes.h"
#include "Engine/Engine.h"
#include "Interfaces/IPluginManager.h"

void FNexusGetProjectGameFeaturesCapability::BuildDefinition(FNexusCapabilityDefinition& Out) const
{
    Out.Name = TEXT("GetProjectGameFeatures"); Out.Description = TEXT("Discover built-in Game Feature plugins recognized by the engine settings, or read their current state. Only project/engine plugin names from discovery are accepted; arbitrary URLs are not accepted. Requires UE 5.7+ GameFeatures.");
    Out.InputSchema = BuildSchemaWithSections(); Out.Tags = {FNexusMcpTags::Readonly, FNexusMcpTags::Editor}; Out.bCacheable = false;
    Out.Prerequisites = {TEXT("EditorOnly"), TEXT("PluginEnabled:GameFeatures")}; Out.ExtraSearchKeywords = {TEXT("game features"), TEXT("gameplay plugin"), TEXT("activation")};
    Out.RelatedCapabilities = {TEXT("ControlEditorGameFeature"), TEXT("GetProjectPlugins")};
}
TSharedPtr<FJsonObject> FNexusGetProjectGameFeaturesCapability::BuildCapabilitySchema() const
{
    return FNexusSchema::Object()
        .Prop(TEXT("PluginName"), FNexusSchema::Str(TEXT("Exact built-in Game Feature plugin name for State")))
        .Prop(TEXT("NameFilter"), FNexusSchema::Str(TEXT("Name substring for Plugins")))
        .Prop(TEXT("Offset"), FNexusSchema::Int(TEXT("Pagination offset"), 0, 0))
        .Prop(TEXT("Limit"), FNexusSchema::Int(TEXT("Maximum plugins returned"), 100, 1, 500)).Build();
}
TArray<FString> FNexusGetProjectGameFeaturesCapability::GetSectionNames() const { return {TEXT("Plugins"), TEXT("State")}; }
TArray<FString> FNexusGetProjectGameFeaturesCapability::GetDefaultSectionNames() const { return {TEXT("Plugins")}; }
void FNexusGetProjectGameFeaturesCapability::ExecuteSection(const FString& SectionName, const TSharedPtr<FJsonObject>& Args, void*, TSharedPtr<FJsonObject>& Out, FString& Error) const
{
    auto* Subsystem = GEngine ? GEngine->GetEngineSubsystem<UGameFeaturesSubsystem>() : nullptr;
    if (!Subsystem) { Error = TEXT("GameFeatures subsystem is unavailable"); return; }
    const FNexusArgs A(Args); TArray<TSharedPtr<FJsonValue>> Items;
    auto Plugins = IPluginManager::Get().GetDiscoveredPlugins(); Plugins.Sort([](const TSharedRef<IPlugin>& Left, const TSharedRef<IPlugin>& Right) { return Left->GetName() < Right->GetName(); });
    for (const auto& Plugin : Plugins)
    {
        if (SectionName == TEXT("State") ? Plugin->GetName() != A.Str(TEXT("PluginName")) : !Plugin->GetName().Contains(A.Str(TEXT("NameFilter")))) continue;
        FString URL; if (!Subsystem->GetBuiltInGameFeaturePluginURL(Plugin, URL)) continue;
        auto Item = MakeShared<FJsonObject>(); Item->SetStringField(TEXT("Name"), Plugin->GetName()); Item->SetBoolField(TEXT("bEnabled"), Plugin->IsEnabled());
        Item->SetStringField(TEXT("State"), UE::GameFeatures::ToString(Subsystem->GetPluginState(URL))); Item->SetBoolField(TEXT("bActive"), Subsystem->IsGameFeaturePluginActive(URL));
        if (SectionName == TEXT("State")) { Out->SetObjectField(TEXT("State"), Item); return; }
        Items.Add(MakeShared<FJsonValueObject>(Item));
    }
    if (SectionName == TEXT("State")) { Error = TEXT("PluginName must identify a discovered built-in Game Feature plugin"); return; }
    Out->SetNumberField(TEXT("Total"), Items.Num());
    const int32 Offset = FMath::Clamp((int32)A.Num(TEXT("Offset")), 0, Items.Num()), Limit = FMath::Clamp((int32)A.Num(TEXT("Limit"), 100), 1, 500);
    TArray<TSharedPtr<FJsonValue>> Page; for (int32 Index = Offset; Index < Items.Num() && Page.Num() < Limit; ++Index) Page.Add(Items[Index]);
    Out->SetNumberField(TEXT("Returned"), Page.Num()); Out->SetArrayField(TEXT("Plugins"), Page);
}
REGISTER_MCP_CAPABILITY(FNexusGetProjectGameFeaturesCapability)
#endif
