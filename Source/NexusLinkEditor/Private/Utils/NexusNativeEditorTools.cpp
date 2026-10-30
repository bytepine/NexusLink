// Copyright byteyang. All Rights Reserved.
#include "Utils/NexusEditorToolsets.h"
#include "Utils/NexusVersionCompat.h"
#include "Utils/NexusAnimationToolActions.h"
#include "Utils/NexusLegacyToolsets.h"
#include "Interfaces/IPluginManager.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "Containers/Ticker.h"
#include "Misc/Base64.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "HAL/FileManager.h"
#include "Misc/Guid.h"
#if WITH_NEXUS_TOOLSETS
#include "ToolsetRegistry/UToolsetRegistry.h"
#include "ToolsetRegistry/ToolsetRegistrySubsystem.h"
#include "ToolsetRegistry/ToolsetRegistry.h"
#include "ToolsetRegistry/ToolCallAsyncResultString.h"
#include "UObject/StrongObjectPtr.h"
#endif

static TMap<FString, bool> GNexusNativeDomains;
static uint32 GNexusNativeGeneration = 0;

#if WITH_NEXUS_TOOLSETS
static TWeakObjectPtr<UToolCallAsyncResultString> GNexusNativePcgPending;
static FString NexusNativeToolsetName(const FString& Domain)
{
	if (Domain.StartsWith(TEXT("animation_toolset.toolsets.")) || Domain.StartsWith(TEXT("editor_toolset.toolsets.")) || Domain.StartsWith(TEXT("sequencer_animmixer_toolset."))) return Domain;
    if (Domain == TEXT("PhysicsToolsets")) return TEXT("PhysicsToolsets.PhysicsAssetToolset");
    if (Domain == TEXT("DataRegistryToolset")) return TEXT("DataRegistryToolset.DataRegistryTools");
	if (Domain == TEXT("ToolsetRegistry")) return TEXT("ToolsetRegistry.AgentSkillToolset");
	if (Domain == TEXT("NiagaraToolsets")) return TEXT("NiagaraToolsets.NiagaraToolset_System");
	return Domain + TEXT(".") + Domain;
}

static bool NexusNativeToolAllowed(const FString& Domain, const FString& Tool)
{
	if (Domain.StartsWith(TEXT("animation_toolset.toolsets.")) || Domain.StartsWith(TEXT("sequencer_animmixer_toolset.")))
    {
        const FString FullName = Domain + TEXT(".") + Tool;
        if (Domain == TEXT("animation_toolset.toolsets.sequencer.SequencerTools") && (Tool == TEXT("set_display_rate") || Tool == TEXT("set_playback_range"))) return true;
        if (Domain == TEXT("animation_toolset.toolsets.controlrig.ControlRigTools") && (Tool == TEXT("set_pin_value") || Tool == TEXT("connect_pins") || Tool == TEXT("disconnect_pins"))) return true;
        for (bool bWrite : {false, true}) for (const auto& Pair : FNexusAnimationToolActions::Get(bWrite)) if (Pair.Value == FullName) return true;
        return false;
    }
	static const TMap<FString, TSet<FString>> Allowed = {
        {TEXT("UMGToolSet"), {TEXT("RenameWidget"), TEXT("ToggleWidgetAsVariable"), TEXT("BindToEventProperty"), TEXT("AddUIComponent"), TEXT("RemoveUIComponent"), TEXT("MoveUIComponent")}},
        {TEXT("editor_toolset.toolsets.static_mesh.StaticMeshTools"), {TEXT("set_material"), TEXT("generate_lods"), TEXT("remove_lods"), TEXT("generate_convex_collisions"), TEXT("set_nanite_enabled")}},
        {TEXT("editor_toolset.toolsets.skeletal_mesh.SkeletalMeshTools"), {TEXT("set_material")}},
        {TEXT("editor_toolset.toolsets.material.MaterialTools"), {TEXT("recompile"),TEXT("layout_expressions"),TEXT("delete_unused_expressions"),TEXT("rename_parameter_group"),TEXT("delete_parameter_group"),TEXT("list_parameter_groups")}},
        {TEXT("editor_toolset.toolsets.scene.SceneTools"), {TEXT("get_folders"), TEXT("get_actors_in_folder"), TEXT("set_actor_folder"), TEXT("rename_folder"), TEXT("delete_folder"), TEXT("trace_world"), TEXT("merge_actors"), TEXT("create_level_instance"), TEXT("edit_level_instance")}},
        {TEXT("editor_toolset.toolsets.blueprint.BlueprintTools"), {TEXT("get_connected_subgraph"), TEXT("find_node_types"), TEXT("get_node_type_pins"), TEXT("arrange_nodes"), TEXT("add_function_param"), TEXT("remove_function_param"), TEXT("write_graph_dsl"), TEXT("read_graph_dsl"), TEXT("get_graph_dsl_docs"), TEXT("set_variable_category"), TEXT("set_variable_instance_editable")}},
        {TEXT("ConfigSettingsToolset"), {TEXT("ListContainers"), TEXT("ListCategories"), TEXT("ListSections"), TEXT("SetSectionProperties"), TEXT("SaveSection"), TEXT("ResetSectionToDefaults")}},
        {TEXT("PluginToolset"), {TEXT("CreatePlugin"), TEXT("GetPluginTemplateDescriptions"), TEXT("SetPluginEnabled"), TEXT("GetPluginDescriptor"), TEXT("UpdatePluginDescriptor"), TEXT("AddPluginDependency"), TEXT("RemovePluginDependency"), TEXT("ListEnabledPlugins"), TEXT("ListDiscoveredPlugins"), TEXT("GetPluginInfo"), TEXT("GetPluginDependencies"), TEXT("GetPluginDependents"), TEXT("GetPluginForAsset")}},
        {TEXT("DataRegistryToolset"), {TEXT("ListRegistries"), TEXT("ListItems"), TEXT("GetItems")}},
        {TEXT("GameplayTagsToolset"), {TEXT("ListTags"), TEXT("GetTagInfo"), TEXT("AddTag"), TEXT("RemoveTag"), TEXT("RenameTag"), TEXT("FindReferencersByTag")}},
        {TEXT("PhysicsToolsets"), {TEXT("AddBody"), TEXT("RemoveBody"), TEXT("SetBodyMassScale"), TEXT("SetBodyPhysicsMode"), TEXT("SetSphere"), TEXT("SetCapsule"), TEXT("SetBox"), TEXT("RemoveShape")}},
        {TEXT("MVVMToolset"), {TEXT("RemoveWidgetViewBinding")}},
		{TEXT("SlateInspectorToolset"), {TEXT("Windows"), TEXT("Observe"), TEXT("Unobserve"), TEXT("Snapshot"), TEXT("Screenshot"), TEXT("Click"), TEXT("Type"), TEXT("PressKey"), TEXT("Hover"), TEXT("Drag"), TEXT("SelectOption"), TEXT("FillForm"), TEXT("WaitFor")}},
		{TEXT("AutomationTestToolset"), {TEXT("DiscoverTests"), TEXT("ListTests"), TEXT("RunTests"), TEXT("GetTestStatus"), TEXT("GetTestResults"), TEXT("StopTests")}},
		{TEXT("ToolsetRegistry"), {TEXT("ListSkills"), TEXT("GetSkills")}},
		{TEXT("SemanticSearchToolset"), {TEXT("Search"), TEXT("FindSimilar")}},
		{TEXT("PCGToolset"), {TEXT("ListGraphInstances"), TEXT("GetGraphStructure"), TEXT("GetGraphSchema"), TEXT("ListNativeNodes"), TEXT("GetNativeNodeSchema"), TEXT("SpawnGraphInstance"), TEXT("ExecuteGraphInstance"), TEXT("GetNodeDataView"), TEXT("UpdateNode"), TEXT("SetGraphParams"), TEXT("RemoveGraphParams"), TEXT("SetGraphInstanceParams"), TEXT("ResetGraphInstanceParams"), TEXT("AddSubgraphNode"), TEXT("GetNodeInfo"), TEXT("GetGraphInstanceParams"), TEXT("ListAvailableSubgraphs"), TEXT("RemoveNode"), TEXT("ConnectNodePins"), TEXT("DisconnectNodePins")}},
		{TEXT("NiagaraToolsets"), {TEXT("GetSystemData"), TEXT("GetSystemCompileState"), TEXT("GetStackIssues"), TEXT("GetSystemDependencies"), TEXT("GetModuleTopology"), TEXT("GetModuleSchema"), TEXT("GetModuleInputValues"), TEXT("GetRendererSchema"), TEXT("GetRendererData"), TEXT("GetStackInputSchema"), TEXT("GetStackInputData"), TEXT("GetDynamicInputChain"), TEXT("GetAvailableDynamicInputs"), TEXT("ApplyStackIssueFix"), TEXT("SetSystemData"), TEXT("SetEmitterData"), TEXT("AddRenderer"), TEXT("SetRendererData"), TEXT("RemoveRenderer"), TEXT("SetStackInputData"), TEXT("AddUserVariables"), TEXT("RemoveUserVariables"), TEXT("SetModuleEnabled"), TEXT("AddSetParametersModule"), TEXT("AddSetParameterEntry"), TEXT("RemoveSetParameterEntry"), TEXT("GetSystemSchema"), TEXT("GetEmitterSchema"), TEXT("GetEmitterData"), TEXT("GetUserVariables"), TEXT("GetDataInterfaceSchema"), TEXT("GetSystemSummary"), TEXT("GetEmitterTopology"), TEXT("GetScriptStackTopology"), TEXT("RemoveEmitter"), TEXT("RemoveModule")}}
	};
	const auto* Tools = Allowed.Find(Domain);
	return Tools && Tools->Contains(Tool);
}
#endif

bool FNexusNativeEditorTools::UseNative(const FString& Domain)
{
	if (const bool* bChosen = GNexusNativeDomains.Find(Domain)) return *bChosen;
	bool bUse = false;
#if WITH_NEXUS_TOOLSETS
	const FString PluginName = Domain.StartsWith(TEXT("sequencer_animmixer_toolset.")) ? TEXT("SequencerAnimMixerToolset") : Domain.StartsWith(TEXT("animation_toolset.toolsets.")) ? TEXT("AnimationAssistantToolset") : Domain.StartsWith(TEXT("editor_toolset.toolsets.")) ? TEXT("EditorToolset") : Domain;
    const auto Plugin = IPluginManager::Get().FindPlugin(PluginName);
	bUse = Plugin.IsValid() && Plugin->IsEnabled();
#if !WITH_NEXUS_SLATE_REFS
    if (Domain == TEXT("SlateInspectorToolset")) bUse = false;
#endif
#endif
	GNexusNativeDomains.Add(Domain, bUse);
	return bUse;
}

TSharedPtr<FJsonObject> FNexusNativeEditorTools::Describe(const FString& Capability)
{
    auto Info = MakeShared<FJsonObject>(); FString Backend, Reason;
    if (Capability == TEXT("GetEditorOperation")) { Backend = TEXT("PerOperation"); Reason = TEXT("Read the fixed backend recorded in the selected operation"); }
    else if (Capability == TEXT("CompileEditorLiveCoding")) { Backend = TEXT("LiveCoding"); Reason = TEXT("Requires Windows and an already enabled editor Live Coding session"); }
    else {
        const bool bAutomation = Capability.Contains(TEXT("Automation"));
        const FString Domain = bAutomation ? TEXT("AutomationTestToolset") : TEXT("SlateInspectorToolset");
        const bool bNative = UseNative(Domain);
        Backend = bNative ? TEXT("OfficialToolset") : bAutomation ? TEXT("EngineAutomation") : TEXT("EngineSlate");
        if (bNative) Reason = TEXT("Host enabled the engine toolset; Registry enable and filter rules apply");
        else {
#if WITH_NEXUS_TOOLSETS
            const auto Plugin = IPluginManager::Get().FindPlugin(Domain);
            Reason = Plugin.IsValid() ? TEXT("Host did not enable the engine toolset; using engine APIs") : TEXT("Engine toolset plugin is absent; using engine APIs");
#else
            Reason = TEXT("Official Toolset integration was not compiled for this host; using engine APIs");
#endif
        }
    }
    Info->SetStringField(TEXT("Backend"), Backend); Info->SetStringField(TEXT("Reason"), Reason); return Info;
}

#if WITH_NEXUS_TOOLSETS
static TSharedPtr<FJsonValue> NexusCanonicalAutomationValue(const TSharedPtr<FJsonValue>& Value)
{
    if (Value->Type == EJson::Array) {
        TArray<TSharedPtr<FJsonValue>> Items; for (const auto& Item : Value->AsArray()) Items.Add(NexusCanonicalAutomationValue(Item));
        return MakeShared<FJsonValueArray>(Items);
    }
    if (Value->Type != EJson::Object) return Value;
    auto Result = MakeShared<FJsonObject>();
    for (const auto& Pair : Value->AsObject()->Values) {
        FString Key(*Pair.Key);
        if (Key == TEXT("state")) {
            const FString State = Pair.Value->AsString();
            Result->SetStringField(TEXT("Outcome"), State == TEXT("Success") ? TEXT("Passed") : State == TEXT("Fail") ? TEXT("Failed") : State);
        } else {
            if (Key == TEXT("duration")) Key = TEXT("DurationSeconds"); else if (!Key.IsEmpty()) Key[0] = FChar::ToUpper(Key[0]);
            Result->SetField(Key, NexusCanonicalAutomationValue(Pair.Value));
        }
    }
    return MakeShared<FJsonValueObject>(Result);
}
#endif

bool FNexusNativeEditorTools::CanCall(const FString& Domain, const FString& Tool)
{
#if WITH_NEXUS_TOOLSETS
    if (!NexusNativeToolAllowed(Domain, Tool) || !UseNative(Domain)) return false;
    auto Subsystem = UToolsetRegistrySubsystem::Get();
    if (Subsystem.HasError()) return false;
    const FString Name = NexusNativeToolsetName(Domain);
    auto Handler = Subsystem.GetValue()->ToolsetRegistry.Find(Name);
    return Handler.IsValid() && Handler->IsToolEnabled(Name + TEXT(".") + Tool);
#else
    return false;
#endif
}

bool FNexusNativeEditorTools::InvokeSync(const FString& Domain, const FString& Tool,
    const TSharedPtr<FJsonObject>& Args, TSharedPtr<FJsonValue>& Value, FString& Error)
{
    check(IsInGameThread()); Value.Reset();
#if WITH_NEXUS_TOOLSETS
    const bool bSynchronousDomain = Domain == TEXT("ConfigSettingsToolset") || Domain == TEXT("PluginToolset") || Domain == TEXT("DataRegistryToolset") || Domain == TEXT("GameplayTagsToolset") || Domain == TEXT("PhysicsToolsets") || Domain == TEXT("MVVMToolset") || Domain == TEXT("UMGToolSet") || Domain.StartsWith(TEXT("editor_toolset.toolsets."))
        || (Domain == TEXT("animation_toolset.toolsets.sequencer.SequencerTools") && Tool == TEXT("set_display_rate"))
        || (Domain == TEXT("animation_toolset.toolsets.controlrig.ControlRigTools") && Tool == TEXT("set_pin_value"))
        || (Domain == TEXT("PCGToolset") && (Tool == TEXT("RemoveNode") || Tool == TEXT("ConnectNodePins") || Tool == TEXT("DisconnectNodePins")))
        || (Domain == TEXT("NiagaraToolsets") && Tool == TEXT("RemoveEmitter"));
    if (!bSynchronousDomain) { Error = TEXT("Tool is not approved for synchronous legacy execution"); return false; }
    if (!CanCall(Domain, Tool)) { Error = TEXT("Official tool is unavailable, disabled or filtered: ") + Domain + TEXT(".") + Tool; return false; }
    if (Domain == TEXT("PCGToolset") && GNexusNativePcgPending.IsValid() && !GNexusNativePcgPending->bIsComplete) { Error = TEXT("Another PCG call is running"); return false; }
    auto NativeArgs = MakeShared<FJsonObject>();
    for (const auto& Pair : Args->Values) { FString Key(Pair.Key); if (!Key.IsEmpty()) Key[0] = FChar::ToLower(Key[0]); NativeArgs->SetField(Key, Pair.Value); }
    FString Json; FJsonSerializer::Serialize(NativeArgs, TJsonWriterFactory<>::Create(&Json));
    // 固定方法均为同步 UFUNCTION/Python 函数。Registry 负责权限过滤、转换和脚本异常。
    TStrongObjectPtr<UToolCallAsyncResultString> Result(UToolsetRegistry::ExecuteTool(NexusNativeToolsetName(Domain), Tool, Json));
    if (!Result.IsValid() || !Result->bIsComplete) { Error = TEXT("Native method did not complete synchronously; no compatibility retry was performed"); return false; }
    if (!Result->Error.IsEmpty()) { Error = Result->Error; return false; }
    if (Result->Value.Len() > 2 * 1024 * 1024 || !FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Result->Value), Value)) { Error = TEXT("Invalid or oversized native JSON result"); return false; }
    if (Value.IsValid() && Value->Type == EJson::Object && Value->AsObject()->HasField(TEXT("returnValue"))) Value = Value->AsObject()->TryGetField(TEXT("returnValue"));
    if (Value.IsValid() && Value->Type == EJson::String) { TSharedPtr<FJsonValue> Inner; if (FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Value->AsString()), Inner)) Value = Inner; }
    return Value.IsValid();
#else
    Error = TEXT("Official Toolset integration was not compiled for this host"); return false;
#endif
}

void FNexusNativeEditorTools::Call(const FString& Domain, const FString& Tool,
	const TSharedPtr<FJsonObject>& Args, const TSharedPtr<FNexusEditorOperation>& Operation)
{
    if (!Operation.IsValid()) return;
    if (!UseNative(Domain) && FNexusLegacyToolsets::Handles(Domain)) { FNexusLegacyToolsets::Call(Domain, Tool, Args, Operation); return; }
#if WITH_NEXUS_TOOLSETS
	if (!NexusNativeToolAllowed(Domain, Tool))
	{
		FNexusEditorOperations::Finish(Operation, TEXT("Failed"), nullptr, TEXT("Tool is not allowlisted"));
		return;
	}
	// 使用 Registry 的启用及逐工具过滤，不直接调用 UFUNCTION 绕过它们。
	const FString Name = NexusNativeToolsetName(Domain);
	if (!CanCall(Domain, Tool))
	{
		FNexusEditorOperations::Finish(Operation, TEXT("Failed"), nullptr, FString::Printf(TEXT("Official tool unavailable or disabled: %s.%s; backend remains fixed for this session."), *Name, *Tool));
		return;
	}
	if (Domain == TEXT("PCGToolset") && GNexusNativePcgPending.IsValid() && !GNexusNativePcgPending->bIsComplete)
	{ FNexusEditorOperations::Finish(Operation, TEXT("Failed"), nullptr, TEXT("Another PCG call is running; wait for completion before using shared inspection state")); return; }
	FString Json;
	auto Writer = TJsonWriterFactory<>::Create(&Json);
	auto NativeArgs = MakeShared<FJsonObject>();
    for (const auto& Pair : Args->Values) {
        FString Key(*Pair.Key); if (!Key.IsEmpty()) Key[0] = FChar::ToLower(Key[0]);
        NativeArgs->SetField(Key, Pair.Value);
    }
    if (Tool == TEXT("ListTests")) NativeArgs->SetStringField(TEXT("tagFilter"), TEXT(""));
    FJsonSerializer::Serialize(NativeArgs, Writer);
	auto Pending = MakeShared<TStrongObjectPtr<UToolCallAsyncResultString>>(UToolsetRegistry::ExecuteTool(Name, Tool, Json));
	if (Domain == TEXT("PCGToolset")) GNexusNativePcgPending = Pending->Get();
	const uint32 Generation = GNexusNativeGeneration;
	auto Tick = FTickerDelegate::CreateLambda([Pending, Operation, Generation, Tool, Domain](float)
	{
		if (Generation != GNexusNativeGeneration) return false;
		if (!Pending->IsValid()) { FNexusEditorOperations::Finish(Operation, TEXT("Failed"), nullptr, TEXT("Official tool returned no result")); return false; }
		if (!(*Pending)->bIsComplete) return true;
		if (!Operation->IsRunning()) return false;
		if (!(*Pending)->Error.IsEmpty()) { FNexusEditorOperations::Finish(Operation, TEXT("Failed"), nullptr, (*Pending)->Error); return false; }
		FString JsonResult = (*Pending)->Value;
		const bool bDomainResult = Domain != TEXT("SlateInspectorToolset") && Domain != TEXT("AutomationTestToolset");
		if (bDomainResult && JsonResult.Len() > 2 * 1024 * 1024) { FNexusEditorOperations::Finish(Operation, TEXT("Failed"), nullptr, TEXT("Official result exceeds the 2M-character limit; narrow the query or page its data")); return false; }
		TSharedPtr<FJsonValue> Value;
		auto Reader = TJsonReaderFactory<>::Create(JsonResult);
		FJsonSerializer::Deserialize(Reader, Value);
        // 实测 Registry 的返回是 {returnValue: ...}；FString 内部还可能是一段 JSON。
        if (Value.IsValid() && Value->Type == EJson::Object && Value->AsObject()->HasField(TEXT("returnValue")))
            Value = Value->AsObject()->TryGetField(TEXT("returnValue"));
        if (Value.IsValid() && Value->Type == EJson::String) {
            JsonResult = Value->AsString();
            Reader = TJsonReaderFactory<>::Create(JsonResult);
            TSharedPtr<FJsonValue> Inner;
            if (FJsonSerializer::Deserialize(Reader, Inner)) Value = Inner;
        }
        if (Domain == TEXT("AutomationTestToolset") && Value.IsValid()) Value = NexusCanonicalAutomationValue(Value);
		TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
		if (Value.IsValid() && Value->Type == EJson::Object) Result = Value->AsObject();
		else if (Value.IsValid() && Value->Type == EJson::Number) Result->SetNumberField(TEXT("Value"), Value->AsNumber());
		else if (Value.IsValid() && Value->Type == EJson::Boolean) Result->SetBoolField(TEXT("bApplied"), Value->AsBool());
        else if (Value.IsValid() && Value->Type == EJson::Array) {
            if (Tool == TEXT("Windows")) Result->SetArrayField(TEXT("Windows"), FNexusEditorToolsets::WindowRefs());
            else Result->SetArrayField(TEXT("Items"), Value->AsArray());
        }
        else {
            const FString Text = Value.IsValid() && Value->Type == EJson::String ? Value->AsString() : JsonResult;
            if (Text.StartsWith(TEXT("Error:"))) { FNexusEditorOperations::Finish(Operation, TEXT("Failed"), nullptr, Text); return false; }
            bool bTruncated = Text.Len() > 131072;
            const FString Snapshot = Tool == TEXT("Snapshot") ? FNexusEditorToolsets::BindNativeSnapshot(Text.Left(131072), bTruncated) : Text;
            Result->SetStringField(TEXT("Snapshot"), Snapshot.Left(131072));
            Result->SetBoolField(TEXT("bTruncated"), bTruncated || Snapshot.Len() > 131072);
        }
		FString BusinessError;
		if ((Result->TryGetStringField(TEXT("Error"), BusinessError) || Result->TryGetStringField(TEXT("error"), BusinessError)) && !BusinessError.IsEmpty()) {
            FNexusEditorOperations::Finish(Operation, TEXT("Failed"), nullptr, BusinessError); return false;
        }
        if (Tool == TEXT("Screenshot"))
		{
			FString Data, Mime;
			Result->TryGetStringField(TEXT("Data"), Data); Result->TryGetStringField(TEXT("MimeType"), Mime);
			if (Data.IsEmpty()) Result->TryGetStringField(TEXT("data"), Data);
			TArray<uint8> Bytes;
			const FString Dir = FPaths::ProjectSavedDir() / TEXT("NexusLink/Captures");
			IFileManager::Get().MakeDirectory(*Dir, true);
			const FString Path = FPaths::ConvertRelativePathToFull(Dir / (FGuid::NewGuid().ToString() + TEXT(".png")));
			if (!FBase64::Decode(Data, Bytes) || Bytes.Num() == 0 || Bytes.Num() > 32 * 1024 * 1024 || !FFileHelper::SaveArrayToFile(Bytes, *Path))
			{ FNexusEditorOperations::Finish(Operation, TEXT("Failed"), nullptr, TEXT("Could not save official screenshot")); return false; }
			Result = MakeShared<FJsonObject>(); Result->SetStringField(TEXT("FilePath"), Path);
			if (Operation->bIncludeImage) Operation->ImageFiles.Add(Path);
		}
		if (bDomainResult)
		{
			auto Wrapped = MakeShared<FJsonObject>(); Wrapped->SetObjectField(TEXT("OfficialResult"), Result);
			FNexusEditorOperations::Finish(Operation, TEXT("Completed"), Wrapped); return false;
		}
		const bool bFalseResult = Tool != TEXT("WaitFor") && Value.IsValid() && Value->Type == EJson::Boolean && !Value->AsBool();
		FNexusEditorOperations::Finish(Operation, bFalseResult ? TEXT("Failed") : TEXT("Completed"), Result);
		return false;
	});
	FNexusEditorToolsets::AddTicker(Tick, 0.05f);
#else
	FNexusEditorOperations::Finish(Operation, TEXT("Failed"), nullptr, TEXT("Official toolset backend was not compiled"));
#endif
}

void FNexusNativeEditorTools::Shutdown() { ++GNexusNativeGeneration; GNexusNativeDomains.Empty(); }
