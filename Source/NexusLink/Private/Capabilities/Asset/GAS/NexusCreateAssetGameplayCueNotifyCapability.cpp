// Copyright byteyang. All Rights Reserved.

#include "Capabilities/Asset/GAS/NexusCreateAssetGameplayCueNotifyCapability.h"

#if WITH_GAS

#include "NexusCapabilityRegistry.h"
#include "NexusMcpSchemaBuilder.h"
#include "Utils/NexusAssetUtils.h"
#include "Utils/NexusCapabilityResultBuilder.h"
#include "GameplayCueNotify_Static.h"
#include "GameplayTagContainer.h"
#include "NexusMcpTool.h"

void FCreateAssetGameplayCueNotifyCapability::BuildDefinition(FNexusCapabilityDefinition& Out) const
{
	Out.Name = TEXT("create_asset_gameplay_cue_notify");
	Out.Description = TEXT("创建 GameplayCueNotify_Static。kind=actor 时请改用 create_asset_blueprint。");
	Out.InputSchema = FNexusSchema::Object()
		.Prop(TEXT("assetPath"), FNexusSchema::Str(TEXT("资产包路径")))
		.Prop(TEXT("kind"), FNexusSchema::Enum(TEXT("类型"), { TEXT("static"), TEXT("actor") }, TEXT("static")))
		.Prop(TEXT("cueName"), FNexusSchema::Str(TEXT("GameplayCue Tag（可选）")))
		.Required({ TEXT("assetPath") })
		.Build();
	Out.Tags = { FNexusMcpTags::Write, FNexusMcpTags::Gas };
	Out.ExtraSearchKeywords = { TEXT("cue"), TEXT("notify"), TEXT("gc"), TEXT("fx") };
	Out.RelatedCapabilities = {
		TEXT("get_asset_gameplay_cue_notify"), TEXT("manage_asset_gameplay_cue_notify"),
		TEXT("create_asset_blueprint")
	};
	Out.WhenToUse = TEXT("新建 Static Cue Notify；Actor 蓝图走 create_asset_blueprint");
}

FCapabilityResult FCreateAssetGameplayCueNotifyCapability::Execute(const TSharedPtr<FJsonObject>& Arguments) const
{
	return FNexusCapabilityResultBuilder::Build([&](auto& OutEntries, auto& OutTop, auto& OutError)
	{
		if (!Arguments.IsValid() || !Arguments->HasField(TEXT("assetPath")))
		{
			OutError = TEXT("缺少 assetPath");
			return;
		}
		const FString AssetPath = Arguments->GetStringField(TEXT("assetPath"));
		FString Kind = TEXT("static");
		if (Arguments->HasField(TEXT("kind"))) Kind = Arguments->GetStringField(TEXT("kind")).ToLower();
		if (Kind == TEXT("actor"))
		{
			FNexusCapabilityResultBuilder::AddEntryError(OutEntries,
				TEXT("kind=actor 请用 create_asset_blueprint(parentClass=GameplayCueNotify_Actor)"));
			return;
		}

		if (LoadObject<UGameplayCueNotify_Static>(nullptr, *AssetPath))
		{
			FNexusCapabilityResultBuilder::AddEntryError(OutEntries,
				FString::Printf(TEXT("GameplayCueNotify already exists: %s"), *AssetPath));
			return;
		}
		UPackage* Package = CreatePackage(*AssetPath);
		if (!Package) { FNexusCapabilityResultBuilder::AddEntryError(OutEntries, TEXT("创建包失败")); return; }
		const FString AssetName = FPaths::GetBaseFilename(AssetPath);
		UGameplayCueNotify_Static* Notify = NewObject<UGameplayCueNotify_Static>(
			Package, *AssetName, RF_Public | RF_Standalone);
		if (!Notify) { FNexusCapabilityResultBuilder::AddEntryError(OutEntries, TEXT("创建失败")); return; }

		FString CueName;
		if (Arguments->TryGetStringField(TEXT("cueName"), CueName) && !CueName.IsEmpty())
		{
			Notify->GameplayCueName = FGameplayTag::RequestGameplayTag(FName(*CueName), false);
		}
		FNexusAssetUtils::NotifyAndSaveCreated(Package, Notify, AssetPath);
		TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
		Entry->SetStringField(TEXT("name"), Notify->GetName());
		Entry->SetStringField(TEXT("path"), Notify->GetPathName());
		OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
	});
}

REGISTER_MCP_CAPABILITY(FCreateAssetGameplayCueNotifyCapability)

#endif
