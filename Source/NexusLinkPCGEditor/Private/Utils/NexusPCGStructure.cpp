// Copyright byteyang. All Rights Reserved.
#include "Utils/NexusAssetStructureUtils.h"

#if WITH_PCG
#include "Utils/NexusArgs.h"
#include "Utils/NexusAssetUtils.h"
#include "PCGGraph.h"
#include "PCGNode.h"
#include "PCGSettings.h"
#include "PCGPin.h"
#include "PCGEdge.h"

static void NexusPCGSnapshot(UObject* Asset, TFunctionRef<FString(const FString&)> Id, TArray<FString>& Rows)
{
	if (UPCGGraph* Graph = Cast<UPCGGraph>(Asset))
	{
		TArray<UPCGNode*> Nodes = Graph->GetNodes(); Nodes.Add(Graph->GetInputNode()); Nodes.Add(Graph->GetOutputNode());
		for (const UPCGNode* Node : Nodes) if (Node)
		{
			Rows.Add(TEXT("Node|") + Id(Node->GetName()) + TEXT("|") + (Node->GetSettings() ? Node->GetSettings()->GetClass()->GetPathName() : TEXT("")));
			for (const UPCGPin* Pin : Node->GetOutputPins()) if (Pin) for (const UPCGEdge* Edge : Pin->Edges) if (Edge)
			{
				const UPCGPin* Other = Edge->GetOtherPin(Pin);
				if (Other && Other->Node) Rows.Add(TEXT("Edge|") + Id(Node->GetName()) + TEXT("|") + Pin->Properties.Label.ToString() + TEXT("|") + Id(Other->Node->GetName()) + TEXT("|") + Other->Properties.Label.ToString());
			}
		}
	}
}

static TOptional<bool> NexusPCGApply(UObject* Asset, const FString& Action, const FNexusArgs& A, const TSharedPtr<FJsonObject>& Entry, FString& Error)
{
	if (UPCGGraph* Graph = Cast<UPCGGraph>(Asset))
	{
		const auto Find = [&](const FString& Name) -> UPCGNode* { if (Graph->GetInputNode() && Name == Graph->GetInputNode()->GetName()) return Graph->GetInputNode(); if (Graph->GetOutputNode() && Name == Graph->GetOutputNode()->GetName()) return Graph->GetOutputNode(); for (UPCGNode* Node : Graph->GetNodes()) if (Node && Name == Node->GetName()) return Node; return nullptr; };
		if (Action == TEXT("AddNode"))
		{
			UClass* Class = FNexusAssetUtils::FindClassWithUPrefix(A.Str(TEXT("SettingsClass")));
			if (!Class || !Class->IsChildOf(UPCGSettings::StaticClass()) || Class->HasAnyClassFlags(CLASS_Abstract)) { Error = TEXT("Concrete PCG SettingsClass is required"); return false; }
			UPCGNode* Node = Graph->AddNode(NewObject<UPCGSettings>(Graph, Class));
			if (!Node) { Error = TEXT("Could not add PCG node"); return false; }
			Entry->SetStringField(TEXT("NodeId"), Node->GetName()); return true;
		}
		if (Action == TEXT("RemoveNode")) { UPCGNode* Node = Find(A.Str(TEXT("NodeId"))); if (!Node || Node == Graph->GetInputNode() || Node == Graph->GetOutputNode()) { Error = TEXT("Removable NodeId not found"); return false; } Graph->RemoveNode(Node); return true; }
		UPCGNode* From = Find(A.Str(TEXT("FromNodeId"))); UPCGNode* To = Find(A.Str(TEXT("ToNodeId")));
		if (!From || !To) { Error = TEXT("Edge endpoint not found"); return false; }
		const auto Outputs = From->OutputPinProperties(), Inputs = To->InputPinProperties();
		const FName FromLabel = A.Str(TEXT("FromPin")).IsEmpty() && Outputs.Num() ? Outputs[0].Label : FName(*A.Str(TEXT("FromPin")));
		const FName ToLabel = A.Str(TEXT("ToPin")).IsEmpty() && Inputs.Num() ? Inputs[0].Label : FName(*A.Str(TEXT("ToPin")));
		if (!From->GetOutputPin(FromLabel) || !To->GetInputPin(ToLabel)) { Error = TEXT("Edge pin not found"); return false; }
		if (Action == TEXT("AddEdge")) { if (!Graph->AddEdge(From, FromLabel, To, ToLabel)) { Error = TEXT("PCG rejected edge"); return false; } }
		else Graph->RemoveEdge(From, FromLabel, To, ToLabel);
		return true;
	}
	return {};
}

void NexusInstallPCGStructure()
{
	FNexusAssetStructureUtils::SnapshotPCG = &NexusPCGSnapshot;
	FNexusAssetStructureUtils::ApplyPCG = &NexusPCGApply;
}

void NexusRemovePCGStructure()
{
	FNexusAssetStructureUtils::SnapshotPCG = nullptr;
	FNexusAssetStructureUtils::ApplyPCG = nullptr;
}
#endif
