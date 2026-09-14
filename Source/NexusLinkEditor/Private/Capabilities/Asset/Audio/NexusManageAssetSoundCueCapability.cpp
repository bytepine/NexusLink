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

void FManageAssetSoundCueCapability::BuildDefinition(FNexusCapabilityDefinition& Out) const
{
	Out.Name = TEXT("manage_asset_sound_cue");
	Out.SearchAssetTypes = {TEXT("SoundCue")};
	Out.Description = TEXT("Batch edit SoundCue. action=set_property/add_node/remove_node/connect_nodes.");
	TSharedPtr<FJsonObject> OpSchema = FNexusSchema::Object()
		.Prop(TEXT("action"),          FNexusSchema::Enum(TEXT("Action"),
			{ TEXT("set_property"), TEXT("add_node"), TEXT("remove_node"), TEXT("connect_nodes") }))
		.Prop(TEXT("propertyPath"),    FNexusSchema::Str(TEXT("Property path (set_property)")))
		.Prop(TEXT("value"),           FNexusSchema::Str(TEXT("New property value string")))
		.Prop(TEXT("nodeClass"),       FNexusSchema::Str(TEXT("SoundNode class (add_node, e.g. SoundNodeWavePlayer)")))
		.Prop(TEXT("soundWavePath"),   FNexusSchema::Str(TEXT("SoundWave path (WavePlayer optional)")))
		.Prop(TEXT("parentNodeIndex"), FNexusSchema::Int(TEXT("Parent node index (add_node/connect_nodes)"), -1, -1))
		.Prop(TEXT("childSlot"),       FNexusSchema::Int(TEXT("Parent child slot (add_node/connect_nodes)"), 0, 0))
		.Prop(TEXT("nodeIndex"),       FNexusSchema::Int(TEXT("Node index (remove_node)"), 0, 0))
		.Prop(TEXT("childIndex"),      FNexusSchema::Int(TEXT("Child node index (connect_nodes)"), 0, 0))
		.Required({ TEXT("action") })
		.Build();
	Out.InputSchema = FNexusSchema::Object()
		.Prop(TEXT("assetPath"),  FNexusSchema::Str(TEXT("SoundCue asset path")))
		.Prop(TEXT("operations"), FNexusSchema::ArrayOf(TEXT("Batch ops (at least one)"), OpSchema.ToSharedRef()))
		.Required({ TEXT("assetPath"), TEXT("operations") })
		.Build();
	Out.Tags = { FNexusMcpTags::Write, FNexusMcpTags::Editor };
	Out.ExtraSearchKeywords = { TEXT("audio"), TEXT("cue"), TEXT("volume"), TEXT("pitch"), TEXT("node") };
	Out.RelatedCapabilities = { TEXT("get_asset_sound_cue"), TEXT("create_asset_sound_cue"), TEXT("get_asset_sound_wave") };
	Out.Prerequisites = { TEXT("editor_only") };
	Out.WhenToUse = TEXT("Edit Cue props or node graph; indices match get_asset_sound_cue nodes[]");
}

static USoundCue* CueFrom(FNexusActionContext& Ctx)
{
	return static_cast<USoundCue*>(Ctx.Target);
}

static void HandleCue_SetProperty(const TSharedPtr<FJsonObject>& Op, FNexusActionContext& Ctx)
{
	USoundCue* Cue = CueFrom(Ctx);
	const FNexusArgs A(Op);
	const FString PropPath = A.Str(TEXT("propertyPath"));
	const FString Value = A.Str(TEXT("value"));
	if (PropPath.IsEmpty() || Value.IsEmpty())
	{
		Ctx.Entry->SetStringField(TEXT("error"), TEXT("set_property requires propertyPath and value"));
		return;
	}
	FString OldVal, ActualVal, Err;
	if (!FNexusPropertyUtils::WritePropertyAndEcho(Cue, { PropPath }, 0, Value, OldVal, ActualVal, Err))
	{
		Ctx.Entry->SetStringField(TEXT("error"), Err);
		return;
	}
	Cue->MarkPackageDirty();
	Ctx.Entry->SetStringField(TEXT("propertyPath"), PropPath);
	if (!OldVal.IsEmpty()) Ctx.Entry->SetStringField(TEXT("oldValue"), OldVal);
	if (!ActualVal.IsEmpty()) Ctx.Entry->SetStringField(TEXT("newValue"), ActualVal);
	Ctx.Entry->SetStringField(TEXT("note"), TEXT("persist with save_asset"));
}

static void HandleCue_AddNode(const TSharedPtr<FJsonObject>& Op, FNexusActionContext& Ctx)
{
	USoundCue* Cue = CueFrom(Ctx);
	const FString NodeClass = FNexusArgs(Op).Str(TEXT("nodeClass"));
	if (NodeClass.IsEmpty())
	{
		Ctx.Entry->SetStringField(TEXT("error"), TEXT("add_node requires nodeClass"));
		return;
	}
	int32 ParentIdx = -1;
	int32 ChildSlot = 0;
	if (Op->HasField(TEXT("parentNodeIndex")))
	{
		ParentIdx = static_cast<int32>(Op->GetNumberField(TEXT("parentNodeIndex")));
	}
	if (Op->HasField(TEXT("childSlot")))
	{
		ChildSlot = static_cast<int32>(Op->GetNumberField(TEXT("childSlot")));
	}
	const FString WavePath = FNexusArgs(Op).Str(TEXT("soundWavePath"));
	USoundWave* Wave = WavePath.IsEmpty()
		? nullptr
		: FNexusAssetUtils::LoadAssetWithFallback<USoundWave>(WavePath);

	FString ClassErr;
	UClass* Class = FNexusSoundCueUtils::ResolveSoundNodeClass(NodeClass, ClassErr);
	if (!Class)
	{
		Ctx.Entry->SetStringField(TEXT("error"), ClassErr);
		return;
	}
	int32 NewIdx = -1;
	FString OpErr;
	if (!FNexusSoundCueUtils::AddNode(Cue, Class, ParentIdx, ChildSlot, Wave, NewIdx, OpErr))
	{
		Ctx.Entry->SetStringField(TEXT("error"), OpErr);
		return;
	}
	Ctx.Entry->SetNumberField(TEXT("nodeIndex"), static_cast<double>(NewIdx));
	Ctx.Entry->SetStringField(TEXT("nodeClass"), Class->GetName());
	Ctx.Entry->SetStringField(TEXT("note"), TEXT("persist with save_asset"));
}

static void HandleCue_RemoveNode(const TSharedPtr<FJsonObject>& Op, FNexusActionContext& Ctx)
{
	USoundCue* Cue = CueFrom(Ctx);
	if (!Op->HasField(TEXT("nodeIndex")))
	{
		Ctx.Entry->SetStringField(TEXT("error"), TEXT("remove_node requires nodeIndex"));
		return;
	}
	const int32 NodeIdx = static_cast<int32>(Op->GetNumberField(TEXT("nodeIndex")));
	FString OpErr;
	if (!FNexusSoundCueUtils::RemoveNode(Cue, NodeIdx, OpErr))
	{
		Ctx.Entry->SetStringField(TEXT("error"), OpErr);
		return;
	}
	Ctx.Entry->SetNumberField(TEXT("removedNodeIndex"), static_cast<double>(NodeIdx));
	Ctx.Entry->SetStringField(TEXT("note"), TEXT("persist with save_asset"));
}

static void HandleCue_ConnectNodes(const TSharedPtr<FJsonObject>& Op, FNexusActionContext& Ctx)
{
	USoundCue* Cue = CueFrom(Ctx);
	if (!Op->HasField(TEXT("childIndex")))
	{
		Ctx.Entry->SetStringField(TEXT("error"), TEXT("connect_nodes requires childIndex"));
		return;
	}
	int32 ParentIdx = -1;
	int32 ChildSlot = 0;
	const int32 ChildIdx = static_cast<int32>(Op->GetNumberField(TEXT("childIndex")));
	if (Op->HasField(TEXT("parentNodeIndex")))
	{
		ParentIdx = static_cast<int32>(Op->GetNumberField(TEXT("parentNodeIndex")));
	}
	if (Op->HasField(TEXT("childSlot")))
	{
		ChildSlot = static_cast<int32>(Op->GetNumberField(TEXT("childSlot")));
	}
	FString OpErr;
	if (!FNexusSoundCueUtils::ConnectNodes(Cue, ParentIdx, ChildSlot, ChildIdx, OpErr))
	{
		Ctx.Entry->SetStringField(TEXT("error"), OpErr);
		return;
	}
	Ctx.Entry->SetStringField(TEXT("note"), TEXT("persist with save_asset"));
}

bool FManageAssetSoundCueCapability::PrepareTarget(
	const TSharedPtr<FJsonObject>& Args,
	TSharedPtr<FJsonObject>& Entry,
	void*& OutTarget,
	FString& OutError) const
{
	const FString AssetPath = FNexusArgs(Args).Str(TEXT("assetPath"));
	Entry->SetStringField(TEXT("path"), AssetPath);
	USoundCue* Cue = FNexusAssetUtils::LoadAssetWithFallback<USoundCue>(AssetPath);
	if (!Cue)
	{
		OutError = FString::Printf(TEXT("SoundCue not found: %s"), *AssetPath);
		return false;
	}
	OutTarget = Cue;
	return true;
}

void FManageAssetSoundCueCapability::RegisterActions(TMap<FString, FNexusActionHandler>& OutHandlers) const
{
	OutHandlers.Add(TEXT("set_property"),  &HandleCue_SetProperty);
	OutHandlers.Add(TEXT("add_node"),      &HandleCue_AddNode);
	OutHandlers.Add(TEXT("remove_node"),   &HandleCue_RemoveNode);
	OutHandlers.Add(TEXT("connect_nodes"), &HandleCue_ConnectNodes);
}

REGISTER_MCP_CAPABILITY(FManageAssetSoundCueCapability)
