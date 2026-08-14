// Copyright byteyang. All Rights Reserved.

#include "Capabilities/Asset/Niagara/NexusCreateAssetNiagaraSystemCapability.h"

#if WITH_NIAGARA

#include "NexusCapabilityRegistry.h"
#include "NexusMcpSchemaBuilder.h"
#include "Utils/NexusAssetUtils.h"
#include "Utils/NexusCapabilityResultBuilder.h"
#include "NiagaraSystem.h"
#include "NexusMcpTool.h"

void FCreateAssetNiagaraSystemCapability::BuildDefinition(FNexusCapabilityDefinition& Out) const
{
	Out.Name = TEXT("create_asset_niagara_system");
	Out.Description = TEXT("创建空白 NiagaraSystem。模块栈用 manage add_emitter/add_module。");
	Out.InputSchema = FNexusSchema::Object()
		.Prop(TEXT("assetPath"), FNexusSchema::Str(TEXT("资产包路径")))
		.Required({ TEXT("assetPath") })
		.Build();
	Out.Tags = { FNexusMcpTags::Write, FNexusMcpTags::Editor };
	Out.ExtraSearchKeywords = { TEXT("niagara"), TEXT("vfx"), TEXT("particle") };
	Out.RelatedCapabilities = { TEXT("get_asset_niagara_system"), TEXT("manage_asset_niagara_system") };
	Out.WhenToUse = TEXT("新建 Niagara 系统；发射器/模块用 manage");
}

FCapabilityResult FCreateAssetNiagaraSystemCapability::Execute(const TSharedPtr<FJsonObject>& Arguments) const
{
	return FNexusCapabilityResultBuilder::Build([&](auto& OutEntries, auto& OutTop, auto& OutError)
	{
		if (!Arguments.IsValid() || !Arguments->HasField(TEXT("assetPath")))
		{
			OutError = TEXT("缺少 assetPath");
			return;
		}
		const FString AssetPath = Arguments->GetStringField(TEXT("assetPath"));
		if (LoadObject<UNiagaraSystem>(nullptr, *AssetPath))
		{
			FNexusCapabilityResultBuilder::AddEntryError(OutEntries,
				FString::Printf(TEXT("NiagaraSystem already exists: %s"), *AssetPath));
			return;
		}
		UPackage* Package = CreatePackage(*AssetPath);
		if (!Package) { FNexusCapabilityResultBuilder::AddEntryError(OutEntries, TEXT("创建包失败")); return; }
		const FString AssetName = FPaths::GetBaseFilename(AssetPath);
		UNiagaraSystem* Sys = NewObject<UNiagaraSystem>(Package, *AssetName, RF_Public | RF_Standalone);
		if (!Sys) { FNexusCapabilityResultBuilder::AddEntryError(OutEntries, TEXT("创建失败")); return; }
		FNexusAssetUtils::NotifyAndSaveCreated(Package, Sys, AssetPath);
		TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
		Entry->SetStringField(TEXT("name"), Sys->GetName());
		Entry->SetStringField(TEXT("path"), Sys->GetPathName());
		OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
	});
}

REGISTER_MCP_CAPABILITY(FCreateAssetNiagaraSystemCapability)

#endif
