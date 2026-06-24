// Copyright byteyang. All Rights Reserved.

#include "Capabilities/Asset/Audio/NexusManageAssetSoundCueCapability.h"
#include "Utils/NexusCapabilityResultBuilder.h"
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
	Out.Description = TEXT("编辑 SoundCue。set_property/add_node/remove_node/connect_nodes。");
	Out.InputSchema = FNexusSchema::Object()
		.Prop(TEXT("assetPath"),       FNexusSchema::Str(TEXT("SoundCue 资产路径")))
		.Prop(TEXT("action"),          FNexusSchema::Enum(TEXT("操作"),
			{ TEXT("set_property"), TEXT("add_node"), TEXT("remove_node"), TEXT("connect_nodes") }))
		.Prop(TEXT("propertyPath"),    FNexusSchema::Str(TEXT("属性路径（set_property）")))
		.Prop(TEXT("value"),           FNexusSchema::Str(TEXT("属性新值字符串")))
		.Prop(TEXT("nodeClass"),       FNexusSchema::Str(TEXT("SoundNode 类名（add_node，如 SoundNodeWavePlayer）")))
		.Prop(TEXT("soundWavePath"),   FNexusSchema::Str(TEXT("SoundWave 路径（WavePlayer 可选）")))
		.Prop(TEXT("parentNodeIndex"), FNexusSchema::Int(TEXT("父节点索引（add_node/connect_nodes）"), -1, -1))
		.Prop(TEXT("childSlot"),       FNexusSchema::Int(TEXT("父节点子槽（add_node/connect_nodes）"), 0, 0))
		.Prop(TEXT("nodeIndex"),       FNexusSchema::Int(TEXT("节点索引（remove_node）"), 0, 0))
		.Prop(TEXT("childIndex"),      FNexusSchema::Int(TEXT("子节点索引（connect_nodes）"), 0, 0))
		.Required({ TEXT("assetPath"), TEXT("action") })
		.Build();
	Out.Tags = { FNexusMcpTags::Write, FNexusMcpTags::Editor };
	Out.ExtraSearchKeywords = { TEXT("audio"), TEXT("cue"), TEXT("volume"), TEXT("pitch"), TEXT("node") };
	Out.RelatedCapabilities = { TEXT("get_asset_sound_cue"), TEXT("get_asset_sound_wave") };
	Out.Prerequisites = { TEXT("editor_only") };
	Out.WhenToUse = TEXT("改 Cue 属性或节点图；索引与 get_asset_sound_cue nodes[] 一致");
}

FCapabilityResult FManageAssetSoundCueCapability::Execute(const TSharedPtr<FJsonObject>& Arguments) const
{
	return FNexusCapabilityResultBuilder::Build([&](auto& OutEntries, auto& OutTop, auto& OutError)
	{
		FString AssetPath, Action;
		if (!FNexusCapability::RequireString(Arguments, TEXT("assetPath"), AssetPath, OutEntries, {})) return;
		if (!FNexusCapability::RequireString(Arguments, TEXT("action"), Action, OutEntries, {{TEXT("assetPath"), AssetPath}})) return;

		USoundCue* Cue = FNexusAssetUtils::LoadAssetWithFallback<USoundCue>(AssetPath);
		if (!Cue)
		{
			FNexusCapability::EmitError(OutEntries, {{TEXT("assetPath"), AssetPath}},
				FString::Printf(TEXT("SoundCue 未找到: %s"), *AssetPath));
			return;
		}

		TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
		Entry->SetStringField(TEXT("assetPath"), AssetPath);
		Entry->SetStringField(TEXT("action"), Action);

		if (Action.Equals(TEXT("set_property"), ESearchCase::IgnoreCase))
		{
			FString PropPath, Value;
			if (!Arguments.IsValid()
				|| !Arguments->TryGetStringField(TEXT("propertyPath"), PropPath) || PropPath.IsEmpty()
				|| !Arguments->TryGetStringField(TEXT("value"), Value) || Value.IsEmpty())
			{
				Entry->SetStringField(TEXT("error"), TEXT("set_property 需要 propertyPath 和 value"));
				OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
				return;
			}
			FString OldVal, ActualVal, Err;
			if (!FNexusPropertyUtils::WritePropertyAndEcho(Cue, { PropPath }, 0, Value, OldVal, ActualVal, Err))
			{
				Entry->SetStringField(TEXT("error"), Err);
				OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
				return;
			}
			Cue->MarkPackageDirty();
			Entry->SetStringField(TEXT("propertyPath"), PropPath);
			if (!OldVal.IsEmpty()) Entry->SetStringField(TEXT("oldValue"), OldVal);
			if (!ActualVal.IsEmpty()) Entry->SetStringField(TEXT("newValue"), ActualVal);
			Entry->SetBoolField(TEXT("success"), true);
			Entry->SetStringField(TEXT("note"), TEXT("用 save_asset 落盘"));
		}
		else if (Action.Equals(TEXT("add_node"), ESearchCase::IgnoreCase))
		{
			FString NodeClass, WavePath;
			if (!Arguments.IsValid() || !Arguments->TryGetStringField(TEXT("nodeClass"), NodeClass) || NodeClass.IsEmpty())
			{
				Entry->SetStringField(TEXT("error"), TEXT("add_node 需要 nodeClass"));
				OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
				return;
			}
			int32 ParentIdx = -1;
			int32 ChildSlot = 0;
			if (Arguments->HasField(TEXT("parentNodeIndex")))
			{
				ParentIdx = static_cast<int32>(Arguments->GetNumberField(TEXT("parentNodeIndex")));
			}
			if (Arguments->HasField(TEXT("childSlot")))
			{
				ChildSlot = static_cast<int32>(Arguments->GetNumberField(TEXT("childSlot")));
			}
			Arguments->TryGetStringField(TEXT("soundWavePath"), WavePath);
			USoundWave* Wave = WavePath.IsEmpty()
				? nullptr
				: FNexusAssetUtils::LoadAssetWithFallback<USoundWave>(WavePath);

			FString ClassErr;
			UClass* Class = FNexusSoundCueUtils::ResolveSoundNodeClass(NodeClass, ClassErr);
			if (!Class)
			{
				Entry->SetStringField(TEXT("error"), ClassErr);
				OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
				return;
			}
			int32 NewIdx = -1;
			FString OpErr;
			if (!FNexusSoundCueUtils::AddNode(Cue, Class, ParentIdx, ChildSlot, Wave, NewIdx, OpErr))
			{
				Entry->SetStringField(TEXT("error"), OpErr);
				OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
				return;
			}
			Entry->SetNumberField(TEXT("nodeIndex"), static_cast<double>(NewIdx));
			Entry->SetStringField(TEXT("nodeClass"), Class->GetName());
			Entry->SetBoolField(TEXT("success"), true);
			Entry->SetStringField(TEXT("note"), TEXT("用 save_asset 落盘"));
		}
		else if (Action.Equals(TEXT("remove_node"), ESearchCase::IgnoreCase))
		{
			if (!Arguments.IsValid() || !Arguments->HasField(TEXT("nodeIndex")))
			{
				Entry->SetStringField(TEXT("error"), TEXT("remove_node 需要 nodeIndex"));
				OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
				return;
			}
			const int32 NodeIdx = static_cast<int32>(Arguments->GetNumberField(TEXT("nodeIndex")));
			FString OpErr;
			if (!FNexusSoundCueUtils::RemoveNode(Cue, NodeIdx, OpErr))
			{
				Entry->SetStringField(TEXT("error"), OpErr);
				OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
				return;
			}
			Entry->SetNumberField(TEXT("removedNodeIndex"), static_cast<double>(NodeIdx));
			Entry->SetBoolField(TEXT("success"), true);
			Entry->SetStringField(TEXT("note"), TEXT("用 save_asset 落盘"));
		}
		else if (Action.Equals(TEXT("connect_nodes"), ESearchCase::IgnoreCase))
		{
			if (!Arguments.IsValid()
				|| !Arguments->HasField(TEXT("childIndex")))
			{
				Entry->SetStringField(TEXT("error"), TEXT("connect_nodes 需要 childIndex"));
				OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
				return;
			}
			int32 ParentIdx = -1;
			int32 ChildSlot = 0;
			const int32 ChildIdx = static_cast<int32>(Arguments->GetNumberField(TEXT("childIndex")));
			if (Arguments->HasField(TEXT("parentNodeIndex")))
			{
				ParentIdx = static_cast<int32>(Arguments->GetNumberField(TEXT("parentNodeIndex")));
			}
			if (Arguments->HasField(TEXT("childSlot")))
			{
				ChildSlot = static_cast<int32>(Arguments->GetNumberField(TEXT("childSlot")));
			}
			FString OpErr;
			if (!FNexusSoundCueUtils::ConnectNodes(Cue, ParentIdx, ChildSlot, ChildIdx, OpErr))
			{
				Entry->SetStringField(TEXT("error"), OpErr);
				OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
				return;
			}
			Entry->SetBoolField(TEXT("success"), true);
			Entry->SetStringField(TEXT("note"), TEXT("用 save_asset 落盘"));
		}
		else
		{
			Entry->SetStringField(TEXT("error"), FString::Printf(TEXT("未知 action: %s"), *Action));
		}

		OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
	});
}

REGISTER_MCP_CAPABILITY(FManageAssetSoundCueCapability)
