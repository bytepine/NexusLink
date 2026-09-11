// Copyright byteyang. All Rights Reserved.

#include "Capabilities/Asset/Niagara/NexusCreateAssetNiagaraSystemCapability.h"

#if WITH_NIAGARA

#include "NexusCapabilityRegistry.h"
#include "NexusMcpSchemaBuilder.h"
#include "Utils/NexusAssetUtils.h"
#include "Utils/NexusCapabilityResultBuilder.h"
#include "Utils/NexusArgs.h"
#include "NiagaraSystem.h"
#if WITH_EDITOR
#include "Factories/Factory.h"
#include "Modules/ModuleManager.h"
#endif
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
#if !WITH_EDITOR
		OutError = TEXT("create_asset_niagara_system only available in editor builds");
		return;
#else
		if (FNexusAssetUtils::PackageExists(AssetPath))
		{
			FNexusCapabilityResultBuilder::AddEntryError(
				OutEntries, FString::Printf(TEXT("NiagaraSystem already exists: %s"), *AssetPath));
			return;
		}

		// 必须走工厂：裸 NewObject 的 System 缺 SystemSpawnScript 等必需图，落盘时引擎断言。
		// 工厂类按名反射取，避免直接引用 UNiagaraSystemFactoryNew——它的 InitializeSystem
		// 在 UE ≤5.1 没有 NIAGARAEDITOR_API 导出，硬链会失败。
#if NX_UE_HAS_FIND_FIRST_OBJECT
		UClass* FactoryClass = FindFirstObject<UClass>(TEXT("NiagaraSystemFactoryNew"), EFindFirstObjectOptions::NativeFirst);
#else
		UClass* FactoryClass = FindObject<UClass>(ANY_PACKAGE, TEXT("NiagaraSystemFactoryNew"));
#endif
		if (!FactoryClass)
		{
			FNexusCapabilityResultBuilder::AddEntryError(OutEntries, TEXT("NiagaraSystemFactoryNew not found"));
			return;
		}

		FText PackageNameError;
		if (!FPackageName::IsValidLongPackageName(AssetPath, false, &PackageNameError))
		{
			FNexusCapabilityResultBuilder::AddEntryError(OutEntries,
				FString::Printf(TEXT("Invalid package path '%s': %s"), *AssetPath, *PackageNameError.ToString()));
			return;
		}
		UPackage* Package = CreatePackage(*AssetPath);
		if (!Package)
		{
			FNexusCapabilityResultBuilder::AddEntryError(OutEntries,
				FString::Printf(TEXT("Failed to create package: %s"), *AssetPath));
			return;
		}

		// 直接调 FactoryCreateNew，不走 IAssetTools::CreateAsset：后者会先调
		// ConfigureProperties()，UE 5.7 的 Niagara 工厂在那里弹 Slate 向导窗，headless 下会挂。
		UFactory* Factory = NewObject<UFactory>(GetTransientPackage(), FactoryClass);
		UObject* NewAsset = Factory->FactoryCreateNew(
			UNiagaraSystem::StaticClass(), Package, *FPackageName::GetShortName(AssetPath),
			RF_Public | RF_Standalone, nullptr, GWarn);
		UNiagaraSystem* Sys = Cast<UNiagaraSystem>(NewAsset);
		if (!Sys)
		{
			FNexusCapabilityResultBuilder::AddEntryError(
				OutEntries, FString::Printf(TEXT("Failed to create NiagaraSystem: %s"), *AssetPath));
			return;
		}
		FNexusAssetUtils::NotifyAndSaveCreated(Package, Sys, AssetPath);
		TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
		Entry->SetStringField(TEXT("name"), Sys->GetName());
		Entry->SetStringField(TEXT("path"), Sys->GetPathName());
		OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
#endif
	});
}

REGISTER_MCP_CAPABILITY(FCreateAssetNiagaraSystemCapability)

#endif
