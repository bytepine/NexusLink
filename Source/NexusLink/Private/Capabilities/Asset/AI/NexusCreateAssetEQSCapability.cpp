// Copyright byteyang. All Rights Reserved.

#include "Capabilities/Asset/AI/NexusCreateAssetEQSCapability.h"

#if NX_UE_HAS_EQS

#include "Utils/NexusCapabilityResultBuilder.h"
#include "Utils/NexusArgs.h"
#include "NexusCapabilityRegistry.h"
#include "NexusMcpSchemaBuilder.h"
#include "Utils/NexusAssetUtils.h"
#include "NexusMcpTool.h"
#include "EnvironmentQuery/EnvQuery.h"

void FCreateAssetEQSCapability::BuildDefinition(FNexusCapabilityDefinition& Out) const
{
	Out.Name = TEXT("create_asset_eqs");
	Out.Description = TEXT("Create empty UEnvQuery. Add Generator/Test via manage.");
	Out.InputSchema = FNexusSchema::Object()
		.Required(TEXT("assetPath"), FNexusSchema::Str(TEXT("Asset package path (/Game/…/EQ_FindCover)")))
		.Build();
	Out.Tags = { FNexusMcpTags::Write, FNexusMcpTags::Blueprint };
	Out.ExtraSearchKeywords = { TEXT("eqs"), TEXT("query"), TEXT("environment"), TEXT("ai"), TEXT("perception"), TEXT("pathfind") };
	Out.RelatedCapabilities = { TEXT("get_asset_eqs"), TEXT("manage_asset_eqs"), TEXT("create_asset_behavior_tree") };
	Out.WhenToUse = TEXT("Create EQS asset; add Generator/Test via manage_asset_eqs");
}

FCapabilityResult FCreateAssetEQSCapability::Execute(const TSharedPtr<FJsonObject>& Arguments) const
{
	return FNexusCapabilityResultBuilder::Build([&](auto& OutEntries, auto& OutTop, auto& OutError)
	{
		const FNexusArgs A(Arguments);
#if !WITH_EDITOR
		OutError = TEXT("create_asset_eqs only available in editor builds");
		return;
#else
		const FString AssetPath = A.Str(TEXT("assetPath"));
		const FNexusAssetUtils::FAssetCreateOutcome Created =
			FNexusAssetUtils::CreatePlainAsset<UEnvQuery>(AssetPath);
		if (!Created.Ok())
		{
			FNexusCapabilityResultBuilder::AddEntryError(OutEntries, Created.Error);
			return;
		}
		UEnvQuery* EQ = Cast<UEnvQuery>(Created.Asset);
		TSharedPtr<FJsonObject> OutEntry = MakeShared<FJsonObject>();
		OutEntry->SetStringField(TEXT("assetType"), TEXT("EnvQuery"));
		OutEntry->SetStringField(TEXT("name"),    EQ->GetName());
		OutEntry->SetStringField(TEXT("path"),    FNexusAssetUtils::PackagePathOf(EQ));
		OutEntries.Add(MakeShared<FJsonValueObject>(OutEntry));
#endif
	});
}

REGISTER_MCP_CAPABILITY(FCreateAssetEQSCapability)

#endif // NX_UE_HAS_EQS
