// Copyright byteyang. All Rights Reserved.

#include "Capabilities/Asset/Audio/NexusManageAssetSoundCueCapability.h"
#include "Utils/NexusArgs.h"
#include "NexusCapabilityRegistry.h"
#include "NexusMcpSchemaBuilder.h"
#include "Utils/NexusAssetUtils.h"
#include "Utils/NexusPropertyUtils.h"
#include "Utils/NexusSoundCueUtils.h"
#include "Sound/SoundCue.h"
#include "Sound/SoundWave.h"
#include "NexusMcpTool.h"

void FNexusManageAssetSoundCueCapability::BuildDefinition(FNexusCapabilityDefinition& Out) const
{
	Out.Name = TEXT("ManageAssetSoundCue");
	Out.SearchAssetTypes = {TEXT("SoundCue")};
	Out.Description = TEXT("Batch edit SoundCue. Action=SetProperty/AddNode/RemoveNode/ConnectNodes.");
	TSharedPtr<FJsonObject> OpSchema = FNexusSchema::Object()
		.Prop(TEXT("Action"),          FNexusSchema::Enum(TEXT("Action"),
			{ TEXT("SetProperty"), TEXT("AddNode"), TEXT("RemoveNode"), TEXT("ConnectNodes") }))
		.Prop(TEXT("PropertyPath"),    FNexusSchema::Str(TEXT("Property path (SetProperty)")))
		.Prop(TEXT("Value"),           FNexusSchema::Str(TEXT("New property value string")))
		.Prop(TEXT("NodeClass"),       FNexusSchema::Str(TEXT("SoundNode class (AddNode, e.g. SoundNodeWavePlayer)")))
		.Prop(TEXT("SoundWavePath"),   FNexusSchema::Str(TEXT("SoundWave path (WavePlayer optional)")))
		.Prop(TEXT("ParentNodeIndex"), FNexusSchema::Int(TEXT("Parent node index (AddNode/ConnectNodes)"), -1, -1))
		.Prop(TEXT("ChildSlot"),       FNexusSchema::Int(TEXT("Parent child slot (AddNode/ConnectNodes)"), 0, 0))
		.Prop(TEXT("NodeIndex"),       FNexusSchema::Int(TEXT("Node index (RemoveNode)"), 0, 0))
		.Prop(TEXT("ChildIndex"),      FNexusSchema::Int(TEXT("Child node index (ConnectNodes)"), 0, 0))
		.Required({ TEXT("Action") })
		.Build();
	Out.InputSchema = FNexusSchema::Object()
		.Prop(TEXT("AssetPath"),  FNexusSchema::Str(TEXT("SoundCue asset path")))
		.Prop(TEXT("Operations"), FNexusSchema::ArrayOf(TEXT("Batch ops (at least one)"), OpSchema.ToSharedRef()))
		.Required({ TEXT("AssetPath"), TEXT("Operations") })
		.Build();
	Out.Tags = { FNexusMcpTags::Write, FNexusMcpTags::Editor };
	Out.ExtraSearchKeywords = { TEXT("audio"), TEXT("cue"), TEXT("Volume"), TEXT("Pitch"), TEXT("Node") };
	Out.RelatedCapabilities = { TEXT("GetAssetSoundCue"), TEXT("CreateAssetSoundCue"), TEXT("GetAssetSoundWave") };
	Out.Prerequisites = { TEXT("EditorOnly") };
	Out.WhenToUse = TEXT("Edit Cue props or node graph; indices match GetAssetSoundCue nodes[]");
}

static USoundCue* CueFrom(FNexusActionContext& Ctx)
{
	return static_cast<USoundCue*>(Ctx.Target);
}

static void HandleCue_SetProperty(const TSharedPtr<FJsonObject>& Op, FNexusActionContext& Ctx)
{
	USoundCue* Cue = CueFrom(Ctx);
	const FNexusArgs A(Op);
	const FString PropPath = A.Str(TEXT("PropertyPath"));
	const FString Value = A.Str(TEXT("Value"));
	if (PropPath.IsEmpty() || Value.IsEmpty())
	{
		Ctx.Entry->SetStringField(TEXT("Error"), TEXT("SetProperty requires PropertyPath and value"));
		return;
	}
	FString OldVal, ActualVal, Err;
	if (!FNexusPropertyUtils::WritePropertyAndEcho(Cue, { PropPath }, 0, Value, OldVal, ActualVal, Err))
	{
		Ctx.Entry->SetStringField(TEXT("Error"), Err);
		return;
	}
	Cue->MarkPackageDirty();
	Ctx.Entry->SetStringField(TEXT("PropertyPath"), PropPath);
	if (!OldVal.IsEmpty()) Ctx.Entry->SetStringField(TEXT("OldValue"), OldVal);
	if (!ActualVal.IsEmpty()) Ctx.Entry->SetStringField(TEXT("NewValue"), ActualVal);
	Ctx.Entry->SetStringField(TEXT("Note"), TEXT("persist with SaveAsset"));
}

static void HandleCue_AddNode(const TSharedPtr<FJsonObject>& Op, FNexusActionContext& Ctx)
{
	USoundCue* Cue = CueFrom(Ctx);
	const FString NodeClass = FNexusArgs(Op).Str(TEXT("NodeClass"));
	if (NodeClass.IsEmpty())
	{
		Ctx.Entry->SetStringField(TEXT("Error"), TEXT("AddNode requires NodeClass"));
		return;
	}
	int32 ParentIdx = -1;
	int32 ChildSlot = 0;
	if (Op->HasField(TEXT("ParentNodeIndex")))
	{
		ParentIdx = static_cast<int32>(Op->GetNumberField(TEXT("ParentNodeIndex")));
	}
	if (Op->HasField(TEXT("ChildSlot")))
	{
		ChildSlot = static_cast<int32>(Op->GetNumberField(TEXT("ChildSlot")));
	}
	const FString WavePath = FNexusArgs(Op).Str(TEXT("SoundWavePath"));
	USoundWave* Wave = WavePath.IsEmpty()
		? nullptr
		: FNexusAssetUtils::LoadAssetWithFallback<USoundWave>(WavePath);

	FString ClassErr;
	UClass* Class = FNexusSoundCueUtils::ResolveSoundNodeClass(NodeClass, ClassErr);
	if (!Class)
	{
		Ctx.Entry->SetStringField(TEXT("Error"), ClassErr);
		return;
	}
	int32 NewIdx = -1;
	FString OpErr;
	if (!FNexusSoundCueUtils::AddNode(Cue, Class, ParentIdx, ChildSlot, Wave, NewIdx, OpErr))
	{
		Ctx.Entry->SetStringField(TEXT("Error"), OpErr);
		return;
	}
	Ctx.Entry->SetNumberField(TEXT("NodeIndex"), static_cast<double>(NewIdx));
	Ctx.Entry->SetStringField(TEXT("NodeClass"), Class->GetName());
	Ctx.Entry->SetStringField(TEXT("Note"), TEXT("persist with SaveAsset"));
}

static void HandleCue_RemoveNode(const TSharedPtr<FJsonObject>& Op, FNexusActionContext& Ctx)
{
	USoundCue* Cue = CueFrom(Ctx);
	if (!Op->HasField(TEXT("NodeIndex")))
	{
		Ctx.Entry->SetStringField(TEXT("Error"), TEXT("RemoveNode requires NodeIndex"));
		return;
	}
	const int32 NodeIdx = static_cast<int32>(Op->GetNumberField(TEXT("NodeIndex")));
	FString OpErr;
	if (!FNexusSoundCueUtils::RemoveNode(Cue, NodeIdx, OpErr))
	{
		Ctx.Entry->SetStringField(TEXT("Error"), OpErr);
		return;
	}
	Ctx.Entry->SetNumberField(TEXT("RemovedNodeIndex"), static_cast<double>(NodeIdx));
	Ctx.Entry->SetStringField(TEXT("Note"), TEXT("persist with SaveAsset"));
}

static void HandleCue_ConnectNodes(const TSharedPtr<FJsonObject>& Op, FNexusActionContext& Ctx)
{
	USoundCue* Cue = CueFrom(Ctx);
	if (!Op->HasField(TEXT("ChildIndex")))
	{
		Ctx.Entry->SetStringField(TEXT("Error"), TEXT("ConnectNodes requires ChildIndex"));
		return;
	}
	int32 ParentIdx = -1;
	int32 ChildSlot = 0;
	const int32 ChildIdx = static_cast<int32>(Op->GetNumberField(TEXT("ChildIndex")));
	if (Op->HasField(TEXT("ParentNodeIndex")))
	{
		ParentIdx = static_cast<int32>(Op->GetNumberField(TEXT("ParentNodeIndex")));
	}
	if (Op->HasField(TEXT("ChildSlot")))
	{
		ChildSlot = static_cast<int32>(Op->GetNumberField(TEXT("ChildSlot")));
	}
	FString OpErr;
	if (!FNexusSoundCueUtils::ConnectNodes(Cue, ParentIdx, ChildSlot, ChildIdx, OpErr))
	{
		Ctx.Entry->SetStringField(TEXT("Error"), OpErr);
		return;
	}
	Ctx.Entry->SetStringField(TEXT("Note"), TEXT("persist with SaveAsset"));
}

bool FNexusManageAssetSoundCueCapability::PrepareTarget(
	const TSharedPtr<FJsonObject>& Args,
	TSharedPtr<FJsonObject>& Entry,
	void*& OutTarget,
	FString& OutError) const
{
	const FString AssetPath = FNexusArgs(Args).Str(TEXT("AssetPath"));
	Entry->SetStringField(TEXT("Path"), AssetPath);
	USoundCue* Cue = FNexusAssetUtils::LoadAssetWithFallback<USoundCue>(AssetPath);
	if (!Cue)
	{
		OutError = FString::Printf(TEXT("SoundCue not found: %s"), *AssetPath);
		return false;
	}
	OutTarget = Cue;
	return true;
}

void FNexusManageAssetSoundCueCapability::RegisterActions(TMap<FString, FNexusActionHandler>& OutHandlers) const
{
	OutHandlers.Add(TEXT("SetProperty"),  &HandleCue_SetProperty);
	OutHandlers.Add(TEXT("AddNode"),      &HandleCue_AddNode);
	OutHandlers.Add(TEXT("RemoveNode"),   &HandleCue_RemoveNode);
	OutHandlers.Add(TEXT("ConnectNodes"), &HandleCue_ConnectNodes);
}

REGISTER_MCP_CAPABILITY(FNexusManageAssetSoundCueCapability)
