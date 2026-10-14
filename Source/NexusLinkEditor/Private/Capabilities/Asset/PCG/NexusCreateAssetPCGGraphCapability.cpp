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

void FNexusCreateAssetPCGGraphCapability::BuildDefinition(FNexusCapabilityDefinition& Out) const
{
	Out.Name        = TEXT("CreateAssetPCGGraph");
	Out.Description = TEXT("Create PCG Graph asset.");
	Out.InputSchema = FNexusSchema::Object()
		.Prop(TEXT("AssetPath"), FNexusSchema::Str(TEXT("New PCG Graph full path, e.g. /Game/PCG/PCG_NewGraph")))
		.Required({ TEXT("AssetPath") })
		.Build();
	Out.Tags = { FNexusMcpTags::Write, FNexusMcpTags::Editor };
	Out.ExtraSearchKeywords = { TEXT("pcg"), TEXT("procedural"), TEXT("generation"), TEXT("graph") };
	Out.RelatedCapabilities = { TEXT("GetAssetPCGGraph"), TEXT("ManageAssetPCGGraph"), TEXT("SearchAsset") };
	Out.WhenToUse = TEXT("Create new PCG Graph asset (UE 5.4+)");
}

FCapabilityResult FNexusCreateAssetPCGGraphCapability::Execute(const TSharedPtr<FJsonObject>& Arguments) const
{
	return FNexusCapabilityResultBuilder::Build([&](auto& OutEntries, auto& OutTop, auto& OutError)
	{
		const FNexusArgs A(Arguments);
		const FString FullPath = A.Str(TEXT("AssetPath"));

		if (UPCGGraph* Existing = FNexusAssetUtils::LoadAssetWithFallback<UPCGGraph>(FullPath))
		{
			TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
			Entry->SetStringField(TEXT("Path"), Existing->GetPathName());
			Entry->SetBoolField(TEXT("bAlreadyExists"), true);
			OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
			return;
		}

		const FNexusAssetUtils::FAssetCreateOutcome Created =
			FNexusAssetUtils::CreatePlainAsset<UPCGGraph>(FullPath);
		if (!Created.Ok())
		{
			FNexusCapability::EmitError(OutEntries, {{TEXT("Path"), FullPath}}, Created.Error);
			return;
		}
		UPCGGraph* Graph = Cast<UPCGGraph>(Created.Asset);

		TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
		Entry->SetStringField(TEXT("Path"), Graph->GetPathName());
		Entry->SetStringField(TEXT("AssetType"), TEXT("PCGGraph"));
		Entry->SetBoolField(TEXT("bCreated"), true);
		OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
	});
}

REGISTER_MCP_CAPABILITY(FNexusCreateAssetPCGGraphCapability)

#endif // WITH_PCG
