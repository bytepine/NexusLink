// Copyright byteyang. All Rights Reserved.

#include "Capabilities/Asset/Foliage/NexusCreateAssetFoliageTypeCapability.h"
#include "NexusCapabilityRegistry.h"
#include "NexusMcpSchemaBuilder.h"
#include "Utils/NexusAssetUtils.h"
#include "Utils/NexusCapabilityResultBuilder.h"
#include "Utils/NexusArgs.h"
#include "FoliageType_InstancedStaticMesh.h"
#include "Engine/StaticMesh.h"
#include "NexusMcpTool.h"

void FNexusCreateAssetFoliageTypeCapability::BuildDefinition(FNexusCapabilityDefinition& Out) const
{
	Out.Name = TEXT("CreateAssetFoliageType");
	Out.Description = TEXT("Create FoliageType (InstancedStaticMesh). Optional MeshPath. No level painting.");
	Out.InputSchema = FNexusSchema::Object()
		.Prop(TEXT("AssetPath"), FNexusSchema::Str(TEXT("Asset package path")))
		.Prop(TEXT("MeshPath"), FNexusSchema::Str(TEXT("StaticMesh path (optional)")))
		.Required({ TEXT("AssetPath") })
		.Build();
	Out.Tags = { FNexusMcpTags::Write, FNexusMcpTags::Data };
	Out.ExtraSearchKeywords = { TEXT("foliage"), TEXT("grass"), TEXT("instanced"), TEXT("vegetation") };
	Out.RelatedCapabilities = { TEXT("GetAssetFoliageType"), TEXT("ManageAssetFoliageType") };
	Out.WhenToUse = TEXT("Create foliage type asset; level painting via editor");
}

FCapabilityResult FNexusCreateAssetFoliageTypeCapability::Execute(const TSharedPtr<FJsonObject>& Arguments) const
{
	return FNexusCapabilityResultBuilder::Build([&](auto& OutEntries, auto& OutTop, auto& OutError)
	{
		const FNexusArgs A(Arguments);
		const FString AssetPath = A.Str(TEXT("AssetPath"));
		const FNexusAssetUtils::FAssetCreateOutcome Created =
			FNexusAssetUtils::CreatePlainAsset<UFoliageType_InstancedStaticMesh>(AssetPath, RF_Public | RF_Standalone, false);
		if (!Created.Ok())
		{
			FNexusCapabilityResultBuilder::AddEntryError(OutEntries, Created.Error);
			return;
		}
		UFoliageType_InstancedStaticMesh* Type = Cast<UFoliageType_InstancedStaticMesh>(Created.Asset);

		FString MeshPath;
		if (Arguments->TryGetStringField(TEXT("MeshPath"), MeshPath) && !MeshPath.IsEmpty())
		{
			if (UStaticMesh* Mesh = FNexusAssetUtils::LoadAssetWithFallback<UStaticMesh>(MeshPath))
			{
				Type->Mesh = Mesh;
			}
		}
		FNexusAssetUtils::NotifyAndSaveCreated(Type->GetOutermost(), Type, AssetPath);
		FNexusCapabilityResultBuilder::AddCreatedEntry(OutEntries, Type);
	});
}

REGISTER_MCP_CAPABILITY(FNexusCreateAssetFoliageTypeCapability)
