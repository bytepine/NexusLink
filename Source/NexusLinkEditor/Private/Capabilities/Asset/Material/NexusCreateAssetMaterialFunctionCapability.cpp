// Copyright byteyang. All Rights Reserved.

#include "Capabilities/Asset/Material/NexusCreateAssetMaterialFunctionCapability.h"
#include "Utils/NexusCapabilityResultBuilder.h"
#include "Utils/NexusArgs.h"
#include "NexusCapabilityRegistry.h"
#include "NexusMcpSchemaBuilder.h"
#include "Utils/NexusAssetUtils.h"
#include "NexusMcpTool.h"
#include "Materials/MaterialFunction.h"
#if WITH_EDITOR
#include "Factories/MaterialFunctionFactoryNew.h"
#include "AssetToolsModule.h"
#include "IAssetTools.h"
#endif
#include "Misc/PackageName.h"

void FNexusCreateAssetMaterialFunctionCapability::BuildDefinition(FNexusCapabilityDefinition& Out) const
{
	Out.Name = TEXT("CreateAssetMaterialFunction");
	Out.Description = TEXT("Create empty UMaterialFunction. Optional description and bExposeToLibrary.");
	Out.InputSchema = FNexusSchema::Object()
		.Required(TEXT("AssetPath"), FNexusSchema::Str(TEXT("Asset package path (/Game/…/MF_MyFunc)")))
		.Prop(TEXT("description"),     FNexusSchema::Str(TEXT("Function description (optional)")))
		.Prop(TEXT("bExposeToLibrary"), FNexusSchema::Bool(TEXT("Show in material function library")))
		.Build();
	Out.Tags = { FNexusMcpTags::Write, FNexusMcpTags::Material };
	Out.ExtraSearchKeywords = { TEXT("mf"), TEXT("Function"), TEXT("Material"), TEXT("shader"), TEXT("reuse") };
	Out.RelatedCapabilities = { TEXT("GetAssetMaterial"), TEXT("ManageAssetMaterial"), TEXT("CreateAssetMaterial") };
	Out.WhenToUse = TEXT("Create MaterialFunction; add nodes via ManageAssetMaterial");
}

FCapabilityResult FNexusCreateAssetMaterialFunctionCapability::Execute(const TSharedPtr<FJsonObject>& Arguments) const
{
	return FNexusCapabilityResultBuilder::Build([&](auto& OutEntries, auto& OutTop, auto& OutError)
	{
		const FNexusArgs A(Arguments);
#if !WITH_EDITOR
		OutError = TEXT("CreateAssetMaterialFunction only available in editor builds");
		return;
#else
		TSharedPtr<FJsonObject> OutEntry = MakeShared<FJsonObject>();

		const FString AssetPath = A.Str(TEXT("AssetPath"));

		if (FPackageName::DoesPackageExist(AssetPath))
		{
			OutEntry->SetStringField(TEXT("Error"), FString::Printf(TEXT("MaterialFunction already exists: %s"), *AssetPath));
			OutEntries.Add(MakeShared<FJsonValueObject>(OutEntry));
			return;
		}

		const FString PackagePath = FPackageName::GetLongPackagePath(AssetPath);
		const FString AssetName   = FPackageName::GetShortName(AssetPath);
		IAssetTools& AT = FModuleManager::LoadModuleChecked<FAssetToolsModule>(TEXT("AssetTools")).Get();

		UMaterialFunctionFactoryNew* Factory = NewObject<UMaterialFunctionFactoryNew>();
		UObject* NewAsset = AT.CreateAsset(AssetName, PackagePath, UMaterialFunction::StaticClass(), Factory);
		if (!NewAsset)
		{
			FNexusCapabilityResultBuilder::AddEntryError(OutEntries, FString::Printf(TEXT("MaterialFunction Createfailed: %s"), *AssetPath));
			return;
		}

		UMaterialFunction* MF = Cast<UMaterialFunction>(NewAsset);
		bool bDirty = false;

		FString Desc;
		if (Arguments->TryGetStringField(TEXT("description"), Desc) && !Desc.IsEmpty())
		{
			MF->Description = Desc;
			bDirty = true;
		}

		bool bExpose;
		if (Arguments->TryGetBoolField(TEXT("bExposeToLibrary"), bExpose))
		{
			MF->bExposeToLibrary = bExpose;
			bDirty = true;
		}

		if (bDirty)
		{
			MF->MarkPackageDirty();
		}

		OutEntry->SetStringField(TEXT("AssetType"), TEXT("MaterialFunction"));
		OutEntry->SetStringField(TEXT("name"),    MF->GetName());
		OutEntry->SetStringField(TEXT("Path"),    FNexusAssetUtils::PackagePathOf(MF));
		OutEntries.Add(MakeShared<FJsonValueObject>(OutEntry));
#endif
	});
}

REGISTER_MCP_CAPABILITY(FNexusCreateAssetMaterialFunctionCapability)
