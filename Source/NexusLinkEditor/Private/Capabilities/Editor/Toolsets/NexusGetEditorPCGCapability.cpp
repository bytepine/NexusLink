// Copyright byteyang. All Rights Reserved.
#include "Capabilities/Editor/Toolsets/NexusGetEditorPCGCapability.h"
#if WITH_NEXUS_TOOLSETS
#include "Utils/NexusEditorToolSchema.h"
#include "Utils/NexusArgs.h"
#include "NexusCapabilityRegistry.h"

void FNexusGetEditorPCGCapability::BuildDefinition(FNexusCapabilityDefinition& Out) const
{
	Out.Name = TEXT("GetEditorPCG");
	Out.Description = TEXT("Inspect PCG graph schemas, native nodes, graph instances and bounded node output data. NodeData may require inspection followed by re-execution.");
	Out.InputSchema = FNexusSchema::Object()
		.Prop(TEXT("Action"), FNexusSchema::Enum(TEXT("PCG inspection"), {TEXT("Instances"), TEXT("GraphStructure"), TEXT("GraphSchema"), TEXT("NativeNodes"), TEXT("NativeNodeSchema"), TEXT("NodeData")}, TEXT("Instances")))
		.Prop(TEXT("AssetPath"), FNexusSchema::Str(TEXT("Graph asset object path for GraphStructure/GraphSchema")))
		.Prop(TEXT("ActorPath"), FNexusSchema::Str(TEXT("PCGVolume object refPath from Instances")))
		.Prop(TEXT("NodePath"), FNexusSchema::Str(TEXT("PCGNode object refPath from GraphStructure")))
		.Prop(TEXT("NodeName"), FNexusSchema::Str(TEXT("Native node name from NativeNodes")))
		.Prop(TEXT("bCommonOnly"), FNexusSchema::Bool(TEXT("List common native nodes; set false only when explicitly requested"), true, true))
		.Prop(TEXT("PinLabel"), FNexusSchema::Str(TEXT("Node output pin"), TEXT("Out")))
		.Prop(TEXT("AttributeName"), FNexusSchema::Str(TEXT("Optional node attribute/property filter")))
		.Prop(TEXT("Offset"), FNexusSchema::Int(TEXT("Node data element offset"), 0, 0, 1000000000))
		.Prop(TEXT("Limit"), FNexusSchema::Int(TEXT("Node data element count"), 100, 1, 1000)).Build();
	FNexusEditorToolSchema::ConfigureNative(Out, false, TEXT("PCGToolset"));
	Out.SearchAssetTypes = {TEXT("PCGGraph")};
	Out.ExtraSearchKeywords = {TEXT("pcg"), TEXT("procedural"), TEXT("inspection"), TEXT("points"), TEXT("attributes")};
	Out.RelatedCapabilities = {TEXT("ControlEditorPCG"), TEXT("GetAssetPCGGraph"), TEXT("GetEditorOperation")};
}
FCapabilityResult FNexusGetEditorPCGCapability::Execute(const TSharedPtr<FJsonObject>& Arguments) const
{
	const FNexusArgs A(Arguments); const FString Action = A.Str(TEXT("Action"), TEXT("Instances"));
	auto Input = MakeShared<FJsonObject>(); FString Tool;
	if (Action == TEXT("Instances")) Tool = TEXT("ListGraphInstances");
	else if (Action == TEXT("NativeNodes")) { Tool = TEXT("ListNativeNodes"); Input->SetBoolField(TEXT("bCommonOnly"), A.Bool(TEXT("bCommonOnly"), true)); }
	else if (Action == TEXT("NativeNodeSchema"))
	{
		if (A.Str(TEXT("NodeName")).IsEmpty()) return FCapabilityResult::MakeArgInvalid(TEXT("NativeNodeSchema requires NodeName"));
		Tool = TEXT("GetNativeNodeSchema"); Input->SetStringField(TEXT("NodeName"), A.Str(TEXT("NodeName")));
	}
	else if (Action == TEXT("GraphStructure") || Action == TEXT("GraphSchema"))
	{
		if (A.Str(TEXT("AssetPath")).IsEmpty()) return FCapabilityResult::MakeArgInvalid(TEXT("Graph inspection requires AssetPath"));
		Tool = TEXT("Get") + Action; Input->SetObjectField(TEXT("Graph"), FNexusNativeEditorTools::Reference(A.Str(TEXT("AssetPath"))));
	}
	else
	{
		if (A.Str(TEXT("ActorPath")).IsEmpty() || A.Str(TEXT("NodePath")).IsEmpty()) return FCapabilityResult::MakeArgInvalid(TEXT("NodeData requires ActorPath and NodePath from discovery"));
		Tool = TEXT("GetNodeDataView");
		Input->SetObjectField(TEXT("PCGVolume"), FNexusNativeEditorTools::Reference(A.Str(TEXT("ActorPath"))));
		Input->SetObjectField(TEXT("Node"), FNexusNativeEditorTools::Reference(A.Str(TEXT("NodePath"))));
		Input->SetStringField(TEXT("PinLabel"), A.Str(TEXT("PinLabel"), TEXT("Out"))); Input->SetStringField(TEXT("AttributeName"), A.Str(TEXT("AttributeName")));
		Input->SetNumberField(TEXT("StartIndex"), A.Num(TEXT("Offset"))); Input->SetNumberField(TEXT("EndIndex"), A.Num(TEXT("Offset")) + A.Num(TEXT("Limit"), 100));
	}
	auto Operation = FNexusEditorOperations::Create(TEXT("OfficialToolset"));
	FNexusNativeEditorTools::Call(TEXT("PCGToolset"), Tool, Input, Operation);
	return FNexusEditorToolSchema::Result(Operation);
}
REGISTER_MCP_CAPABILITY(FNexusGetEditorPCGCapability)
#endif
