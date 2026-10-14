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

void FNexusCreateAssetMetaSoundCapability::BuildDefinition(FNexusCapabilityDefinition& Out) const
{
	Out.Name        = TEXT("CreateAssetMetaSound");
	Out.Description = TEXT("Create MetaSound Source asset.");
	Out.InputSchema = FNexusSchema::Object()
		.Prop(TEXT("AssetPath"), FNexusSchema::Str(TEXT("New MetaSound asset full path, e.g. /Game/Audio/MS_NewSound")))
		.Required({ TEXT("AssetPath") })
		.Build();
	Out.Tags = { FNexusMcpTags::Write, FNexusMcpTags::Editor };
	Out.ExtraSearchKeywords = { TEXT("metasound"), TEXT("audio"), TEXT("Sound"), TEXT("procedural") };
	Out.RelatedCapabilities = { TEXT("GetAssetMetaSound"), TEXT("ManageAssetMetaSound"), TEXT("SearchAsset") };
	Out.WhenToUse = TEXT("Create new MetaSound Source asset");
}

FCapabilityResult FNexusCreateAssetMetaSoundCapability::Execute(const TSharedPtr<FJsonObject>& Arguments) const
{
	return FNexusCapabilityResultBuilder::Build([&](auto& OutEntries, auto& OutTop, auto& OutError)
	{
		const FNexusArgs A(Arguments);
		const FString FullPath = A.Str(TEXT("AssetPath"));

		if (UMetaSoundSource* Existing = FNexusAssetUtils::LoadAssetWithFallback<UMetaSoundSource>(FullPath))
		{
			TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
			Entry->SetStringField(TEXT("Path"), Existing->GetPathName());
			Entry->SetBoolField(TEXT("bAlreadyExists"), true);
			OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
			return;
		}

		const FNexusAssetUtils::FAssetCreateOutcome Created =
			FNexusAssetUtils::CreatePlainAsset<UMetaSoundSource>(FullPath);
		if (!Created.Ok())
		{
			FNexusCapability::EmitError(OutEntries, {{TEXT("Path"), FullPath}}, Created.Error);
			return;
		}
		UMetaSoundSource* Source = Cast<UMetaSoundSource>(Created.Asset);

		TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
		Entry->SetStringField(TEXT("Path"), Source->GetPathName());
		Entry->SetStringField(TEXT("AssetType"), TEXT("MetaSoundSource"));
		Entry->SetBoolField(TEXT("bCreated"), true);
		OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
	});
}

REGISTER_MCP_CAPABILITY(FNexusCreateAssetMetaSoundCapability)

#endif // WITH_METASOUND
