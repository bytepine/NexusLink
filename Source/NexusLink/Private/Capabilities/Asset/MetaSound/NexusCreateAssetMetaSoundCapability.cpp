// Copyright byteyang. All Rights Reserved.

#include "Capabilities/Asset/MetaSound/NexusCreateAssetMetaSoundCapability.h"

#if WITH_METASOUND

#include "Utils/NexusCapabilityResultBuilder.h"
#include "Utils/NexusArgs.h"
#include "NexusCapabilityRegistry.h"
#include "NexusMcpSchemaBuilder.h"
#include "Utils/NexusAssetUtils.h"
#include "NexusMcpTool.h"
#include "MetasoundSource.h"

void FCreateAssetMetaSoundCapability::BuildDefinition(FNexusCapabilityDefinition& Out) const
{
	Out.Name        = TEXT("create_asset_meta_sound");
	Out.Description = TEXT("Create MetaSound Source asset.; use get_asset_ for readsmeta_sound.");
	Out.InputSchema = FNexusSchema::Object()
		.Prop(TEXT("assetPath"), FNexusSchema::Str(TEXT("New MetaSound asset full path, e.g. /Game/Audio/MS_NewSound")))
		.Required({ TEXT("assetPath") })
		.Build();
	Out.Tags = { FNexusMcpTags::Write, FNexusMcpTags::Editor };
	Out.ExtraSearchKeywords = { TEXT("metasound"), TEXT("audio"), TEXT("sound"), TEXT("procedural") };
	Out.RelatedCapabilities = { TEXT("get_asset_meta_sound"), TEXT("manage_asset_meta_sound"), TEXT("search_asset") };
	Out.WhenToUse = TEXT("Create new MetaSound Source asset");
}

FCapabilityResult FCreateAssetMetaSoundCapability::Execute(const TSharedPtr<FJsonObject>& Arguments) const
{
	return FNexusCapabilityResultBuilder::Build([&](auto& OutEntries, auto& OutTop, auto& OutError)
	{
		const FNexusArgs A(Arguments);
		const FString FullPath = A.Str(TEXT("assetPath"));

		if (UMetaSoundSource* Existing = FNexusAssetUtils::LoadAssetWithFallback<UMetaSoundSource>(FullPath))
		{
			TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
			Entry->SetStringField(TEXT("path"), Existing->GetPathName());
			Entry->SetBoolField(TEXT("alreadyExists"), true);
			OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
			return;
		}

		const FNexusAssetUtils::FAssetCreateOutcome Created =
			FNexusAssetUtils::CreatePlainAsset<UMetaSoundSource>(FullPath);
		if (!Created.Ok())
		{
			FNexusCapability::EmitError(OutEntries, {{TEXT("path"), FullPath}}, Created.Error);
			return;
		}
		UMetaSoundSource* Source = Cast<UMetaSoundSource>(Created.Asset);

		TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
		Entry->SetStringField(TEXT("path"), Source->GetPathName());
		Entry->SetStringField(TEXT("assetType"), TEXT("MetaSoundSource"));
		Entry->SetBoolField(TEXT("created"), true);
		OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
	});
}

REGISTER_MCP_CAPABILITY(FCreateAssetMetaSoundCapability)

#endif // WITH_METASOUND
