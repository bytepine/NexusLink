// Copyright byteyang. All Rights Reserved.

#include "Capabilities/Asset/MetaSound/NexusCreateAssetMetaSoundPatchCapability.h"

#if WITH_METASOUND

#include "Utils/NexusVersionCompat.h"

#if NX_UE_HAS_METASOUND_PATCH

#include "Utils/NexusCapabilityResultBuilder.h"
#include "Utils/NexusArgs.h"
#include "NexusCapabilityRegistry.h"
#include "NexusMcpSchemaBuilder.h"
#include "Utils/NexusAssetUtils.h"
#include "NexusMcpTool.h"
#include "Metasound.h"

void FCreateAssetMetaSoundPatchCapability::BuildDefinition(FNexusCapabilityDefinition& Out) const
{
	Out.Name        = TEXT("create_asset_meta_sound_patch");
	Out.Description = TEXT("Create MetaSound Patch (reusable subgraph, ≥UE5.1). Reads via get_asset_meta_sound.");
	Out.InputSchema = FNexusSchema::Object()
		.Prop(TEXT("assetPath"), FNexusSchema::Str(TEXT("New MetaSound Patch full path, e.g. /Game/Audio/MSP_NewPatch")))
		.Required({ TEXT("assetPath") })
		.Build();
	Out.Tags = { FNexusMcpTags::Write, FNexusMcpTags::Editor };
	Out.ExtraSearchKeywords = { TEXT("metasound"), TEXT("patch"), TEXT("audio"), TEXT("subgraph") };
	Out.RelatedCapabilities = { TEXT("get_asset_meta_sound"), TEXT("manage_asset_meta_sound"), TEXT("search_asset") };
	Out.WhenToUse = TEXT("Create reusable MetaSound Patch subgraph (≥UE5.1)");
}

FCapabilityResult FCreateAssetMetaSoundPatchCapability::Execute(const TSharedPtr<FJsonObject>& Arguments) const
{
	return FNexusCapabilityResultBuilder::Build([&](auto& OutEntries, auto& OutTop, auto& OutError)
	{
		const FNexusArgs A(Arguments);
		const FString FullPath = A.Str(TEXT("assetPath"));

		if (FNexusAssetUtils::LoadAssetWithFallback<UMetaSoundPatch>(FullPath))
		{
			TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
			Entry->SetStringField(TEXT("path"),    FullPath);
			Entry->SetStringField(TEXT("assetType"),    TEXT("MetaSoundPatch"));
			Entry->SetBoolField(TEXT("alreadyExists"),  true);
			OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
			return;
		}

		const FNexusAssetUtils::FAssetCreateOutcome Created =
			FNexusAssetUtils::CreatePlainAsset<UMetaSoundPatch>(FullPath);
		if (!Created.Ok())
		{
			FNexusCapability::EmitError(OutEntries, {{TEXT("path"), FullPath}}, Created.Error);
			return;
		}
		UMetaSoundPatch* Patch = Cast<UMetaSoundPatch>(Created.Asset);

		TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
		Entry->SetStringField(TEXT("path"), Patch->GetPathName());
		Entry->SetStringField(TEXT("assetType"), TEXT("MetaSoundPatch"));
		Entry->SetBoolField(TEXT("created"),     true);
		OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
	});
}

REGISTER_MCP_CAPABILITY(FCreateAssetMetaSoundPatchCapability)

#else // NX_UE_HAS_METASOUND_PATCH

// UE5.0 及更低版本不支持 MetaSoundPatch，提供空实现
void FCreateAssetMetaSoundPatchCapability::BuildDefinition(FNexusCapabilityDefinition& Out) const
{
	Out.Name        = TEXT("create_asset_meta_sound_patch");
	Out.Description = TEXT("(MetaSoundPatch requires UE5.1+ on this engine)");
	Out.InputSchema = FNexusSchema::Object().Build();
	Out.Tags = { FNexusMcpTags::Write, FNexusMcpTags::Editor };
}

FCapabilityResult FCreateAssetMetaSoundPatchCapability::Execute(const TSharedPtr<FJsonObject>&) const
{
	return FNexusCapabilityResultBuilder::Build([](auto& OutEntries, auto&, auto& OutError)
	{
		OutError = TEXT("create_asset_meta_sound_patch requires UE5.1+");
	});
}

REGISTER_MCP_CAPABILITY(FCreateAssetMetaSoundPatchCapability)

#endif // NX_UE_HAS_METASOUND_PATCH

#endif // WITH_METASOUND
