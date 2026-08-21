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

void FCreateAssetFoliageTypeCapability::BuildDefinition(FNexusCapabilityDefinition& Out) const
{
	Out.Name = TEXT("create_asset_foliage_type");
	Out.Description = TEXT("Create FoliageType (InstancedStaticMesh). Optional meshPath. No level painting.");
	Out.InputSchema = FNexusSchema::Object()
		.Prop(TEXT("assetPath"), FNexusSchema::Str(TEXT("Asset package path")))
		.Prop(TEXT("meshPath"), FNexusSchema::Str(TEXT("StaticMesh path (optional)")))
		.Required({ TEXT("assetPath") })
		.Build();
	Out.Tags = { FNexusMcpTags::Write, FNexusMcpTags::Data };
	Out.ExtraSearchKeywords = { TEXT("foliage"), TEXT("grass"), TEXT("instanced"), TEXT("vegetation") };
	Out.RelatedCapabilities = { TEXT("get_asset_foliage_type"), TEXT("manage_asset_foliage_type") };
	Out.WhenToUse = TEXT("Create foliage type asset; level painting via editor");
}

FCapabilityResult FCreateAssetFoliageTypeCapability::Execute(const TSharedPtr<FJsonObject>& Arguments) const
{
	return FNexusCapabilityResultBuilder::Build([&](auto& OutEntries, auto& OutTop, auto& OutError)
	{
		const FNexusArgs A(Arguments);
		const FString AssetPath = A.Str(TEXT("assetPath"));
		const FNexusAssetUtils::FAssetCreateOutcome Created =
			FNexusAssetUtils::CreatePlainAsset<UFoliageType_InstancedStaticMesh>(AssetPath, RF_Public | RF_Standalone, false);
		if (!Created.Ok())
		{
			FNexusCapabilityResultBuilder::AddEntryError(OutEntries, Created.Error);
			return;
		}
		UFoliageType_InstancedStaticMesh* Type = Cast<UFoliageType_InstancedStaticMesh>(Created.Asset);

		FString MeshPath;
		if (Arguments->TryGetStringField(TEXT("meshPath"), MeshPath) && !MeshPath.IsEmpty())
		{
			if (UStaticMesh* Mesh = FNexusAssetUtils::LoadAssetWithFallback<UStaticMesh>(MeshPath))
			{
				Type->Mesh = Mesh;
			}
		}
		FNexusAssetUtils::NotifyAndSaveCreated(Type->GetOutermost(), Type, AssetPath);
		TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
		Entry->SetStringField(TEXT("name"), Type->GetName());
		Entry->SetStringField(TEXT("path"), Type->GetPathName());
		OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
	});
}

REGISTER_MCP_CAPABILITY(FCreateAssetFoliageTypeCapability)
