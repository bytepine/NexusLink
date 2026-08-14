// Copyright byteyang. All Rights Reserved.

#include "Capabilities/Asset/GAS/NexusManageAssetGameplayCueNotifyCapability.h"

#if WITH_GAS

#include "NexusCapabilityRegistry.h"
#include "NexusMcpSchemaBuilder.h"
#include "Utils/NexusAssetUtils.h"
#include "Utils/NexusCapabilityResultBuilder.h"
#include "Utils/NexusJsonUtils.h"
#include "GameplayCueNotify_Static.h"
#include "NexusMcpTool.h"

void FManageAssetGameplayCueNotifyCapability::BuildDefinition(FNexusCapabilityDefinition& Out) const
{
	Out.Name = TEXT("manage_asset_gameplay_cue_notify");
	Out.SearchAssetTypes = {TEXT("GameplayCueNotify_Static")};
	Out.Description = TEXT("批量编辑 GameplayCueNotify_Static。operations[].action=set_cue_name。Actor BP 图走 blueprint。");
	TSharedPtr<FJsonObject> OpSchema = FNexusSchema::Object()
		.Prop(TEXT("action"), FNexusSchema::Enum(TEXT("操作"), { TEXT("set_cue_name") }))
		.Prop(TEXT("cueName"), FNexusSchema::Str(TEXT("GameplayCue Tag")))
		.Required({ TEXT("action") })
		.Build();
	Out.InputSchema = FNexusSchema::Object()
		.Prop(TEXT("assetPath"), FNexusSchema::Str(TEXT("Cue Notify 资产路径")))
		.Prop(TEXT("operations"), FNexusSchema::ArrayOf(TEXT("批量操作（至少一项）"), OpSchema.ToSharedRef()))
		.Required({ TEXT("assetPath"), TEXT("operations") })
		.Build();
	Out.Tags = { FNexusMcpTags::Write, FNexusMcpTags::Gas };
	Out.ExtraSearchKeywords = { TEXT("cue"), TEXT("notify"), TEXT("tag") };
	Out.RelatedCapabilities = {
		TEXT("get_asset_gameplay_cue_notify"), TEXT("create_asset_gameplay_cue_notify"),
		TEXT("manage_asset_blueprint")
	};
}

FCapabilityResult FManageAssetGameplayCueNotifyCapability::Execute(const TSharedPtr<FJsonObject>& Arguments) const
{
	return FNexusCapabilityResultBuilder::Build([&](auto& OutEntries, auto& OutTop, auto& OutError)
	{
		FString AssetPath;
		if (!FNexusCapability::RequireString(Arguments, TEXT("assetPath"), AssetPath, OutEntries, {})) return;

		UGameplayCueNotify_Static* Notify = FNexusAssetUtils::LoadAssetWithFallback<UGameplayCueNotify_Static>(AssetPath);
		if (!Notify)
		{
			FNexusCapability::EmitError(OutEntries, {{TEXT("path"), AssetPath}},
				TEXT("仅支持 GameplayCueNotify_Static；Actor BP 请用 manage_asset_blueprint"));
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
			const TSharedPtr<FJsonObject>* OpPtr = nullptr;
			if (!OpVal.IsValid() || !OpVal->TryGetObject(OpPtr) || !OpPtr) continue;
			const TSharedPtr<FJsonObject>& Op = *OpPtr;
			FString Action;
			Op->TryGetStringField(TEXT("action"), Action);
			Action = Action.ToLower();

			TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
			Entry->SetStringField(TEXT("path"), AssetPath);
			Entry->SetStringField(TEXT("action"), Action);

			if (Action == TEXT("set_cue_name"))
			{
				FString CueName;
				if (!Op->TryGetStringField(TEXT("cueName"), CueName) || CueName.IsEmpty())
				{
					Entry->SetStringField(TEXT("error"), TEXT("set_cue_name 需要 cueName"));
					OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
					continue;
				}
				Notify->GameplayCueName = FName(*CueName);
				bDirty = true;
				Entry->SetStringField(TEXT("cueName"), Notify->GameplayCueName.ToString());
			}
			else
			{
				Entry->SetStringField(TEXT("error"), FString::Printf(TEXT("不支持的操作: '%s'"), *Action));
			}
			OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
		}
		if (bDirty) Notify->MarkPackageDirty();
	});
}

REGISTER_MCP_CAPABILITY(FManageAssetGameplayCueNotifyCapability)

#endif
