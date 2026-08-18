// Copyright byteyang. All Rights Reserved.

#include "Capabilities/Asset/GAS/NexusCreateAssetGameplayCueNotifyCapability.h"

#if WITH_GAS

#include "NexusCapabilityRegistry.h"
#include "NexusMcpSchemaBuilder.h"
#include "Utils/NexusAssetUtils.h"
#include "Utils/NexusCapabilityResultBuilder.h"
#include "Utils/NexusArgs.h"
#include "GameplayCueNotify_Static.h"
#include "NexusMcpTool.h"

void FCreateAssetGameplayCueNotifyCapability::BuildDefinition(FNexusCapabilityDefinition& Out) const
{
	Out.Name = TEXT("create_asset_gameplay_cue_notify");
	Out.Description = TEXT("Create GameplayCueNotify_Static. Use create_asset_blueprint for kind=actor.");
	Out.InputSchema = FNexusSchema::Object()
		.Prop(TEXT("assetPath"), FNexusSchema::Str(TEXT("Asset package path")))
		.Prop(TEXT("kind"), FNexusSchema::Enum(TEXT("Kind"), { TEXT("static"), TEXT("actor") }, TEXT("static")))
		.Prop(TEXT("cueName"), FNexusSchema::Str(TEXT("GameplayCue Tag (optional)")))
		.Required({ TEXT("assetPath") })
		.Build();
	Out.Tags = { FNexusMcpTags::Write, FNexusMcpTags::Gas };
	Out.ExtraSearchKeywords = { TEXT("cue"), TEXT("notify"), TEXT("gc"), TEXT("fx") };
	Out.RelatedCapabilities = {
		TEXT("get_asset_gameplay_cue_notify"), TEXT("manage_asset_gameplay_cue_notify"),
		TEXT("create_asset_blueprint")
	};
	Out.WhenToUse = TEXT("Create Static Cue Notify; Actor BP via create_asset_blueprint");
}

FCapabilityResult FCreateAssetGameplayCueNotifyCapability::Execute(const TSharedPtr<FJsonObject>& Arguments) const
{
	return FNexusCapabilityResultBuilder::Build([&](auto& OutEntries, auto& OutTop, auto& OutError)
	{
		const FNexusArgs A(Arguments);
		const FString AssetPath = A.Str(TEXT("assetPath"));
		FString Kind = TEXT("static");
		if (Arguments->HasField(TEXT("kind"))) Kind = A.Str(TEXT("kind")).ToLower();
		if (Kind == TEXT("actor"))
		{
			FNexusCapabilityResultBuilder::AddEntryError(OutEntries,
				TEXT("For kind=actor use create_asset_blueprint(parentClass=GameplayCueNotify_Actor)"));
			return;
		}

		if (LoadObject<UGameplayCueNotify_Static>(nullptr, *AssetPath))
		{
			FNexusCapabilityResultBuilder::AddEntryError(OutEntries,
				FString::Printf(TEXT("GameplayCueNotify already exists: %s"), *AssetPath));
			return;
		}
		UPackage* Package = CreatePackage(*AssetPath);
		if (!Package) { FNexusCapabilityResultBuilder::AddEntryError(OutEntries, TEXT("Failed to create package")); return; }
		const FString AssetName = FPaths::GetBaseFilename(AssetPath);
		UGameplayCueNotify_Static* Notify = NewObject<UGameplayCueNotify_Static>(
			Package, *AssetName, RF_Public | RF_Standalone);
		if (!Notify) { FNexusCapabilityResultBuilder::AddEntryError(OutEntries, TEXT("Creation failed")); return; }

		FString CueName;
		if (Arguments->TryGetStringField(TEXT("cueName"), CueName) && !CueName.IsEmpty())
		{
			Notify->GameplayCueName = FName(*CueName);
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
