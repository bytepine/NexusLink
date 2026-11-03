// Copyright byteyang. All Rights Reserved.
#include "Utils/NexusEditorActionSchema.h"
#include "Utils/NexusEditorToolsets.h"
#include "Utils/NexusAnimationToolActions.h"
#include "Utils/NexusAnimationAdvancedTools.h"
#include "Utils/NexusAssetUtils.h"
#include "Interfaces/IPluginManager.h"
#include "MovieSceneSection.h"
#include "MovieSceneTrack.h"
#include "NexusCapabilityRegistry.h"
#include "NexusMcpSchemaBuilder.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#if WITH_NEXUS_TOOLSETS
#include "ToolsetRegistry/ToolsetRegistrySubsystem.h"
#include "ToolsetRegistry/ToolsetRegistry.h"
#endif

static TSharedRef<FJsonObject> NexusAnimationParameterType(FProperty* Property, int32 Depth = 0)
{
	if (Depth > 8) return FNexusSchema::AnyObject(TEXT("Nested value"));
	if (CastField<FObjectPropertyBase>(Property)) return FNexusSchema::Object().Required(TEXT("refPath"), FNexusSchema::Str(TEXT("Discovered object/class reference"))).Build().ToSharedRef();
	if (auto* Array = CastField<FArrayProperty>(Property)) return FNexusSchema::ArrayOf(TEXT("At most 128 elements"), NexusAnimationParameterType(Array->Inner, Depth + 1));
	if (auto* Struct = CastField<FStructProperty>(Property))
	{
		if (Struct->Struct == TBaseStructure<FGuid>::Get()) return FNexusSchema::Str(TEXT("GUID"));
		auto Schema = FNexusSchema::Object();
		for (TFieldIterator<FProperty> It(Struct->Struct); It; ++It) Schema.Prop(*It->GetName(), NexusAnimationParameterType(*It, Depth + 1));
		return Schema.Build().ToSharedRef();
	}
	if (CastField<FBoolProperty>(Property)) return FNexusSchema::Bool(TEXT("Boolean"));
	if (const auto* Number = CastField<FNumericProperty>(Property)) return Number->IsInteger() ? FNexusSchema::Int(TEXT("Integer")) : FNexusSchema::Num(TEXT("Number"));
	return FNexusSchema::Str(TEXT("Text or enum value"));
}

static TSharedPtr<FJsonObject> NexusCompatibilityParameters(const FString& Domain, const FString& Tool)
{
	auto Ref = FNexusSchema::Object().Required(TEXT("refPath"), FNexusSchema::Str(TEXT("Discovered object path"))).Build().ToSharedRef();
	if (Domain.StartsWith(TEXT("sequencer_animmixer_toolset.")))
	{
		UFunction* Function = FNexusAnimationAdvancedTools::FindMixerFunction(Tool);
		if (!Function) return nullptr;
		auto Schema = FNexusSchema::Object();
		for (TFieldIterator<FProperty> It(Function); It && It->HasAnyPropertyFlags(CPF_Parm); ++It)
			if (!It->HasAnyPropertyFlags(CPF_ReturnParm)) Schema.Required(*It->GetName(), NexusAnimationParameterType(*It));
		return Schema.Build();
	}
	if (Domain.StartsWith(TEXT("animation_toolset.")) || Domain == TEXT("NexusSequenceFolders"))
	{
		auto Schema = FNexusSchema::Object();
		if (Domain.Contains(TEXT(".conditions.")))
		{
			const bool bSection = Tool.Contains(TEXT("section")), bRow = Tool.Contains(TEXT("track_row"));
			FStructProperty* Container = FindFProperty<FStructProperty>(bSection ? UMovieSceneSection::StaticClass() : UMovieSceneTrack::StaticClass(), TEXT("ConditionContainer"));
			if (bRow)
			{
				FMapProperty* Rows = FindFProperty<FMapProperty>(UMovieSceneTrack::StaticClass(), TEXT("TrackRowMetadata"));
				FStructProperty* Metadata = Rows ? CastField<FStructProperty>(Rows->ValueProp) : nullptr;
				Container = Metadata ? FindFProperty<FStructProperty>(Metadata->Struct, TEXT("ConditionContainer")) : nullptr;
			}
			if (!Container || !FindFProperty<FObjectPropertyBase>(Container->Struct, TEXT("Condition"))) return nullptr;
			Schema.Required(bSection ? TEXT("section") : TEXT("track"), Ref);
			if (bRow) Schema.Required(TEXT("row_index"), FNexusSchema::Int(TEXT("Existing track row index"), 0, 0, 10000));
			if (Tool.StartsWith(TEXT("set_"))) Schema.Required(TEXT("condition_class"), FNexusSchema::Str(TEXT("Concrete MovieScene condition class path")));
			return Schema.Build();
		}
		if (Tool == TEXT("export_fbx") || Tool == TEXT("import_fbx"))
		{
			const auto Plugin = IPluginManager::Get().FindPlugin(TEXT("SequencerScripting"));
			UClass* Library = FNexusAssetUtils::FindClassWithUPrefix(TEXT("SequencerToolsFunctionLibrary"));
			if (!Plugin || !Plugin->IsEnabled() || !Library) return nullptr;
			Schema.Required(TEXT("sequence"), Ref).Required(TEXT("fbx_file_path"), FNexusSchema::Str(TEXT("New export or existing import .fbx path"))).Prop(TEXT("world"), Ref)
				.Prop(TEXT("bindings"), FNexusSchema::ArrayOf(TEXT("Discovered sequencer binding proxies"), FNexusSchema::AnyObject(TEXT("Binding proxy"))));
			Schema.Prop(Tool == TEXT("export_fbx") ? TEXT("override_options") : TEXT("import_settings"), Ref);
			return Schema.Build();
		}
		if (Tool == TEXT("get_bindings") || Tool == TEXT("get_tracks_on_sequence") || Tool == TEXT("get_sequence_folders"))
			return Schema.Required(TEXT("sequence"), Ref).Build();
		if (Tool == TEXT("add_track_to_sequence")) return Schema.Required(TEXT("sequence"), Ref).Required(TEXT("track_type"), Ref).Build();
		if (Tool == TEXT("get_sections") || Tool == TEXT("add_section")) return Schema.Required(TEXT("track"), Ref).Build();
		if (Tool == TEXT("remove_section")) return Schema.Required(TEXT("track"), Ref).Required(TEXT("section"), Ref).Build();
		if (Tool == TEXT("add_sequence_folder") || Tool == TEXT("remove_sequence_folder"))
			return Schema.Required(TEXT("sequence"), Ref).Required(TEXT("folder_name"), FNexusSchema::Str(TEXT("Folder name"))).Build();
		const TSet<FString> SectionTools = {TEXT("get_section_range"), TEXT("get_section_properties"), TEXT("set_section_range"), TEXT("set_section_ease_in"), TEXT("set_section_ease_out"), TEXT("get_channel_names"), TEXT("get_keys"), TEXT("remove_key_at_frame"), TEXT("add_key_float"), TEXT("add_key_bool"), TEXT("add_key_integer"), TEXT("add_key_string")};
		if (!SectionTools.Contains(Tool)) return nullptr;
		Schema.Required(TEXT("section"), Ref);
		if (Tool == TEXT("set_section_range")) Schema.Required(TEXT("start_frame"), FNexusSchema::Int(TEXT("Inclusive display frame"))).Required(TEXT("end_frame"), FNexusSchema::Int(TEXT("Exclusive display frame")));
		if (Tool.StartsWith(TEXT("set_section_ease_"))) Schema.Required(TEXT("duration"), FNexusSchema::Int(TEXT("Nonnegative display frame count"), 0, 0));
		if (Tool == TEXT("get_keys") || Tool == TEXT("remove_key_at_frame") || Tool.StartsWith(TEXT("add_key_"))) Schema.Required(TEXT("channel_name"), FNexusSchema::Str(TEXT("Exact discovered channel name")));
		if (Tool == TEXT("remove_key_at_frame") || Tool.StartsWith(TEXT("add_key_"))) Schema.Required(TEXT("frame"), FNexusSchema::Int(TEXT("Display frame")));
		if (Tool == TEXT("add_key_float")) Schema.Required(TEXT("value"), FNexusSchema::Num(TEXT("Key value"))).Prop(TEXT("interpolation"), FNexusSchema::Enum(TEXT("Interpolation"), {TEXT("Auto"), TEXT("Linear"), TEXT("Constant")}));
		if (Tool == TEXT("add_key_bool")) Schema.Required(TEXT("value"), FNexusSchema::Bool(TEXT("Key value")));
		if (Tool == TEXT("add_key_integer")) Schema.Required(TEXT("value"), FNexusSchema::Int(TEXT("Key value")));
		if (Tool == TEXT("add_key_string")) Schema.Required(TEXT("value"), FNexusSchema::Str(TEXT("Key value")));
		return Schema.Build();
	}
	if (Domain == TEXT("NiagaraToolsets"))
	{
		auto Schema = FNexusSchema::Object();
		if (Tool == TEXT("AddUserVariables") || Tool == TEXT("RemoveUserVariables"))
		{
			auto Variable = FNexusSchema::Object().Required(TEXT("name"), FNexusSchema::Str(TEXT("Unique User.* name")))
				.Required(TEXT("type"), FNexusSchema::Object().Required(TEXT("classStructOrEnum"), Ref).Build().ToSharedRef())
				.Prop(TEXT("defaultValue"), FNexusSchema::Object().Prop(TEXT("value"), FNexusSchema::AnyObject(TEXT("Fields of the selected supported value struct"))).Build().ToSharedRef()).Build().ToSharedRef();
			return Schema.Required(TEXT("system"), Ref).Required(Tool == TEXT("AddUserVariables") ? TEXT("variablesToAdd") : TEXT("variablesToRemove"), FNexusSchema::ArrayOf(TEXT("1-64 variables; value structs only"), Variable)).Build();
		}
		if (Tool == TEXT("GetUserVariables") || Tool == TEXT("GetSystemData")) return Schema.Required(TEXT("system"), Ref).Build();
		if (Tool == TEXT("SetSystemData")) return Schema.Required(TEXT("system"), Ref).Required(TEXT("systemData"), FNexusSchema::Object().Required(TEXT("propertyValues"), FNexusSchema::Str(TEXT("JSON object of editable system properties"))).Build().ToSharedRef()).Build();
		auto Emitter = FNexusSchema::Object().Required(TEXT("system"), Ref).Required(TEXT("emitterName"), FNexusSchema::Str(TEXT("Discovered emitter name"))).Prop(TEXT("rendererIndex"), FNexusSchema::Int(TEXT("Discovered renderer index"), 0, 0)).Build().ToSharedRef();
		if (Tool == TEXT("AddRenderer")) return Schema.Required(TEXT("newRendererLocation"), Emitter).Required(TEXT("rendererClass"), Ref).Build();
		if (Tool == TEXT("GetRendererData")) return Schema.Required(TEXT("renderer"), Emitter).Build();
		if (Tool == TEXT("RemoveRenderer")) return Schema.Required(TEXT("rendererToRemove"), Emitter).Build();
		if (Tool == TEXT("SetRendererData")) return Schema.Required(TEXT("renderer"), Emitter).Required(TEXT("rendererData"), FNexusSchema::Object().Required(TEXT("propertyValues"), FNexusSchema::Str(TEXT("JSON object of editable renderer properties"))).Build().ToSharedRef()).Build();
	}
	return nullptr;
}

static TSharedPtr<FJsonObject> NexusNativeParameters(const FString& Domain, const FString& Tool)
{
#if WITH_NEXUS_TOOLSETS
	if (!FNexusNativeEditorTools::CanCall(Domain, Tool)) return nullptr;
	const FString ToolsetName = Domain == TEXT("NiagaraToolsets") ? TEXT("NiagaraToolsets.NiagaraToolset_System") : Domain == TEXT("PCGToolset") ? TEXT("PCGToolset.PCGToolset") : Domain;
	auto Subsystem = UToolsetRegistrySubsystem::Get();
	if (Subsystem.HasError()) return nullptr;
	auto Handler = Subsystem.GetValue()->ToolsetRegistry.Find(ToolsetName);
	TArray<TSharedPtr<FJsonValue>> Schemas;
	if (Handler.IsValid() && FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(TEXT("[") + Handler->GetJsonSchema() + TEXT("]")), Schemas))
		for (const auto& Value : Schemas)
		{
			const TArray<TSharedPtr<FJsonValue>>* Tools = nullptr;
			if (!Value->AsObject()->TryGetArrayField(TEXT("tools"), Tools)) continue;
			for (const auto& Entry : *Tools)
			{
				const auto Item = Entry->AsObject(); FString Name; const TSharedPtr<FJsonObject>* Input = nullptr;
				if (Item->TryGetStringField(TEXT("name"), Name) && Name == ToolsetName + TEXT(".") + Tool && Item->TryGetObjectField(TEXT("inputSchema"), Input)) return *Input;
			}
		}
#endif
	return nullptr;
}

static TSharedPtr<FJsonObject> NexusFlatActionInput(const FCapRecord& Record, const FString& Action, bool bNative)
{
	FString Json; FJsonSerializer::Serialize(Record.Def.InputSchema.ToSharedRef(), TJsonWriterFactory<>::Create(&Json));
	TSharedPtr<FJsonObject> Schema; if (!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Json), Schema)) return nullptr;
	const auto Properties = Schema->GetObjectField(TEXT("properties"));
	Properties->SetObjectField(TEXT("Action"), FNexusSchema::Enum(TEXT("Selected action"), {*Action}));
	TArray<TSharedPtr<FJsonValue>> Required = {MakeShared<FJsonValueString>(TEXT("Action"))};
	const auto Require = [&](const TCHAR* Key) { Required.Add(MakeShared<FJsonValueString>(Key)); };
	if (Record.Def.Name.Contains(TEXT("PCG")))
	{
		if (Action == TEXT("GraphStructure") || Action == TEXT("GraphSchema") || Action == TEXT("Spawn") || Action.Contains(TEXT("GraphParameters")) || Action == TEXT("AddSubgraph")) Require(TEXT("AssetPath"));
		if (Action == TEXT("NodeInfo") || Action == TEXT("NodeData") || Action == TEXT("UpdateNode")) Require(TEXT("NodePath"));
		if (Action == TEXT("InstanceParameters") || Action == TEXT("NodeData") || Action == TEXT("Execute") || Action.Contains(TEXT("InstanceParameters"))) Require(TEXT("ActorPath"));
		if (Action == TEXT("Spawn")) Require(TEXT("ActorName"));
		if (Action == TEXT("NativeNodeSchema")) Require(TEXT("NodeName"));
		if (Action == TEXT("UpdateNode") || Action == TEXT("SetInstanceParameters")) Require(TEXT("Parameters"));
		if (Action == TEXT("RemoveGraphParameters") || Action == TEXT("ResetInstanceParameters")) Require(TEXT("ParameterNames"));
		if (Action == TEXT("AddSubgraph")) { Require(TEXT("SubgraphPath")); Require(TEXT("NodeName")); }
		if (Action == TEXT("SetGraphParameters"))
		{
			Require(TEXT("ParameterDefinitions"));
			if (!bNative)
			{
				const auto Definition = Properties->GetObjectField(TEXT("ParameterDefinitions"))->GetObjectField(TEXT("items"))->GetObjectField(TEXT("properties"));
				Definition->SetObjectField(TEXT("Type"), FNexusSchema::Enum(TEXT("Compatibility scalar type"), {TEXT("Float"), TEXT("Double"), TEXT("Integer32"), TEXT("Integer64"), TEXT("String"), TEXT("Boolean"), TEXT("Name")}));
				Definition->SetObjectField(TEXT("ContainerType"), FNexusSchema::Enum(TEXT("Compatibility container"), {TEXT("None")}));
			}
		}
	}
	else
	{
		const bool bClassOnly = Action == TEXT("RendererSchema") || Action == TEXT("AvailableDynamicInputs") || Action == TEXT("DataInterfaceSchema");
		if (bClassOnly) Require(TEXT("ClassPath"));
		else if (Action != TEXT("SystemSchema") && Action != TEXT("EmitterSchema")) Require(TEXT("AssetPath"));
		if (Action == TEXT("EmitterData") || Action == TEXT("EmitterTopology") || Action == TEXT("RendererData")) Require(TEXT("EmitterName"));
		if (Action == TEXT("RendererData")) Require(TEXT("RendererIndex"));
		if (Action == TEXT("ScriptTopology") || Action.StartsWith(TEXT("Module")) || Action.StartsWith(TEXT("Input")) || Action == TEXT("DynamicInputChain")) Require(TEXT("ScriptName"));
		if (Action.StartsWith(TEXT("Module")) || Action.StartsWith(TEXT("Input")) || Action == TEXT("DynamicInputChain")) Require(TEXT("ModuleName"));
		if (Action.StartsWith(TEXT("Input")) || Action == TEXT("DynamicInputChain")) Require(TEXT("InputNameStack"));
	}
	Schema->SetArrayField(TEXT("required"), Required); return Schema;
}

TArray<TSharedPtr<FJsonValue>> FNexusEditorActionSchema::List(const FString& Capability, const FString& Action)
{
	TArray<TSharedPtr<FJsonValue>> Rows;
	for (const FString& Name : {TEXT("GetEditorAnimation"), TEXT("ControlEditorAnimation"), TEXT("GetEditorPCG"), TEXT("ControlEditorPCG"), TEXT("GetEditorNiagara"), TEXT("ControlEditorNiagara")})
	{
		if (!Capability.IsEmpty() && Capability != Name) continue;
		const FCapRecord* Record = FNexusCapabilityRegistry::Get().FindRecordByName(Name);
		if (!Record) continue;
		const bool bAnimation = Name.Contains(TEXT("Animation")), bWrite = Name.StartsWith(TEXT("Control"));
		TMap<FString, FString> Routes;
		if (bAnimation) Routes = FNexusAnimationToolActions::Get(bWrite);
		else
		{
			const auto Properties = Record->Def.InputSchema->GetObjectField(TEXT("properties"));
			for (const auto& Choice : Properties->GetObjectField(TEXT("Action"))->GetArrayField(TEXT("enum"))) Routes.Add(Choice->AsString(), Choice->AsString());
		}
		for (const auto& Pair : Routes)
		{
			if (!Action.IsEmpty() && Action != Pair.Key) continue;
			FString Domain = Name.Contains(TEXT("PCG")) ? TEXT("PCGToolset") : TEXT("NiagaraToolsets"), Tool = Pair.Value;
			if (bAnimation) { int32 Separator; Tool.FindLastChar(TEXT('.'), Separator); Domain = Tool.Left(Separator); Tool = Tool.Mid(Separator + 1); }
			else if (!bWrite && Domain == TEXT("NiagaraToolsets"))
			{
				Tool = Pair.Key == TEXT("CompileState") ? TEXT("GetSystemCompileState") : Pair.Key == TEXT("Dependencies") ? TEXT("GetSystemDependencies") : Pair.Key == TEXT("ScriptTopology") ? TEXT("GetScriptStackTopology") : TEXT("Get") + Pair.Key;
				if (Pair.Key == TEXT("InputSchema") || Pair.Key == TEXT("InputData")) Tool = TEXT("GetStack") + Pair.Key;
			}
			else if (Domain == TEXT("PCGToolset"))
			{
				static const TMap<FString, FString> Mapping = {{TEXT("Instances"),TEXT("ListGraphInstances")},{TEXT("GraphStructure"),TEXT("GetGraphStructure")},{TEXT("GraphSchema"),TEXT("GetGraphSchema")},{TEXT("NativeNodes"),TEXT("ListNativeNodes")},{TEXT("NativeNodeSchema"),TEXT("GetNativeNodeSchema")},{TEXT("NodeData"),TEXT("GetNodeDataView")},{TEXT("NodeInfo"),TEXT("GetNodeInfo")},{TEXT("InstanceParameters"),TEXT("GetGraphInstanceParams")},{TEXT("AvailableSubgraphs"),TEXT("ListAvailableSubgraphs")},{TEXT("Spawn"),TEXT("SpawnGraphInstance")},{TEXT("Execute"),TEXT("ExecuteGraphInstance")},{TEXT("SetGraphParameters"),TEXT("SetGraphParams")},{TEXT("RemoveGraphParameters"),TEXT("RemoveGraphParams")},{TEXT("SetInstanceParameters"),TEXT("SetGraphInstanceParams")},{TEXT("ResetInstanceParameters"),TEXT("ResetGraphInstanceParams")},{TEXT("AddSubgraph"),TEXT("AddSubgraphNode")}};
				if (const auto* Mapped = Mapping.Find(Pair.Key)) Tool = *Mapped;
			}
			const bool bNative = FNexusNativeEditorTools::UseNative(Domain);
			auto Parameters = bNative ? NexusNativeParameters(Domain, Tool) : NexusCompatibilityParameters(Domain, Tool);
			bool bSupported = Parameters.IsValid();
			if (!bNative && Domain == TEXT("PCGToolset"))
			{
				const TSet<FString> Supported = {TEXT("ListGraphInstances"), TEXT("GetGraphSchema"), TEXT("GetNodeInfo"), TEXT("GetGraphInstanceParams"), TEXT("UpdateNode"), TEXT("SetGraphParams"), TEXT("RemoveGraphParams"), TEXT("SetGraphInstanceParams"), TEXT("ResetGraphInstanceParams"), TEXT("ExecuteGraphInstance"), TEXT("GetNodeDataView")};
                bSupported = Supported.Contains(Tool) || Tool == TEXT("SpawnGraphInstance");
			}
			auto Row = MakeShared<FJsonObject>();
			Row->SetStringField(TEXT("Name"), Pair.Key); Row->SetStringField(TEXT("Capability"), Name);
			Row->SetStringField(TEXT("ToolName"), Tool); Row->SetStringField(TEXT("ToolsetName"), Domain);
			Row->SetStringField(TEXT("Backend"), bNative ? TEXT("OfficialToolset") : TEXT("EngineCompatibility"));
			Row->SetBoolField(TEXT("bSupported"), bSupported);
			Row->SetArrayField(TEXT("Prerequisites"), {MakeShared<FJsonValueString>(TEXT("EditorOnly")), MakeShared<FJsonValueString>(Domain == TEXT("PCGToolset") ? TEXT("PluginEnabled:PCG") : Domain == TEXT("NiagaraToolsets") ? TEXT("PluginEnabled:Niagara") : TEXT("ValidDiscoveredObjectReferences"))});
			const FString MinimumVersion = bNative || Domain.StartsWith(TEXT("sequencer_animmixer_toolset.")) ? TEXT("5.8")
				: Domain.Contains(TEXT(".conditions.")) ? TEXT("5.5") : !bSupported ? TEXT("5.8") : Domain == TEXT("PCGToolset") ? TEXT("5.4") : TEXT("4.26");
			Row->SetStringField(TEXT("MinimumVersion"), MinimumVersion);
			Row->SetStringField(TEXT("Reason"), bSupported ? TEXT("Available on this host; discovered object references and target-specific prerequisites still apply") : bNative ? TEXT("Native action is disabled, filtered, or has no registered input schema") : TEXT("No discoverable compatibility schema for this action; use the native UE 5.8 backend"));
			// 扁平入口返回客户端参数；透传入口返回 Parameters 内部契约。
			if (!bAnimation && !(bWrite && Domain == TEXT("NiagaraToolsets"))) Row->SetObjectField(TEXT("InputSchema"), NexusFlatActionInput(*Record, Pair.Key, bNative));
			else if (Parameters) Row->SetObjectField(TEXT("InputSchema"), Parameters);
			Rows.Add(MakeShared<FJsonValueObject>(Row));
		}
	}
	return Rows;
}
