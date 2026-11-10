// Copyright byteyang. All Rights Reserved.
#include "Utils/NexusEditorActionSchema.h"
#include "Utils/NexusNiagaraEditorTools.h"
#include "Utils/NexusVersionCompat.h"
#include "Utils/NexusAnimationToolActions.h"
#include "Utils/NexusAnimationAdvancedTools.h"
#include "NexusCapabilityRegistry.h"
#include "NexusMcpSchemaBuilder.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"


static TSharedPtr<FJsonObject> NexusNiagaraLegacyParameters(const FString& Tool)
{
#if WITH_NIAGARA
    auto Ref = FNexusSchema::Object().Required(TEXT("refPath"), FNexusSchema::Str(TEXT("Discovered object path"))).Build().ToSharedRef();
    auto Schema = FNexusSchema::Object();
		if (Tool == TEXT("AddUserVariables") || Tool == TEXT("RemoveUserVariables"))
		{
			auto Variable = FNexusSchema::Object().Required(TEXT("name"), FNexusSchema::Str(TEXT("Unique User.* name")))
				.Required(TEXT("type"), FNexusSchema::Object().Required(TEXT("classStructOrEnum"), Ref).Build().ToSharedRef())
				.Prop(TEXT("defaultValue"), FNexusSchema::Object().Prop(TEXT("struct"), Ref).Prop(TEXT("value"), FNexusSchema::AnyObject(TEXT("Fields of the selected supported value struct"))).Build().ToSharedRef()).Build().ToSharedRef();
			return Schema.Required(TEXT("system"), Ref).Required(Tool == TEXT("AddUserVariables") ? TEXT("variablesToAdd") : TEXT("variablesToRemove"), FNexusSchema::ArrayOf(TEXT("1-64 variables; value structs only"), Variable)).Build();
		}
		if (Tool == TEXT("GetUserVariables") || Tool == TEXT("GetSystemData")) return Schema.Required(TEXT("system"), Ref).Build();
		if (Tool == TEXT("SetSystemData")) return Schema.Required(TEXT("system"), Ref).Required(TEXT("systemData"), FNexusSchema::Object().Required(TEXT("propertyValues"), FNexusSchema::Str(TEXT("JSON object of editable system properties"))).Build().ToSharedRef()).Build();
		auto Emitter = FNexusSchema::Object().Required(TEXT("system"), Ref).Required(TEXT("emitterName"), FNexusSchema::Str(TEXT("Discovered emitter name"))).Prop(TEXT("rendererIndex"), FNexusSchema::Int(TEXT("Discovered renderer index"), 0, 0)).Build().ToSharedRef();
		if (Tool == TEXT("AddRenderer")) return Schema.Required(TEXT("newRendererLocation"), Emitter).Required(TEXT("rendererClass"), Ref).Build();
		if (Tool == TEXT("GetRendererData")) return Schema.Required(TEXT("renderer"), Emitter).Build();
		if (Tool == TEXT("RemoveRenderer")) return Schema.Required(TEXT("rendererToRemove"), Emitter).Build();
		if (Tool == TEXT("SetRendererData")) return Schema.Required(TEXT("renderer"), Emitter).Required(TEXT("rendererData"), FNexusSchema::Object().Required(TEXT("propertyValues"), FNexusSchema::Str(TEXT("JSON object of editable renderer properties"))).Build().ToSharedRef()).Build();
#endif
	return nullptr;
}

static TSharedPtr<FJsonObject> NexusFlatActionInput(const FCapRecord& Record, const FString& Action)
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
		if (Action == TEXT("SetGraphParameters")) Require(TEXT("ParameterDefinitions"));
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
			FString Domain = Name.Contains(TEXT("PCG")) ? TEXT("NexusPCG") : TEXT("NexusNiagara"), Tool = Pair.Value;
			if (bAnimation) { int32 Separator; Tool.FindLastChar(TEXT('.'), Separator); Domain = Tool.Left(Separator); Tool = Tool.Mid(Separator + 1); }
			else if (!bWrite && Domain == TEXT("NexusNiagara"))
			{
				Tool = Pair.Key == TEXT("CompileState") ? TEXT("GetSystemCompileState") : Pair.Key == TEXT("Dependencies") ? TEXT("GetSystemDependencies") : Pair.Key == TEXT("ScriptTopology") ? TEXT("GetScriptStackTopology") : TEXT("Get") + Pair.Key;
				if (Pair.Key == TEXT("InputSchema") || Pair.Key == TEXT("InputData")) Tool = TEXT("GetStack") + Pair.Key;
			}
			else if (Domain == TEXT("NexusPCG"))
			{
				static const TMap<FString, FString> Mapping = {{TEXT("Instances"),TEXT("ListGraphInstances")},{TEXT("GraphStructure"),TEXT("GetGraphStructure")},{TEXT("GraphSchema"),TEXT("GetGraphSchema")},{TEXT("NativeNodes"),TEXT("ListNativeNodes")},{TEXT("NativeNodeSchema"),TEXT("GetNativeNodeSchema")},{TEXT("NodeData"),TEXT("GetNodeDataView")},{TEXT("NodeInfo"),TEXT("GetNodeInfo")},{TEXT("InstanceParameters"),TEXT("GetGraphInstanceParams")},{TEXT("AvailableSubgraphs"),TEXT("ListAvailableSubgraphs")},{TEXT("Spawn"),TEXT("SpawnGraphInstance")},{TEXT("Execute"),TEXT("ExecuteGraphInstance")},{TEXT("SetGraphParameters"),TEXT("SetGraphParams")},{TEXT("RemoveGraphParameters"),TEXT("RemoveGraphParams")},{TEXT("SetInstanceParameters"),TEXT("SetGraphInstanceParams")},{TEXT("ResetInstanceParameters"),TEXT("ResetGraphInstanceParams")},{TEXT("AddSubgraph"),TEXT("AddSubgraphNode")}};
				if (const auto* Mapped = Mapping.Find(Pair.Key)) Tool = *Mapped;
			}
			auto Parameters = bAnimation ? FNexusAnimationAdvancedTools::InputSchema(Domain, Tool) : Domain == TEXT("NexusNiagara") ? FNexusNiagaraEditorTools::InputSchema(Tool) : nullptr;
            if (!Parameters && Domain == TEXT("NexusNiagara")) Parameters = NexusNiagaraLegacyParameters(Tool);
			bool bSupported = Parameters.IsValid() && (!bAnimation || FNexusAnimationAdvancedTools::Supports(Domain, Tool));
            if (Domain == TEXT("NexusPCG")) bSupported = true;
			auto Row = MakeShared<FJsonObject>();
			Row->SetStringField(TEXT("Name"), Pair.Key); Row->SetStringField(TEXT("Capability"), Name);
			Row->SetStringField(TEXT("ToolName"), Tool); Row->SetStringField(TEXT("ToolsetName"), Domain);
			Row->SetStringField(TEXT("Backend"), TEXT("Independent"));
			Row->SetBoolField(TEXT("bSupported"), bSupported);
			TArray<TSharedPtr<FJsonValue>> Prerequisites = {MakeShared<FJsonValueString>(TEXT("EditorOnly")), MakeShared<FJsonValueString>(TEXT("ValidDiscoveredObjectReferences"))};
            if (Domain == TEXT("NexusPCG")) Prerequisites.Add(MakeShared<FJsonValueString>(TEXT("PluginEnabled:PCG")));
            else if (Domain == TEXT("NexusNiagara")) Prerequisites.Add(MakeShared<FJsonValueString>(TEXT("PluginEnabled:Niagara")));
            else if (Domain == TEXT("NexusAnimation.Mixer")) Prerequisites.Add(MakeShared<FJsonValueString>(TEXT("PluginEnabled:MovieSceneAnimMixer")));
            else if (Domain.StartsWith(TEXT("NexusAnimation.ControlRig"))) Prerequisites.Add(MakeShared<FJsonValueString>(TEXT("PluginEnabled:ControlRig")));
            Row->SetArrayField(TEXT("Prerequisites"), Prerequisites);
            const FString MinimumVersion = bAnimation ? FNexusAnimationAdvancedTools::MinimumVersion(Domain,Tool)
                : Domain == TEXT("NexusPCG") ? TEXT("5.4") : NexusNiagaraLegacyParameters(Tool).IsValid() ? TEXT("4.26") : TEXT("5.8");
			Row->SetStringField(TEXT("MinimumVersion"), MinimumVersion);
			Row->SetStringField(TEXT("Reason"), bSupported ? TEXT("Available on this host; discovered object references and target-specific prerequisites still apply") : TEXT("The required engine domain API is unavailable on this host"));
            if (!bSupported && Domain == TEXT("NexusAnimation.Mixer")) Row->SetStringField(TEXT("Reason"), TEXT("Enable MovieSceneAnimMixer and use an engine exposing the requested Mixer scripting API (UE 5.8+). No Toolset plugin is required."));
			// 扁平入口返回客户端参数；透传入口返回 Parameters 内部契约。
			if (!bAnimation && !(bWrite && Domain == TEXT("NexusNiagara"))) Row->SetObjectField(TEXT("InputSchema"), NexusFlatActionInput(*Record, Pair.Key));
			else if (Parameters) Row->SetObjectField(TEXT("InputSchema"), Parameters);
			Rows.Add(MakeShared<FJsonValueObject>(Row));
		}
	}
	return Rows;
}
