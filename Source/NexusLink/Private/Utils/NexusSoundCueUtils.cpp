// Copyright byteyang. All Rights Reserved.

#include "Utils/NexusSoundCueUtils.h"
#include "Utils/NexusAssetUtils.h"
#include "Utils/NexusVersionCompat.h"
#include "Sound/SoundCue.h"
#include "Sound/SoundNode.h"
#include "Sound/SoundNodeWavePlayer.h"

void FNexusSoundCueUtils::CollectNodesInOrder(USoundCue* Cue, TArray<USoundNode*>& OutNodes)
{
	OutNodes.Reset();
	if (!Cue || !Cue->FirstNode)
	{
		return;
	}
	Cue->RecursiveFindAllNodes(Cue->FirstNode, OutNodes);
}

USoundNode* FNexusSoundCueUtils::FindNodeByIndex(USoundCue* Cue, int32 NodeIndex)
{
	TArray<USoundNode*> Nodes;
	CollectNodesInOrder(Cue, Nodes);
	return Nodes.IsValidIndex(NodeIndex) ? Nodes[NodeIndex] : nullptr;
}

UClass* FNexusSoundCueUtils::ResolveSoundNodeClass(const FString& ClassName, FString& OutError)
{
	UClass* Class = FNexusAssetUtils::FindClassWithUPrefix(ClassName);
	if (!Class || !Class->IsChildOf(USoundNode::StaticClass()))
	{
		OutError = FString::Printf(TEXT("nodeClass '%s' 未找到或不是 SoundNode 子类"), *ClassName);
		return nullptr;
	}
	return Class;
}

static bool FindParentAndSlot(USoundCue* Cue, USoundNode* Target, USoundNode*& OutParent, int32& OutSlot)
{
	OutParent = nullptr;
	OutSlot = INDEX_NONE;
	if (!Cue || !Target)
	{
		return false;
	}
	if (Cue->FirstNode == Target)
	{
		return true;
	}
	TArray<USoundNode*> All;
	FNexusSoundCueUtils::CollectNodesInOrder(Cue, All);
	for (USoundNode* Node : All)
	{
		if (!Node) continue;
		for (int32 i = 0; i < Node->ChildNodes.Num(); ++i)
		{
			if (Node->ChildNodes[i] == Target)
			{
				OutParent = Node;
				OutSlot = i;
				return true;
			}
		}
	}
	return false;
}

static void DisconnectNode(USoundCue* Cue, USoundNode* Node)
{
	if (!Cue || !Node) return;
	USoundNode* Parent = nullptr;
	int32 Slot = INDEX_NONE;
	if (Cue->FirstNode == Node)
	{
		Cue->FirstNode = nullptr;
	}
	else if (FindParentAndSlot(Cue, Node, Parent, Slot) && Parent && Parent->ChildNodes.IsValidIndex(Slot))
	{
		Parent->ChildNodes[Slot] = nullptr;
	}
}

bool FNexusSoundCueUtils::AddNode(USoundCue* Cue, UClass* NodeClass, int32 ParentNodeIndex, int32 ChildSlot,
	USoundWave* OptionalWave, int32& OutNodeIndex, FString& OutError)
{
	if (!Cue || !NodeClass)
	{
		OutError = TEXT("Cue 或 NodeClass 无效");
		return false;
	}

	USoundNode* NewNode = NewObject<USoundNode>(Cue, NodeClass);
	if (!NewNode)
	{
		OutError = TEXT("创建 SoundNode 失败");
		return false;
	}

	if (USoundNodeWavePlayer* Player = Cast<USoundNodeWavePlayer>(NewNode))
	{
		if (OptionalWave)
		{
#if NX_UE_HAS_SOUND_NODE_WAVE_ACCESSOR
			Player->SetSoundWave(OptionalWave);
#endif
		}
	}

	if (!Cue->FirstNode)
	{
		Cue->FirstNode = NewNode;
	}
	else if (ParentNodeIndex < 0)
	{
		OutError = TEXT("已有根节点时 add_node 需要 parentNodeIndex");
		return false;
	}
	else
	{
		USoundNode* Parent = FindNodeByIndex(Cue, ParentNodeIndex);
		if (!Parent)
		{
			OutError = FString::Printf(TEXT("parentNodeIndex %d 无效"), ParentNodeIndex);
			return false;
		}
		if (ChildSlot < 0) ChildSlot = 0;
		if (Parent->ChildNodes.Num() <= ChildSlot)
		{
			Parent->ChildNodes.SetNum(ChildSlot + 1);
		}
		Parent->ChildNodes[ChildSlot] = NewNode;
	}

	TArray<USoundNode*> Nodes;
	CollectNodesInOrder(Cue, Nodes);
	OutNodeIndex = Nodes.IndexOfByKey(NewNode);
	Cue->MarkPackageDirty();
	return true;
}

bool FNexusSoundCueUtils::RemoveNode(USoundCue* Cue, int32 NodeIndex, FString& OutError)
{
	USoundNode* Node = FindNodeByIndex(Cue, NodeIndex);
	if (!Cue || !Node)
	{
		OutError = FString::Printf(TEXT("nodeIndex %d 无效"), NodeIndex);
		return false;
	}

	DisconnectNode(Cue, Node);
#if NX_UE_HAS_MARK_AS_GARBAGE
	Node->MarkAsGarbage();
#else
	Node->MarkPendingKill();
#endif
	Cue->MarkPackageDirty();
	return true;
}

bool FNexusSoundCueUtils::ConnectNodes(USoundCue* Cue, int32 ParentIndex, int32 ChildSlot, int32 ChildIndex, FString& OutError)
{
	if (!Cue)
	{
		OutError = TEXT("Cue 无效");
		return false;
	}
	USoundNode* Child = FindNodeByIndex(Cue, ChildIndex);
	if (!Child)
	{
		OutError = FString::Printf(TEXT("childIndex %d 无效"), ChildIndex);
		return false;
	}

	DisconnectNode(Cue, Child);

	if (ParentIndex < 0)
	{
		Cue->FirstNode = Child;
	}
	else
	{
		USoundNode* Parent = FindNodeByIndex(Cue, ParentIndex);
		if (!Parent)
		{
			OutError = FString::Printf(TEXT("parentNodeIndex %d 无效"), ParentIndex);
			return false;
		}
		if (ChildSlot < 0) ChildSlot = 0;
		if (Parent->ChildNodes.Num() <= ChildSlot)
		{
			Parent->ChildNodes.SetNum(ChildSlot + 1);
		}
		Parent->ChildNodes[ChildSlot] = Child;
	}

	Cue->MarkPackageDirty();
	return true;
}
