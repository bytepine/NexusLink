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

void FNexusCreateAssetGameplayCueNotifyCapability::BuildDefinition(FNexusCapabilityDefinition& Out) const
{
	Out.Name = TEXT("CreateAssetGameplayCueNotify");
	Out.Description = TEXT("Create GameplayCueNotify_Static. Use CreateAssetBlueprint for Kind=actor.");
	Out.InputSchema = FNexusSchema::Object()
		.Prop(TEXT("AssetPath"), FNexusSchema::Str(TEXT("Asset package path")))
		.Prop(TEXT("Kind"), FNexusSchema::Enum(TEXT("Kind"), { TEXT("Static"), TEXT("Actor") }, TEXT("Static")))
		.Prop(TEXT("CueName"), FNexusSchema::Str(TEXT("GameplayCue Tag (optional)")))
		.Required({ TEXT("AssetPath") })
		.Build();
	Out.Tags = { FNexusMcpTags::Write, FNexusMcpTags::Gas };
	Out.ExtraSearchKeywords = { TEXT("cue"), TEXT("notify"), TEXT("gc"), TEXT("fx") };
	Out.RelatedCapabilities = {
		TEXT("GetAssetGameplayCueNotify"), TEXT("ManageAssetGameplayCueNotify"),
		TEXT("CreateAssetBlueprint")
	};
	Out.WhenToUse = TEXT("Create Static Cue Notify; Actor BP via CreateAssetBlueprint");
}

FCapabilityResult FNexusCreateAssetGameplayCueNotifyCapability::Execute(const TSharedPtr<FJsonObject>& Arguments) const
{
	return FNexusCapabilityResultBuilder::Build([&](auto& OutEntries, auto& OutTop, auto& OutError)
	{
		const FNexusArgs A(Arguments);
		const FString AssetPath = A.Str(TEXT("AssetPath"));
		FString Kind = TEXT("Static");
		if (Arguments->HasField(TEXT("Kind"))) Kind = A.Str(TEXT("Kind")).ToLower();
		if (Kind == TEXT("Actor"))
		{
			FNexusCapabilityResultBuilder::AddEntryError(OutEntries,
				TEXT("For Kind=actor use CreateAssetBlueprint(ParentClass=GameplayCueNotify_Actor)"));
			return;
		}

		const FNexusAssetUtils::FAssetCreateOutcome Created =
			FNexusAssetUtils::CreatePlainAsset<UGameplayCueNotify_Static>(AssetPath, RF_Public | RF_Standalone, false);
		if (!Created.Ok())
		{
			FNexusCapabilityResultBuilder::AddEntryError(OutEntries, Created.Error);
			return;
		}
		UGameplayCueNotify_Static* Notify = Cast<UGameplayCueNotify_Static>(Created.Asset);

		FString CueName;
		if (Arguments->TryGetStringField(TEXT("CueName"), CueName) && !CueName.IsEmpty())
		{
			Notify->GameplayCueName = FName(*CueName);
		}
		FNexusAssetUtils::NotifyAndSaveCreated(Notify->GetOutermost(), Notify, AssetPath);
		FNexusCapabilityResultBuilder::AddCreatedEntry(OutEntries, Notify);
	});
}

REGISTER_MCP_CAPABILITY(FNexusCreateAssetGameplayCueNotifyCapability)

#endif
