// Copyright byteyang. All Rights Reserved.

#include "Capabilities/Asset/Audio/NexusManageAssetSoundWaveCapability.h"
#include "Utils/NexusCapabilityResultBuilder.h"
#include "Utils/NexusJsonUtils.h"
#include "NexusCapabilityRegistry.h"
#include "NexusMcpSchemaBuilder.h"
#include "Utils/NexusAssetUtils.h"
#include "Utils/NexusPropertyUtils.h"
#include "Sound/SoundWave.h"
#include "NexusMcpTool.h"

void FManageAssetSoundWaveCapability::BuildDefinition(FNexusCapabilityDefinition& Out) const
{
	Out.Name = TEXT("manage_asset_sound_wave");
	Out.SearchAssetTypes = {TEXT("SoundWave")};
	Out.Description = TEXT("批量编辑 SoundWave 属性。operations[].action=set_property。音量/循环等。");
	TSharedPtr<FJsonObject> OpSchema = FNexusSchema::Object()
		.Prop(TEXT("action"),       FNexusSchema::Enum(TEXT("操作"), { TEXT("set_property") }))
		.Prop(TEXT("propertyPath"), FNexusSchema::Str(TEXT("属性路径（如 Volume/Looping）")))
		.Prop(TEXT("value"),        FNexusSchema::Str(TEXT("属性新值字符串")))
		.Required({ TEXT("action") })
		.Build();
	Out.InputSchema = FNexusSchema::Object()
		.Prop(TEXT("assetPath"),  FNexusSchema::Str(TEXT("SoundWave 资产路径")))
		.Prop(TEXT("operations"), FNexusSchema::ArrayOf(TEXT("批量属性操作（至少一项）"), OpSchema.ToSharedRef()))
		.Required({ TEXT("assetPath"), TEXT("operations") })
		.Build();
	Out.Tags = { FNexusMcpTags::Write, FNexusMcpTags::Editor };
	Out.ExtraSearchKeywords = { TEXT("audio"), TEXT("wave"), TEXT("volume"), TEXT("loop"), TEXT("sound") };
	Out.RelatedCapabilities = { TEXT("get_asset_sound_wave"), TEXT("get_asset_sound_cue") };
	Out.Prerequisites = { TEXT("editor_only") };
	Out.WhenToUse = TEXT("改 SoundWave 音量/循环/衰减；修改后需 save_asset 落盘");
}

FCapabilityResult FManageAssetSoundWaveCapability::Execute(const TSharedPtr<FJsonObject>& Arguments) const
{
	return FNexusCapabilityResultBuilder::Build([&](auto& OutEntries, auto& OutTop, auto& OutError)
	{
		FString AssetPath;
		if (!FNexusCapability::RequireString(Arguments, TEXT("assetPath"), AssetPath, OutEntries, {})) return;

		USoundWave* Wave = FNexusAssetUtils::LoadAssetWithFallback<USoundWave>(AssetPath);
		if (!Wave)
		{
			FNexusCapability::EmitError(OutEntries, {{TEXT("path"), AssetPath}},
				FString::Printf(TEXT("SoundWave 未找到: %s"), *AssetPath));
			return;
		}

		const TArray<TSharedPtr<FJsonValue>> Ops = FNexusJsonUtils::ExtractOperations(Arguments);
		if (Ops.Num() == 0)
		{
			FNexusCapability::EmitError(OutEntries, {{TEXT("path"), AssetPath}}, TEXT("缺少 operations 或为空"));
			return;
		}

		bool bDirty = false;
		for (const TSharedPtr<FJsonValue>& OpVal : Ops)
		{
			const TSharedPtr<FJsonObject>* OpObjPtr = nullptr;
			if (!OpVal.IsValid() || !OpVal->TryGetObject(OpObjPtr) || !OpObjPtr) continue;
			const TSharedPtr<FJsonObject>& Op = *OpObjPtr;

			FString Action, PropPath, Value;
			Op->TryGetStringField(TEXT("action"), Action);
			Op->TryGetStringField(TEXT("propertyPath"), PropPath);
			Op->TryGetStringField(TEXT("value"), Value);

			TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
			Entry->SetStringField(TEXT("path"), AssetPath);
			Entry->SetStringField(TEXT("action"), Action);

			if (Action.Equals(TEXT("set_property"), ESearchCase::IgnoreCase))
			{
				if (PropPath.IsEmpty() || Value.IsEmpty())
				{
					Entry->SetStringField(TEXT("error"), TEXT("set_property 需要 propertyPath 和 value"));
					OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
					continue;
				}
				FString OldVal, ActualVal, Err;
				if (!FNexusPropertyUtils::WritePropertyAndEcho(Wave, { PropPath }, 0, Value, OldVal, ActualVal, Err))
				{
					Entry->SetStringField(TEXT("error"), Err);
					OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
					continue;
				}
				bDirty = true;
				Entry->SetStringField(TEXT("propertyPath"), PropPath);
				if (!OldVal.IsEmpty()) Entry->SetStringField(TEXT("oldValue"), OldVal);
				if (!ActualVal.IsEmpty()) Entry->SetStringField(TEXT("newValue"), ActualVal);
				Entry->SetStringField(TEXT("note"), TEXT("用 save_asset 落盘"));
			}
			else
			{
				Entry->SetStringField(TEXT("error"), FString::Printf(TEXT("未知 action: %s"), *Action));
			}

			OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
		}

		if (bDirty) Wave->MarkPackageDirty();
	});
}

REGISTER_MCP_CAPABILITY(FManageAssetSoundWaveCapability)
