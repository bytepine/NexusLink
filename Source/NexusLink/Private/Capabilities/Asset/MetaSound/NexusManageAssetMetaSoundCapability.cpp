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
#if NX_UE_HAS_METASOUND_PATCH
#include "Metasound.h"
#endif

void FManageAssetMetaSoundCapability::BuildDefinition(FNexusCapabilityDefinition& Out) const
{
	Out.Name        = TEXT("manage_asset_meta_sound");
	Out.SearchAssetTypes = {TEXT("MetaSoundSource"), TEXT("MetaSoundPatch")};
	Out.Description = TEXT("?? MetaSound Source/Patch ???????????UE5.1??? operations[].action?");
	Out.InputSchema = FNexusSchema::Object()
		.Prop(TEXT("assetPath"),  FNexusSchema::Str(TEXT("MetaSound Source ? Patch ????")))
		.Prop(TEXT("operations"), FNexusSchema::ArrOfObj(TEXT("????")))
		.Required({ TEXT("assetPath"), TEXT("operations") })
		.Build();
	Out.Tags = { FNexusMcpTags::Editor };
	Out.ExtraSearchKeywords = { TEXT("metasound"), TEXT("audio"), TEXT("sound"), TEXT("input"), TEXT("output"), TEXT("node"), TEXT("edge"), TEXT("wire"), TEXT("connect"), TEXT("patch") };
	Out.RelatedCapabilities = { TEXT("get_asset_meta_sound"), TEXT("create_asset_meta_sound"), TEXT("create_asset_meta_sound_patch") };
	Out.WhenToUse = TEXT("?? MetaSound Source ? Patch ???/??add_edge ? fromNodeID/fromPin/toNodeID/toPin??? ID ? get_asset_meta_sound ???");
}

#if NX_UE_HAS_METASOUND_FRONTEND_DOCUMENT

// ?????? FMetasoundFrontendDocument ?????PropName ????????
static FMetasoundFrontendDocument* GetMutableDocumentByProp(UObject* Asset, const TCHAR* PropName)
{
	if (!Asset) return nullptr;
	FProperty* Prop = Asset->GetClass()->FindPropertyByName(PropName);
	FStructProperty* StructProp = CastField<FStructProperty>(Prop);
	if (!StructProp) return nullptr;
	return StructProp->ContainerPtrToValuePtr<FMetasoundFrontendDocument>(Asset);
}

// UMetaSoundSource ?????? 's'?
static FMetasoundFrontendDocument* GetMutableDocument(UMetaSoundSource* Source)
{
	return GetMutableDocumentByProp(Source, TEXT("RootMetasoundDocument"));
}
#if NX_UE_HAS_METASOUND_PATCH
// UMetaSoundPatch ?????? 'S'?
static FMetasoundFrontendDocument* GetMutableDocumentPatch(UMetaSoundPatch* Patch)
{
	return GetMutableDocumentByProp(Patch, TEXT("RootMetaSoundDocument"));
}
#endif

static void ApplyOperation(const TSharedPtr<FJsonObject>& Op, FMetasoundFrontendDocument* Doc,
	const FString& AssetPath, TArray<TSharedPtr<FJsonValue>>& OutEntries)
{
	FString Action;
	Op->TryGetStringField(TEXT("action"), Action);

	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetStringField(TEXT("path"), AssetPath);
	Result->SetStringField(TEXT("action"), Action);

	FMetasoundFrontendClassInterface& Iface = Doc->RootGraph.Interface;

	if (Action == TEXT("add_input"))
	{
		FString Name, TypeName;
		if (!Op->TryGetStringField(TEXT("name"),     Name)     || Name.IsEmpty()     ||
		    !Op->TryGetStringField(TEXT("typeName"), TypeName) || TypeName.IsEmpty())
		{
			Result->SetStringField(TEXT("error"), TEXT("add_input ?? name ? typeName"));
			OutEntries.Add(MakeShared<FJsonValueObject>(Result));
			return;
		}
		// ????????? input
		const FName InputName(*Name);
		for (const FMetasoundFrontendClassInput& Existing : Iface.Inputs)
		{
			if (Existing.Name == InputName)
			{
				Result->SetBoolField(TEXT("alreadyExists"), true);
				OutEntries.Add(MakeShared<FJsonValueObject>(Result));
				return;
			}
		}
		FMetasoundFrontendClassInput NewInput;
		NewInput.Name     = InputName;
		NewInput.TypeName = FName(*TypeName);
		NewInput.VertexID = FGuid::NewGuid();
		Iface.Inputs.Add(NewInput);
	}
	else if (Action == TEXT("remove_input"))
	{
		FString Name;
		Op->TryGetStringField(TEXT("name"), Name);
		const FName InputName(*Name);
		const int32 Removed = Iface.Inputs.RemoveAll([&](const FMetasoundFrontendClassInput& I) {
			return I.Name == InputName;
		});
		if (Removed == 0) Result->SetStringField(TEXT("error"), FString::Printf(TEXT("input '%s' ???"), *Name));
	}
	else if (Action == TEXT("add_output"))
	{
		FString Name, TypeName;
		if (!Op->TryGetStringField(TEXT("name"),     Name)     || Name.IsEmpty()     ||
		    !Op->TryGetStringField(TEXT("typeName"), TypeName) || TypeName.IsEmpty())
		{
			Result->SetStringField(TEXT("error"), TEXT("add_output ?? name ? typeName"));
			OutEntries.Add(MakeShared<FJsonValueObject>(Result));
			return;
		}
		const FName OutputName(*Name);
		for (const FMetasoundFrontendClassOutput& Existing : Iface.Outputs)
		{
			if (Existing.Name == OutputName)
			{
				Result->SetBoolField(TEXT("alreadyExists"), true);
				OutEntries.Add(MakeShared<FJsonValueObject>(Result));
				return;
			}
		}
		FMetasoundFrontendClassOutput NewOutput;
		NewOutput.Name     = OutputName;
		NewOutput.TypeName = FName(*TypeName);
		NewOutput.VertexID = FGuid::NewGuid();
		Iface.Outputs.Add(NewOutput);
	}
	else if (Action == TEXT("remove_output"))
	{
		FString Name;
		Op->TryGetStringField(TEXT("name"), Name);
		const FName OutputName(*Name);
		const int32 Removed = Iface.Outputs.RemoveAll([&](const FMetasoundFrontendClassOutput& O) {
			return O.Name == OutputName;
		});
		if (Removed == 0) Result->SetStringField(TEXT("error"), FString::Printf(TEXT("output '%s' ???"), *Name));
	}
	else if (Action == TEXT("add_node"))
	{
		// ??????????classID ? GUID ?????? get_asset_meta_sound ? dependencies ???
		FString ClassIDStr, NodeName;
		Op->TryGetStringField(TEXT("classID"),  ClassIDStr);
		Op->TryGetStringField(TEXT("nodeName"), NodeName);
		FGuid ClassGuid;
		if (!FGuid::Parse(ClassIDStr, ClassGuid))
		{
			Result->SetStringField(TEXT("error"), TEXT("add_node: ?? classID??? GUID ????"));
			OutEntries.Add(MakeShared<FJsonValueObject>(Result));
			return;
		}
		FMetasoundFrontendNode NewNode;
		NewNode.ID      = FGuid::NewGuid();
		NewNode.ClassID = ClassGuid;
		if (!NodeName.IsEmpty())
			NewNode.Name = FName(*NodeName);
		Doc->RootGraph.Graph.Nodes.Add(NewNode);
		Result->SetStringField(TEXT("nodeID"), NewNode.ID.ToString());
	}
	else if (Action == TEXT("remove_node"))
	{
		FString NodeIDStr;
		Op->TryGetStringField(TEXT("nodeID"), NodeIDStr);
		FGuid NodeGuid;
		if (!FGuid::Parse(NodeIDStr, NodeGuid))
		{
			Result->SetStringField(TEXT("error"), TEXT("remove_node: ?? nodeID"));
			OutEntries.Add(MakeShared<FJsonValueObject>(Result));
			return;
		}
		const int32 RemovedNodes = Doc->RootGraph.Graph.Nodes.RemoveAll(
			[&](const FMetasoundFrontendNode& N) { return N.GetID() == NodeGuid; });
		// ?????????????
		const int32 RemovedEdges = Doc->RootGraph.Graph.Edges.RemoveAll(
			[&](const FMetasoundFrontendEdge& E) {
				return E.From.NodeID == NodeGuid || E.To.NodeID == NodeGuid; });
		Result->SetNumberField(TEXT("removedEdges"), RemovedEdges);
		if (RemovedNodes == 0)
			Result->SetStringField(TEXT("error"), FString::Printf(TEXT("?? '%s' ???"), *NodeIDStr));
	}
	else if (Action == TEXT("add_edge"))
	{
		// ??????????fromNodeID/fromPin ???toNodeID/toPin ???
		FString FromNodeIDStr, FromPin, ToNodeIDStr, ToPin;
		if (!Op->TryGetStringField(TEXT("fromNodeID"), FromNodeIDStr) ||
			!Op->TryGetStringField(TEXT("fromPin"),    FromPin)       ||
			!Op->TryGetStringField(TEXT("toNodeID"),   ToNodeIDStr)   ||
			!Op->TryGetStringField(TEXT("toPin"),      ToPin))
		{
			Result->SetStringField(TEXT("error"), TEXT("add_edge ?? fromNodeID/fromPin/toNodeID/toPin"));
			OutEntries.Add(MakeShared<FJsonValueObject>(Result));
			return;
		}
		FMetasoundFrontendEdge NewEdge;
		FGuid::Parse(FromNodeIDStr, NewEdge.From.NodeID);
		NewEdge.From.VertexName = FName(*FromPin);
		FGuid::Parse(ToNodeIDStr,   NewEdge.To.NodeID);
		NewEdge.To.VertexName   = FName(*ToPin);
		// ????
		const bool bExists = Doc->RootGraph.Graph.Edges.ContainsByPredicate(
			[&](const FMetasoundFrontendEdge& E) {
				return E.From.NodeID     == NewEdge.From.NodeID &&
				       E.From.VertexName == NewEdge.From.VertexName &&
				       E.To.NodeID       == NewEdge.To.NodeID   &&
				       E.To.VertexName   == NewEdge.To.VertexName; });
		if (bExists) { Result->SetBoolField(TEXT("alreadyExists"), true); }
		else { Doc->RootGraph.Graph.Edges.Add(NewEdge); }
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
		if (Removed == 0)
			Result->SetStringField(TEXT("error"), TEXT("????"));
	}
	else
	{
		Result->SetStringField(TEXT("error"), FString::Printf(
			TEXT("?? action '%s'???: add_input/remove_input/add_output/remove_output/add_node/remove_node/add_edge/remove_edge"),
			*Action));
	}

	OutEntries.Add(MakeShared<FJsonValueObject>(Result));
}

#endif // NX_UE_HAS_METASOUND_FRONTEND_DOCUMENT

FCapabilityResult FManageAssetMetaSoundCapability::Execute(const TSharedPtr<FJsonObject>& Arguments) const
{
	return FNexusCapabilityResultBuilder::Build([&](auto& OutEntries, auto& OutTop, auto& OutError)
	{
		FString AssetPath;
		if (!FNexusCapability::RequireString(Arguments, TEXT("assetPath"), AssetPath, OutEntries, {})) return;

		// ???? MetaSoundSource?????? MetaSoundPatch??5.1?
		UObject* SoundAsset = nullptr;
		UMetaSoundSource* Source = FNexusAssetUtils::LoadAssetWithFallback<UMetaSoundSource>(AssetPath);
		if (Source) { SoundAsset = Source; }
#if NX_UE_HAS_METASOUND_PATCH
		UMetaSoundPatch* Patch = nullptr;
		if (!SoundAsset)
		{
			Patch = FNexusAssetUtils::LoadAssetWithFallback<UMetaSoundPatch>(AssetPath);
			if (Patch) { SoundAsset = Patch; }
		}
#endif
		if (!SoundAsset)
		{
			FNexusCapability::EmitError(OutEntries, {{TEXT("path"), AssetPath}},
				FString::Printf(TEXT("MetaSound Source / Patch ???: %s"), *AssetPath));
			return;
		}

		const TArray<TSharedPtr<FJsonValue>>* OpsArr = nullptr;
		if (!Arguments->TryGetArrayField(TEXT("operations"), OpsArr) || !OpsArr || OpsArr->IsEmpty())
		{
			FNexusCapability::EmitError(OutEntries, {{TEXT("path"), AssetPath}},
				TEXT("operations ????"));
			return;
		}

#if NX_UE_HAS_METASOUND_FRONTEND_DOCUMENT
		// ?????????? Document
		FMetasoundFrontendDocument* Doc = Source
			? GetMutableDocument(Source)
#if NX_UE_HAS_METASOUND_PATCH
			: GetMutableDocumentPatch(Patch);
#else
			: nullptr;
#endif
		if (!Doc)
		{
			FNexusCapability::EmitError(OutEntries, {{TEXT("path"), AssetPath}},
				TEXT("???? FMetasoundFrontendDocument??????"));
			return;
		}

		for (const TSharedPtr<FJsonValue>& Val : *OpsArr)
		{
			const TSharedPtr<FJsonObject>* OpObj = nullptr;
			if (!Val->TryGetObject(OpObj) || !OpObj) continue;
			ApplyOperation(*OpObj, Doc, AssetPath, OutEntries);
		}

		SoundAsset->MarkPackageDirty();
#else
		FNexusCapability::EmitError(OutEntries, {{TEXT("path"), AssetPath}},
			TEXT("manage_asset_meta_sound ????? UE 5.3+?NX_UE_HAS_METASOUND_FRONTEND_DOCUMENT?"));
#endif
	});
}

REGISTER_MCP_CAPABILITY(FManageAssetMetaSoundCapability)

#endif // WITH_METASOUND
