// Copyright byteyang. All Rights Reserved.

#include "Capabilities/Asset/Niagara/NexusCreateAssetNiagaraSystemCapability.h"

#if WITH_NIAGARA

#include "NexusCapabilityRegistry.h"
#include "NexusMcpSchemaBuilder.h"
#include "Utils/NexusAssetUtils.h"
#include "Utils/NexusCapabilityResultBuilder.h"
#include "Utils/NexusArgs.h"
#include "NiagaraSystem.h"
#include "NexusMcpTool.h"

void FCreateAssetNiagaraSystemCapability::BuildDefinition(FNexusCapabilityDefinition& Out) const
{
	Out.Name = TEXT("create_asset_niagara_system");
	Out.Description = TEXT("Create empty NiagaraSystem. Module stack via manage add_emitter/add_module.");
	Out.InputSchema = FNexusSchema::Object()
		.Prop(TEXT("assetPath"), FNexusSchema::Str(TEXT("Asset package path")))
		.Required({ TEXT("assetPath") })
		.Build();
	Out.Tags = { FNexusMcpTags::Write, FNexusMcpTags::Editor };
	Out.ExtraSearchKeywords = { TEXT("niagara"), TEXT("vfx"), TEXT("particle") };
	Out.RelatedCapabilities = { TEXT("get_asset_niagara_system"), TEXT("manage_asset_niagara_system") };
	Out.WhenToUse = TEXT("Create Niagara system; emitters/modules via manage");
}

FCapabilityResult FCreateAssetNiagaraSystemCapability::Execute(const TSharedPtr<FJsonObject>& Arguments) const
{
	return FNexusCapabilityResultBuilder::Build([&](auto& OutEntries, auto& OutTop, auto& OutError)
	{
		const FNexusArgs A(Arguments);
		const FString AssetPath = A.Str(TEXT("assetPath"));
		const FNexusAssetUtils::FAssetCreateOutcome Created =
			FNexusAssetUtils::CreatePlainAsset<UNiagaraSystem>(AssetPath);
		if (!Created.Ok())
		{
			FNexusCapabilityResultBuilder::AddEntryError(OutEntries, Created.Error);
			return;
		}
		UNiagaraSystem* Sys = Cast<UNiagaraSystem>(Created.Asset);
		TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
		Entry->SetStringField(TEXT("name"), Sys->GetName());
		Entry->SetStringField(TEXT("path"), Sys->GetPathName());
		OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
	});
}

REGISTER_MCP_CAPABILITY(FCreateAssetNiagaraSystemCapability)

#endif
