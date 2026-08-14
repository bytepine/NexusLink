// Copyright byteyang. All Rights Reserved.

#include "Capabilities/Asset/GAS/NexusGetAssetGameplayCueNotifyCapability.h"

#if WITH_GAS

#include "NexusCapabilityRegistry.h"
#include "NexusMcpSchemaBuilder.h"
#include "Utils/NexusAssetUtils.h"
#include "Utils/NexusCapabilityResultBuilder.h"
#include "GameplayCueNotify_Static.h"
#include "GameplayCueNotify_Actor.h"
#include "Engine/Blueprint.h"
#include "Engine/BlueprintGeneratedClass.h"
#include "NexusMcpTool.h"

void FGetAssetGameplayCueNotifyCapability::BuildDefinition(FNexusCapabilityDefinition& Out) const
{
	Out.Name = TEXT("get_asset_gameplay_cue_notify");
	Out.SearchAssetTypes = {TEXT("GameplayCueNotify_Static")};
	Out.Description = TEXT("读取 GameplayCueNotify：CueName / 类名 / 是否蓝图。");
	Out.InputSchema = FNexusSchema::Object()
		.Prop(TEXT("assetPath"), FNexusSchema::Str(TEXT("Cue Notify 资产路径")))
		.Required({ TEXT("assetPath") })
		.Build();
	Out.Tags = { FNexusMcpTags::Readonly, FNexusMcpTags::Gas };
	Out.ExtraSearchKeywords = { TEXT("cue"), TEXT("notify"), TEXT("gc") };
	Out.RelatedCapabilities = {
		TEXT("manage_asset_gameplay_cue_notify"), TEXT("create_asset_gameplay_cue_notify"),
		TEXT("manage_asset_blueprint")
	};
}

FCapabilityResult FGetAssetGameplayCueNotifyCapability::Execute(const TSharedPtr<FJsonObject>& Arguments) const
{
	return FNexusCapabilityResultBuilder::Build([&](auto& OutEntries, auto& OutTop, auto& OutError)
	{
		FString AssetPath;
		if (!FNexusCapability::RequireString(Arguments, TEXT("assetPath"), AssetPath, OutEntries, {})) return;

		UObject* Loaded = FNexusAssetUtils::LoadAssetWithFallback<UObject>(AssetPath);
		if (!Loaded)
		{
			FNexusCapability::EmitError(OutEntries, {{TEXT("path"), AssetPath}},
				FString::Printf(TEXT("加载失败: %s"), *AssetPath));
			return;
		}

		FGameplayTag CueTag;
		FString ClassName;
		bool bIsBlueprint = false;

		if (UGameplayCueNotify_Static* StaticNotify = Cast<UGameplayCueNotify_Static>(Loaded))
		{
			CueTag = StaticNotify->GameplayCueName;
			ClassName = StaticNotify->GetClass()->GetName();
		}
		else if (UBlueprint* BP = Cast<UBlueprint>(Loaded))
		{
			bIsBlueprint = true;
			ClassName = BP->ParentClass ? BP->ParentClass->GetName() : TEXT("Blueprint");
			UClass* Gen = BP->GeneratedClass;
			if (Gen)
			{
				if (UGameplayCueNotify_Static* CDO = Cast<UGameplayCueNotify_Static>(Gen->GetDefaultObject()))
				{
					CueTag = CDO->GameplayCueName;
				}
				else if (AGameplayCueNotify_Actor* ActorCDO = Cast<AGameplayCueNotify_Actor>(Gen->GetDefaultObject()))
				{
					CueTag = ActorCDO->GameplayCueName;
				}
			}
		}
		else
		{
			FNexusCapability::EmitError(OutEntries, {{TEXT("path"), AssetPath}}, TEXT("不是 GameplayCueNotify"));
			return;
		}

		TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
		Entry->SetStringField(TEXT("name"), Loaded->GetName());
		Entry->SetStringField(TEXT("path"), Loaded->GetPathName());
		Entry->SetStringField(TEXT("className"), ClassName);
		Entry->SetBoolField(TEXT("isBlueprint"), bIsBlueprint);
		Entry->SetStringField(TEXT("cueName"), CueTag.ToString());
		OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
	});
}

REGISTER_MCP_CAPABILITY(FGetAssetGameplayCueNotifyCapability)

#endif
