// Copyright byteyang. All Rights Reserved.

#include "Capabilities/Asset/Audio/NexusManageAssetSoundCueCapability.h"
#include "Utils/NexusCapabilityResultBuilder.h"
#include "Utils/NexusJsonUtils.h"
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

FCapabilityResult FManageAssetSoundCueCapability::Execute(const TSharedPtr<FJsonObject>& Arguments) const
{
	return FNexusCapabilityResultBuilder::Build([&](auto& OutEntries, auto& OutTop, auto& OutError)
	{
		FString AssetPath;
		if (!FNexusCapability::RequireString(Arguments, TEXT("assetPath"), AssetPath, OutEntries, {})) return;

		USoundCue* Cue = FNexusAssetUtils::LoadAssetWithFallback<USoundCue>(AssetPath);
		if (!Cue)
		{
			FNexusCapability::EmitError(OutEntries, {{TEXT("path"), AssetPath}},
				FString::Printf(TEXT("SoundCue not found: %s"), *AssetPath));
			return;
		}

		const TArray<TSharedPtr<FJsonValue>> Ops = FNexusJsonUtils::ExtractOperations(Arguments);
		if (Ops.Num() == 0)
		{
			FNexusCapability::EmitError(OutEntries, {{TEXT("path"), AssetPath}}, TEXT("Missing or empty operations"));
			return;
		}

		for (const TSharedPtr<FJsonValue>& OpVal : Ops)
		{
		const TSharedPtr<FJsonObject>* OpObjPtr = nullptr;
		if (!OpVal.IsValid() || !OpVal->TryGetObject(OpObjPtr) || !OpObjPtr) continue;
		const TSharedPtr<FJsonObject>& OpArgs = *OpObjPtr;

		FString Action;
		OpArgs->TryGetStringField(TEXT("action"), Action);

		TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
		Entry->SetStringField(TEXT("path"), AssetPath);
		Entry->SetStringField(TEXT("action"), Action);

		if (Action.Equals(TEXT("set_property"), ESearchCase::IgnoreCase))
		{
			FString PropPath, Value;
			if (!OpArgs.IsValid()
				|| !OpArgs->TryGetStringField(TEXT("propertyPath"), PropPath) || PropPath.IsEmpty()
				|| !OpArgs->TryGetStringField(TEXT("value"), Value) || Value.IsEmpty())
			{
				Entry->SetStringField(TEXT("error"), TEXT("set_property requires propertyPath and value"));
				OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
				continue;
			}
			FString OldVal, ActualVal, Err;
			if (!FNexusPropertyUtils::WritePropertyAndEcho(Cue, { PropPath }, 0, Value, OldVal, ActualVal, Err))
			{
				Entry->SetStringField(TEXT("error"), Err);
				OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
				continue;
			}
			Cue->MarkPackageDirty();
			Entry->SetStringField(TEXT("propertyPath"), PropPath);
			if (!OldVal.IsEmpty()) Entry->SetStringField(TEXT("oldValue"), OldVal);
			if (!ActualVal.IsEmpty()) Entry->SetStringField(TEXT("newValue"), ActualVal);
			Entry->SetStringField(TEXT("note"), TEXT("persist with save_asset"));
		}
		else if (Action.Equals(TEXT("add_node"), ESearchCase::IgnoreCase))
		{
			FString NodeClass, WavePath;
			if (!OpArgs.IsValid() || !OpArgs->TryGetStringField(TEXT("nodeClass"), NodeClass) || NodeClass.IsEmpty())
			{
				Entry->SetStringField(TEXT("error"), TEXT("add_node requires nodeClass"));
				OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
				continue;
			}
			int32 ParentIdx = -1;
			int32 ChildSlot = 0;
			if (OpArgs->HasField(TEXT("parentNodeIndex")))
			{
				ParentIdx = static_cast<int32>(OpArgs->GetNumberField(TEXT("parentNodeIndex")));
			}
			if (OpArgs->HasField(TEXT("childSlot")))
			{
				ChildSlot = static_cast<int32>(OpArgs->GetNumberField(TEXT("childSlot")));
			}
			OpArgs->TryGetStringField(TEXT("soundWavePath"), WavePath);
			USoundWave* Wave = WavePath.IsEmpty()
				? nullptr
				: FNexusAssetUtils::LoadAssetWithFallback<USoundWave>(WavePath);

			FString ClassErr;
			UClass* Class = FNexusSoundCueUtils::ResolveSoundNodeClass(NodeClass, ClassErr);
			if (!Class)
			{
				Entry->SetStringField(TEXT("error"), ClassErr);
				OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
				continue;
			}
			int32 NewIdx = -1;
			FString OpErr;
			if (!FNexusSoundCueUtils::AddNode(Cue, Class, ParentIdx, ChildSlot, Wave, NewIdx, OpErr))
			{
				Entry->SetStringField(TEXT("error"), OpErr);
				OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
				continue;
			}
			Entry->SetNumberField(TEXT("nodeIndex"), static_cast<double>(NewIdx));
			Entry->SetStringField(TEXT("nodeClass"), Class->GetName());
			Entry->SetStringField(TEXT("note"), TEXT("persist with save_asset"));
		}
		else if (Action.Equals(TEXT("remove_node"), ESearchCase::IgnoreCase))
		{
			if (!OpArgs.IsValid() || !OpArgs->HasField(TEXT("nodeIndex")))
			{
				Entry->SetStringField(TEXT("error"), TEXT("remove_node requires nodeIndex"));
				OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
				continue;
			}
			const int32 NodeIdx = static_cast<int32>(OpArgs->GetNumberField(TEXT("nodeIndex")));
			FString OpErr;
			if (!FNexusSoundCueUtils::RemoveNode(Cue, NodeIdx, OpErr))
			{
				Entry->SetStringField(TEXT("error"), OpErr);
				OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
				continue;
			}
			Entry->SetNumberField(TEXT("removedNodeIndex"), static_cast<double>(NodeIdx));
			Entry->SetStringField(TEXT("note"), TEXT("persist with save_asset"));
		}
		else if (Action.Equals(TEXT("connect_nodes"), ESearchCase::IgnoreCase))
		{
			if (!OpArgs.IsValid()
				|| !OpArgs->HasField(TEXT("childIndex")))
			{
				Entry->SetStringField(TEXT("error"), TEXT("connect_nodes requires childIndex"));
				OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
				continue;
			}
			int32 ParentIdx = -1;
			int32 ChildSlot = 0;
			const int32 ChildIdx = static_cast<int32>(OpArgs->GetNumberField(TEXT("childIndex")));
			if (OpArgs->HasField(TEXT("parentNodeIndex")))
			{
				ParentIdx = static_cast<int32>(OpArgs->GetNumberField(TEXT("parentNodeIndex")));
			}
			if (OpArgs->HasField(TEXT("childSlot")))
			{
				ChildSlot = static_cast<int32>(OpArgs->GetNumberField(TEXT("childSlot")));
			}
			FString OpErr;
			if (!FNexusSoundCueUtils::ConnectNodes(Cue, ParentIdx, ChildSlot, ChildIdx, OpErr))
			{
				Entry->SetStringField(TEXT("error"), OpErr);
				OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
				continue;
			}
			Entry->SetStringField(TEXT("note"), TEXT("persist with save_asset"));
		}
		else
		{
			Entry->SetStringField(TEXT("error"), FString::Printf(TEXT("Unknown action: %s"), *Action));
		}

		OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
		}
	});
}

REGISTER_MCP_CAPABILITY(FManageAssetSoundCueCapability)
