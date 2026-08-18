// Copyright byteyang. All Rights Reserved.

#include "Capabilities/Asset/PCG/NexusCreateAssetPCGGraphCapability.h"

#if WITH_PCG

#include "Utils/NexusCapabilityResultBuilder.h"
#include "Utils/NexusArgs.h"
#include "NexusCapabilityRegistry.h"
#include "NexusMcpSchemaBuilder.h"
#include "Utils/NexusAssetUtils.h"
#include "NexusMcpTool.h"
#include "PCGGraph.h"

void FCreateAssetPCGGraphCapability::BuildDefinition(FNexusCapabilityDefinition& Out) const
{
	Out.Name        = TEXT("create_asset_pcg_graph");
	Out.Description = TEXT("Create PCG Graph asset.; use get_asset_ for readspcg_graph.");
	Out.InputSchema = FNexusSchema::Object()
		.Prop(TEXT("assetPath"), FNexusSchema::Str(TEXT("New PCG Graph full path, e.g. /Game/PCG/PCG_NewGraph")))
		.Required({ TEXT("assetPath") })
		.Build();
	Out.Tags = { FNexusMcpTags::Write, FNexusMcpTags::Editor };
	Out.ExtraSearchKeywords = { TEXT("pcg"), TEXT("procedural"), TEXT("generation"), TEXT("graph") };
	Out.RelatedCapabilities = { TEXT("get_asset_pcg_graph"), TEXT("manage_asset_pcg_graph"), TEXT("search_asset") };
	Out.WhenToUse = TEXT("Create new PCG Graph asset (UE 5.4+)");
}

FCapabilityResult FCreateAssetPCGGraphCapability::Execute(const TSharedPtr<FJsonObject>& Arguments) const
{
	return FNexusCapabilityResultBuilder::Build([&](auto& OutEntries, auto& OutTop, auto& OutError)
	{
		const FNexusArgs A(Arguments);
		const FString FullPath = A.Str(TEXT("assetPath"));

		if (UPCGGraph* Existing = FNexusAssetUtils::LoadAssetWithFallback<UPCGGraph>(FullPath))
		{
			TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
			Entry->SetStringField(TEXT("path"), Existing->GetPathName());
			Entry->SetBoolField(TEXT("alreadyExists"), true);
			OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
			return;
		}

		const FNexusAssetUtils::FAssetCreateOutcome Created =
			FNexusAssetUtils::CreatePlainAsset<UPCGGraph>(FullPath);
		if (!Created.Ok())
		{
			FNexusCapability::EmitError(OutEntries, {{TEXT("path"), FullPath}}, Created.Error);
			return;
		}
		UPCGGraph* Graph = Cast<UPCGGraph>(Created.Asset);

		TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
		Entry->SetStringField(TEXT("path"), Graph->GetPathName());
		Entry->SetStringField(TEXT("assetType"), TEXT("PCGGraph"));
		Entry->SetBoolField(TEXT("created"), true);
		OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
	});
}

REGISTER_MCP_CAPABILITY(FCreateAssetPCGGraphCapability)

#endif // WITH_PCG
