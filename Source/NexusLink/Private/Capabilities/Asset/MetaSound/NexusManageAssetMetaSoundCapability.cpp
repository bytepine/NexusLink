// Copyright byteyang. All Rights Reserved.

#include "Capabilities/Asset/MetaSound/NexusManageAssetMetaSoundCapability.h"

#if WITH_METASOUND

#include "Utils/NexusCapabilityResultBuilder.h"
#include "NexusCapabilityRegistry.h"
#include "NexusMcpSchemaBuilder.h"
#include "Utils/NexusAssetUtils.h"
#include "Utils/NexusVersionCompat.h"
#include "NexusMcpTool.h"
#include "MetasoundSource.h"
#include "MetasoundFrontendDocument.h"
#include "UObject/UnrealType.h"

void FManageAssetMetaSoundCapability::BuildDefinition(FNexusCapabilityDefinition& Out) const
{
	Out.Name        = TEXT("manage_asset_meta_sound");
	Out.Description = TEXT("修改 MetaSound Source：add_input/remove_input/add_output/remove_output/add_node/remove_node/add_edge/remove_edge。");
	Out.InputSchema = FNexusSchema::Object()
		.Prop(TEXT("assetPath"),  FNexusSchema::Str(TEXT("MetaSound Source 资产路径")))
		.Prop(TEXT("operations"), FNexusSchema::ArrOfObj(TEXT("操作列表")))
		.Required({ TEXT("assetPath"), TEXT("operations") })
		.Build();
	Out.Tags = { FNexusMcpTags::Editor };
	Out.ExtraSearchKeywords = { TEXT("metasound"), TEXT("audio"), TEXT("sound"), TEXT("input"), TEXT("output"), TEXT("node"), TEXT("edge"), TEXT("wire"), TEXT("connect") };
	Out.RelatedCapabilities = { TEXT("get_asset_meta_sound"), TEXT("create_asset_meta_sound") };
	Out.WhenToUse = TEXT("修改 MetaSound 接口/图：添加/删除 input、output、节点、连线；add_edge 用 fromNodeID/fromPin/toNodeID/toPin（节点 ID 从 get_asset_meta_sound 获取）");
}

#if NX_UE_HAS_METASOUND_FRONTEND_DOCUMENT

// 获取 UMetaSoundSource::RootMetasoundDocument 的可变指针（通过反射绕过 protected 访问）
static FMetasoundFrontendDocument* GetMutableDocument(UMetaSoundSource* Source)
{
	if (!Source) return nullptr;
	FProperty* Prop = Source->GetClass()->FindPropertyByName(TEXT("RootMetasoundDocument"));
	FStructProperty* StructProp = CastField<FStructProperty>(Prop);
	if (!StructProp) return nullptr;
	return StructProp->ContainerPtrToValuePtr<FMetasoundFrontendDocument>(Source);
}

static void ApplyOperation(const TSharedPtr<FJsonObject>& Op, FMetasoundFrontendDocument* Doc,
	TArray<TSharedPtr<FJsonValue>>& Results)
{
	FString Action;
	Op->TryGetStringField(TEXT("action"), Action);

	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetStringField(TEXT("action"), Action);

	FMetasoundFrontendClassInterface& Iface = Doc->RootGraph.Interface;

	if (Action == TEXT("add_input"))
	{
		FString Name, TypeName;
		if (!Op->TryGetStringField(TEXT("name"),     Name)     || Name.IsEmpty()     ||
		    !Op->TryGetStringField(TEXT("typeName"), TypeName) || TypeName.IsEmpty())
		{
			Result->SetStringField(TEXT("error"), TEXT("add_input 需要 name 和 typeName"));
			Results.Add(MakeShared<FJsonValueObject>(Result));
			return;
		}
		// 检查是否已存在同名 input
		const FName InputName(*Name);
		for (const FMetasoundFrontendClassInput& Existing : Iface.Inputs)
		{
			if (Existing.Name == InputName)
			{
				Result->SetBoolField(TEXT("alreadyExists"), true);
				Results.Add(MakeShared<FJsonValueObject>(Result));
				return;
			}
		}
		FMetasoundFrontendClassInput NewInput;
		NewInput.Name     = InputName;
		NewInput.TypeName = FName(*TypeName);
		NewInput.VertexID = FGuid::NewGuid();
		Iface.Inputs.Add(NewInput);
		Result->SetBoolField(TEXT("success"), true);
	}
	else if (Action == TEXT("remove_input"))
	{
		FString Name;
		Op->TryGetStringField(TEXT("name"), Name);
		const FName InputName(*Name);
		const int32 Removed = Iface.Inputs.RemoveAll([&](const FMetasoundFrontendClassInput& I) {
			return I.Name == InputName;
		});
		Result->SetBoolField(TEXT("success"), Removed > 0);
		if (Removed == 0) Result->SetStringField(TEXT("error"), FString::Printf(TEXT("input '%s' 不存在"), *Name));
	}
	else if (Action == TEXT("add_output"))
	{
		FString Name, TypeName;
		if (!Op->TryGetStringField(TEXT("name"),     Name)     || Name.IsEmpty()     ||
		    !Op->TryGetStringField(TEXT("typeName"), TypeName) || TypeName.IsEmpty())
		{
			Result->SetStringField(TEXT("error"), TEXT("add_output 需要 name 和 typeName"));
			Results.Add(MakeShared<FJsonValueObject>(Result));
			return;
		}
		const FName OutputName(*Name);
		for (const FMetasoundFrontendClassOutput& Existing : Iface.Outputs)
		{
			if (Existing.Name == OutputName)
			{
				Result->SetBoolField(TEXT("alreadyExists"), true);
				Results.Add(MakeShared<FJsonValueObject>(Result));
				return;
			}
		}
		FMetasoundFrontendClassOutput NewOutput;
		NewOutput.Name     = OutputName;
		NewOutput.TypeName = FName(*TypeName);
		NewOutput.VertexID = FGuid::NewGuid();
		Iface.Outputs.Add(NewOutput);
		Result->SetBoolField(TEXT("success"), true);
	}
	else if (Action == TEXT("remove_output"))
	{
		FString Name;
		Op->TryGetStringField(TEXT("name"), Name);
		const FName OutputName(*Name);
		const int32 Removed = Iface.Outputs.RemoveAll([&](const FMetasoundFrontendClassOutput& O) {
			return O.Name == OutputName;
		});
		Result->SetBoolField(TEXT("success"), Removed > 0);
		if (Removed == 0) Result->SetStringField(TEXT("error"), FString::Printf(TEXT("output '%s' 不存在"), *Name));
	}
	else if (Action == TEXT("add_node"))
	{
		// 在图中插入节点实例；classID 为 GUID 字符串（可从 get_asset_meta_sound 的 dependencies 获取）
		FString ClassIDStr, NodeName;
		Op->TryGetStringField(TEXT("classID"),  ClassIDStr);
		Op->TryGetStringField(TEXT("nodeName"), NodeName);
		FGuid ClassGuid;
		if (!FGuid::Parse(ClassIDStr, ClassGuid))
		{
			Result->SetStringField(TEXT("error"), TEXT("add_node: 无效 classID（需为 GUID 字符串）"));
			Results.Add(MakeShared<FJsonValueObject>(Result));
			return;
		}
		FMetasoundFrontendNode NewNode;
		NewNode.ID      = FGuid::NewGuid();
		NewNode.ClassID = ClassGuid;
		if (!NodeName.IsEmpty())
			NewNode.Name = FName(*NodeName);
		Doc->RootGraph.Graph.Nodes.Add(NewNode);
		Result->SetBoolField(TEXT("success"),  true);
		Result->SetStringField(TEXT("nodeID"), NewNode.ID.ToString());
	}
	else if (Action == TEXT("remove_node"))
	{
		FString NodeIDStr;
		Op->TryGetStringField(TEXT("nodeID"), NodeIDStr);
		FGuid NodeGuid;
		if (!FGuid::Parse(NodeIDStr, NodeGuid))
		{
			Result->SetStringField(TEXT("error"), TEXT("remove_node: 无效 nodeID"));
			Results.Add(MakeShared<FJsonValueObject>(Result));
			return;
		}
		const int32 RemovedNodes = Doc->RootGraph.Graph.Nodes.RemoveAll(
			[&](const FMetasoundFrontendNode& N) { return N.GetID() == NodeGuid; });
		// 同时清除涉及该节点的所有边
		const int32 RemovedEdges = Doc->RootGraph.Graph.Edges.RemoveAll(
			[&](const FMetasoundFrontendEdge& E) {
				return E.From.NodeID == NodeGuid || E.To.NodeID == NodeGuid; });
		Result->SetBoolField(TEXT("success"),      RemovedNodes > 0);
		Result->SetNumberField(TEXT("removedEdges"), RemovedEdges);
		if (RemovedNodes == 0)
			Result->SetStringField(TEXT("error"), FString::Printf(TEXT("节点 '%s' 不存在"), *NodeIDStr));
	}
	else if (Action == TEXT("add_edge"))
	{
		// 连接两个节点的引脚；fromNodeID/fromPin 为源，toNodeID/toPin 为目标
		FString FromNodeIDStr, FromPin, ToNodeIDStr, ToPin;
		if (!Op->TryGetStringField(TEXT("fromNodeID"), FromNodeIDStr) ||
			!Op->TryGetStringField(TEXT("fromPin"),    FromPin)       ||
			!Op->TryGetStringField(TEXT("toNodeID"),   ToNodeIDStr)   ||
			!Op->TryGetStringField(TEXT("toPin"),      ToPin))
		{
			Result->SetStringField(TEXT("error"), TEXT("add_edge 需要 fromNodeID/fromPin/toNodeID/toPin"));
			Results.Add(MakeShared<FJsonValueObject>(Result));
			return;
		}
		FMetasoundFrontendEdge NewEdge;
		FGuid::Parse(FromNodeIDStr, NewEdge.From.NodeID);
		NewEdge.From.VertexName = FName(*FromPin);
		FGuid::Parse(ToNodeIDStr,   NewEdge.To.NodeID);
		NewEdge.To.VertexName   = FName(*ToPin);
		// 检查重复
		const bool bExists = Doc->RootGraph.Graph.Edges.ContainsByPredicate(
			[&](const FMetasoundFrontendEdge& E) {
				return E.From.NodeID     == NewEdge.From.NodeID &&
				       E.From.VertexName == NewEdge.From.VertexName &&
				       E.To.NodeID       == NewEdge.To.NodeID   &&
				       E.To.VertexName   == NewEdge.To.VertexName; });
		if (bExists) { Result->SetBoolField(TEXT("alreadyExists"), true); }
		else { Doc->RootGraph.Graph.Edges.Add(NewEdge); Result->SetBoolField(TEXT("success"), true); }
	}
	else if (Action == TEXT("remove_edge"))
	{
		FString FromNodeIDStr, FromPin, ToNodeIDStr, ToPin;
		Op->TryGetStringField(TEXT("fromNodeID"), FromNodeIDStr);
		Op->TryGetStringField(TEXT("fromPin"),    FromPin);
		Op->TryGetStringField(TEXT("toNodeID"),   ToNodeIDStr);
		Op->TryGetStringField(TEXT("toPin"),      ToPin);
		FGuid FromGuid, ToGuid;
		FGuid::Parse(FromNodeIDStr, FromGuid);
		FGuid::Parse(ToNodeIDStr,   ToGuid);
		const FName FromPinName(*FromPin), ToPinName(*ToPin);
		const int32 Removed = Doc->RootGraph.Graph.Edges.RemoveAll(
			[&](const FMetasoundFrontendEdge& E) {
				return E.From.NodeID == FromGuid && E.From.VertexName == FromPinName &&
				       E.To.NodeID   == ToGuid   && E.To.VertexName   == ToPinName; });
		Result->SetBoolField(TEXT("success"), Removed > 0);
		if (Removed == 0)
			Result->SetStringField(TEXT("error"), TEXT("边不存在"));
	}
	else
	{
		Result->SetStringField(TEXT("error"), FString::Printf(
			TEXT("未知 action '%s'，支持: add_input/remove_input/add_output/remove_output/add_node/remove_node/add_edge/remove_edge"),
			*Action));
	}

	Results.Add(MakeShared<FJsonValueObject>(Result));
}

#endif // NX_UE_HAS_METASOUND_FRONTEND_DOCUMENT

FCapabilityResult FManageAssetMetaSoundCapability::Execute(const TSharedPtr<FJsonObject>& Arguments) const
{
	return FNexusCapabilityResultBuilder::Build([&](auto& OutEntries, auto& OutTop, auto& OutError)
	{
		FString AssetPath;
		if (!FNexusCapability::RequireString(Arguments, TEXT("assetPath"), AssetPath, OutEntries, {})) return;

		UMetaSoundSource* Source = FNexusAssetUtils::LoadAssetWithFallback<UMetaSoundSource>(AssetPath);
		if (!Source)
		{
			FNexusCapability::EmitError(OutEntries, {{TEXT("assetPath"), AssetPath}},
				FString::Printf(TEXT("MetaSound Source 未找到: %s"), *AssetPath));
			return;
		}

		const TArray<TSharedPtr<FJsonValue>>* OpsArr = nullptr;
		if (!Arguments->TryGetArrayField(TEXT("operations"), OpsArr) || !OpsArr || OpsArr->IsEmpty())
		{
			FNexusCapability::EmitError(OutEntries, {{TEXT("assetPath"), AssetPath}},
				TEXT("operations 数组为空"));
			return;
		}

#if NX_UE_HAS_METASOUND_FRONTEND_DOCUMENT
		FMetasoundFrontendDocument* Doc = GetMutableDocument(Source);
		if (!Doc)
		{
			FNexusCapability::EmitError(OutEntries, {{TEXT("assetPath"), AssetPath}},
				TEXT("无法获取 FMetasoundFrontendDocument（反射失败）"));
			return;
		}

		TArray<TSharedPtr<FJsonValue>> Results;
		for (const TSharedPtr<FJsonValue>& Val : *OpsArr)
		{
			const TSharedPtr<FJsonObject>* OpObj = nullptr;
			if (!Val->TryGetObject(OpObj) || !OpObj) continue;
			ApplyOperation(*OpObj, Doc, Results);
		}

		Source->MarkPackageDirty();

		TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
		Entry->SetStringField(TEXT("assetPath"), AssetPath);
		Entry->SetArrayField(TEXT("results"), Results);
		OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
#else
		FNexusCapability::EmitError(OutEntries, {{TEXT("assetPath"), AssetPath}},
			TEXT("manage_asset_meta_sound 写操作需要 UE 5.3+（NX_UE_HAS_METASOUND_FRONTEND_DOCUMENT）"));
#endif
	});
}

REGISTER_MCP_CAPABILITY(FManageAssetMetaSoundCapability)

#endif // WITH_METASOUND
