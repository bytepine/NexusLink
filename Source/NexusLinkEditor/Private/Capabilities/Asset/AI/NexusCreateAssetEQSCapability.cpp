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

void FNexusCreateAssetEQSCapability::BuildDefinition(FNexusCapabilityDefinition& Out) const
{
	Out.Name = TEXT("CreateAssetEQS");
	Out.Description = TEXT("Create empty UEnvQuery. Add Generator/Test via manage.");
	Out.InputSchema = FNexusSchema::Object()
		.Required(TEXT("AssetPath"), FNexusSchema::Str(TEXT("Asset package path (/Game/…/EQ_FindCover)")))
		.Build();
	Out.Tags = { FNexusMcpTags::Write, FNexusMcpTags::Blueprint };
	Out.ExtraSearchKeywords = { TEXT("eqs"), TEXT("Query"), TEXT("environment"), TEXT("ai"), TEXT("perception"), TEXT("pathfind") };
	Out.RelatedCapabilities = { TEXT("GetAssetEQS"), TEXT("ManageAssetEQS"), TEXT("CreateAssetBehaviorTree") };
	Out.WhenToUse = TEXT("Create EQS asset; add Generator/Test via ManageAssetEQS");
}

FCapabilityResult FNexusCreateAssetEQSCapability::Execute(const TSharedPtr<FJsonObject>& Arguments) const
{
	return FNexusCapabilityResultBuilder::Build([&](auto& OutEntries, auto& OutTop, auto& OutError)
	{
		const FNexusArgs A(Arguments);
#if !WITH_EDITOR
		OutError = TEXT("CreateAssetEQS only available in editor builds");
		return;
#else
		const FString AssetPath = A.Str(TEXT("AssetPath"));
		const FNexusAssetUtils::FAssetCreateOutcome Created =
			FNexusAssetUtils::CreatePlainAsset<UEnvQuery>(AssetPath);
		if (!Created.Ok())
		{
			FNexusCapabilityResultBuilder::AddEntryError(OutEntries, Created.Error);
			return;
		}
		UEnvQuery* EQ = Cast<UEnvQuery>(Created.Asset);
		TSharedPtr<FJsonObject> OutEntry = MakeShared<FJsonObject>();
		OutEntry->SetStringField(TEXT("AssetType"), TEXT("EnvQuery"));
		OutEntry->SetStringField(TEXT("name"),    EQ->GetName());
		OutEntry->SetStringField(TEXT("Path"),    FNexusAssetUtils::PackagePathOf(EQ));
		OutEntries.Add(MakeShared<FJsonValueObject>(OutEntry));
#endif
	});
}

REGISTER_MCP_CAPABILITY(FNexusCreateAssetEQSCapability)

#endif // NX_UE_HAS_EQS
