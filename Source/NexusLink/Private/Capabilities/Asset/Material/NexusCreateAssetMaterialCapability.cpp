// Copyright byteyang. All Rights Reserved.
#include "Capabilities/Asset/Material/NexusCreateAssetMaterialCapability.h"
#include "Utils/NexusCapabilityResultBuilder.h"
#include "Utils/NexusArgs.h"
#include "NexusCapabilityRegistry.h"
#include "NexusMcpSchemaBuilder.h"
#include "Utils/NexusMaterialUtils.h"
#include "Utils/NexusAssetUtils.h"
#include "Materials/Material.h"
#if NX_UE_HAS_MATERIAL_DOMAIN_HEADER
#include "MaterialDomain.h"
#endif
#include "Materials/MaterialInstanceConstant.h"
#if WITH_EDITOR
#include "Factories/MaterialFactoryNew.h"
#include "Factories/MaterialInstanceConstantFactoryNew.h"
#include "AssetToolsModule.h"
#include "IAssetTools.h"
#endif
#include "Misc/PackageName.h"
#include "Internationalization/Text.h"
#include "NexusMcpTool.h"


static void AppendScalarVectorTextureParamSummaries(UMaterialInstanceConstant* MI, TArray<TSharedPtr<FJsonValue>>& OutInheritedParams)
{
	TArray<FMaterialParameterInfo> Infos;
	TArray<FGuid> Guids;
	MI->GetAllScalarParameterInfo(Infos, Guids);
	for (const FMaterialParameterInfo& I : Infos)
	{
		TSharedPtr<FJsonObject> P = MakeShared<FJsonObject>();
		P->SetStringField(TEXT("paramName"), I.Name.ToString());
		P->SetStringField(TEXT("paramType"), TEXT("scalar"));
		OutInheritedParams.Add(MakeShared<FJsonValueObject>(P));
	}
	Infos.Reset();
	Guids.Reset();
	MI->GetAllVectorParameterInfo(Infos, Guids);
	for (const FMaterialParameterInfo& I : Infos)
	{
		TSharedPtr<FJsonObject> P = MakeShared<FJsonObject>();
		P->SetStringField(TEXT("paramName"), I.Name.ToString());
		P->SetStringField(TEXT("paramType"), TEXT("vector"));
		OutInheritedParams.Add(MakeShared<FJsonValueObject>(P));
	}
	Infos.Reset();
	Guids.Reset();
	MI->GetAllTextureParameterInfo(Infos, Guids);
	for (const FMaterialParameterInfo& I : Infos)
	{
		TSharedPtr<FJsonObject> P = MakeShared<FJsonObject>();
		P->SetStringField(TEXT("paramName"), I.Name.ToString());
		P->SetStringField(TEXT("paramType"), TEXT("texture"));
		OutInheritedParams.Add(MakeShared<FJsonValueObject>(P));
	}
}

void FCreateAssetMaterialCapability::BuildDefinition(FNexusCapabilityDefinition& Out) const
{
	Out.Name = TEXT("create_asset_material");
	Out.Description = TEXT("Create Material or MaterialInstance. MI requires parentMaterial.");
	Out.InputSchema = FNexusSchema::Object()
		.Prop(TEXT("assetPath"), FNexusSchema::Str(TEXT("New asset full package path (e.g. /Game/Mats/M1.M1)")))
		.Prop(TEXT("type"), FNexusSchema::Enum(TEXT("Asset kind"), { TEXT("Material"), TEXT("MaterialInstance") }, TEXT("Material")))
		.Prop(TEXT("parentMaterial"), FNexusSchema::Str(TEXT("Parent material path (required for MaterialInstance)")))
		.Prop(TEXT("materialDomain"), FNexusSchema::Enum(TEXT("Material domain (Material only)"),
			{
				TEXT("surface"), TEXT("deferredDecal"), TEXT("lightFunction"), TEXT("volume"), TEXT("postProcess"), TEXT("ui"),
				TEXT("runtimeVirtualTexture")
			},
			TEXT("surface")))
		.Required({ TEXT("assetPath") })
		.Build();
	Out.Tags = {FNexusMcpTags::Write, FNexusMcpTags::Material };
	Out.ExtraSearchKeywords = { TEXT("new"), TEXT("instance"), TEXT("mi"), TEXT("shader"), TEXT("render") };
	Out.RelatedCapabilities = { TEXT("manage_asset_material"), TEXT("get_asset_material") };
	Out.WhenToUse = TEXT("Create empty Material or MaterialInstance");
}

FCapabilityResult FCreateAssetMaterialCapability::Execute(const TSharedPtr<FJsonObject>& Arguments) const
{
	return FNexusCapabilityResultBuilder::Build([&](auto& OutEntries, auto& OutTop, auto& OutError)
	{
		const FNexusArgs A(Arguments);
#if !WITH_EDITOR
		OutError = TEXT("create_asset_material only available in editor builds");
		return;
#else
		TSharedPtr<FJsonObject> OutEntry = MakeShared<FJsonObject>();

		const FString AssetPath = A.Str(TEXT("assetPath"));
		if (FPackageName::DoesPackageExist(AssetPath))
		{
			OutEntry->SetStringField(TEXT("error"), FString::Printf(TEXT("Asset package already exists: %s"), *AssetPath));
			OutEntries.Add(MakeShared<FJsonValueObject>(OutEntry));
			return;
		}
		FText PackageNameError;
		if (!FPackageName::IsValidLongPackageName(AssetPath, false, &PackageNameError))
		{
			OutEntry->SetStringField(TEXT("error"),
				FString::Printf(TEXT("Invalid package path '%s': %s"), *AssetPath, *PackageNameError.ToString()));
			OutEntries.Add(MakeShared<FJsonValueObject>(OutEntry));
			return;
		}

		FString Type = TEXT("Material");
		if (Arguments->HasField(TEXT("type")))
		{
			Type = A.Str(TEXT("type"));
		}
		else if (Arguments->HasField(TEXT("parentMaterial")))
		{
			Type = TEXT("MaterialInstance");
		}
		const FString TypeLower = Type.TrimStartAndEnd().ToLower();
		if (TypeLower != TEXT("material") && TypeLower != TEXT("materialinstance"))
		{
			OutEntry->SetStringField(TEXT("error"),
				FString::Printf(TEXT("Invalid type '%s' (Material|MaterialInstance)"), *Type));
			OutEntries.Add(MakeShared<FJsonValueObject>(OutEntry));
			return;
		}
		const FString PackagePath = FPackageName::GetLongPackagePath(AssetPath);
		const FString AssetName   = FPackageName::GetShortName(AssetPath);
		IAssetTools& AssetTools   = FModuleManager::LoadModuleChecked<FAssetToolsModule>(TEXT("AssetTools")).Get();
		if (TypeLower == TEXT("materialinstance"))
		{
			if (!Arguments->HasField(TEXT("parentMaterial")))
			{
				OutEntry->SetStringField(TEXT("error"), TEXT("Create MaterialInstance missing required parentMaterial."));
				OutEntries.Add(MakeShared<FJsonValueObject>(OutEntry));
				return;
			}
			const FString ParentPath = A.Str(TEXT("parentMaterial"));
			UObject* ParentObj = FNexusAssetUtils::LoadAssetWithFallback<UObject>(ParentPath);
			if (!ParentObj)
			{
				OutEntry->SetStringField(TEXT("error"), FString::Printf(TEXT("Parent material not found: %s"), *ParentPath));
				OutEntries.Add(MakeShared<FJsonValueObject>(OutEntry));
				return;
			}
			UMaterialInterface* ParentMat = Cast<UMaterialInterface>(ParentObj);
			if (!ParentMat)
			{
				OutEntry->SetStringField(TEXT("error"),
					FString::Printf(TEXT("parentMaterial is not a MaterialInterface: %s (%s)"), *ParentPath, *ParentObj->GetClass()->GetName()));
				OutEntries.Add(MakeShared<FJsonValueObject>(OutEntry));
				return;
			}
			UMaterialInstanceConstantFactoryNew* Factory = NewObject<UMaterialInstanceConstantFactoryNew>();
			Factory->InitialParent = ParentMat;
			UObject* NewAsset = AssetTools.CreateAsset(AssetName, PackagePath, UMaterialInstanceConstant::StaticClass(), Factory);
			if (!NewAsset)
			{
				OutEntry->SetStringField(TEXT("error"), FString::Printf(TEXT("In %s Create MaterialInstance failed"), *AssetPath));
				OutEntries.Add(MakeShared<FJsonValueObject>(OutEntry));
				return;
			}
			UMaterialInstanceConstant* MI = Cast<UMaterialInstanceConstant>(NewAsset);
			OutEntry->SetStringField(TEXT("type"), TEXT("MaterialInstance"));
			OutEntry->SetStringField(TEXT("name"), MI->GetName());
			OutEntry->SetStringField(TEXT("path"), FNexusAssetUtils::PackagePathOf(MI));
			OutEntry->SetStringField(TEXT("parentMaterial"), FNexusAssetUtils::PackagePathOf(ParentMat));
			TArray<TSharedPtr<FJsonValue>> InheritedParams;
			AppendScalarVectorTextureParamSummaries(MI, InheritedParams);
			OutEntry->SetArrayField(TEXT("inheritedParameters"), InheritedParams);
		}
		else
		{
			// 先校验 domain，避免 CreateAsset 成功后因非法字符串留下半成品资产
			EMaterialDomain DomainToApply = EMaterialDomain::MD_Surface;
			bool bApplyDomain = false;
			FString DomainEcho = TEXT("surface");
			if (Arguments->HasField(TEXT("materialDomain")))
			{
				const FString DomainStr = A.Str(TEXT("materialDomain"));
				if (!DomainStr.TrimStartAndEnd().IsEmpty())
				{
					FString DomErr;
					if (!FNexusMaterialUtils::TryParseMaterialDomain(DomainStr, DomainToApply, DomErr))
					{
						OutEntry->SetStringField(TEXT("error"), DomErr);
						OutEntries.Add(MakeShared<FJsonValueObject>(OutEntry));
						return;
					}
					bApplyDomain = true;
					DomainEcho = DomainStr.TrimStartAndEnd();
				}
			}
			UMaterialFactoryNew* Factory = NewObject<UMaterialFactoryNew>();
			UObject* NewAsset = AssetTools.CreateAsset(AssetName, PackagePath, UMaterial::StaticClass(), Factory);
			if (!NewAsset)
			{
				OutEntry->SetStringField(TEXT("error"), FString::Printf(TEXT("In %s Create Material failed"), *AssetPath));
				OutEntries.Add(MakeShared<FJsonValueObject>(OutEntry));
				return;
			}
			UMaterial* Mat = Cast<UMaterial>(NewAsset);
			if (bApplyDomain)
			{
				Mat->MaterialDomain = DomainToApply;
				Mat->PostEditChange();
			}
			OutEntry->SetStringField(TEXT("type"), TEXT("Material"));
			OutEntry->SetStringField(TEXT("name"), Mat->GetName());
			OutEntry->SetStringField(TEXT("path"), FNexusAssetUtils::PackagePathOf(Mat));
			OutEntry->SetStringField(TEXT("materialDomain"), DomainEcho);
		}
		OutEntries.Add(MakeShared<FJsonValueObject>(OutEntry));
#endif
	});
}
REGISTER_MCP_CAPABILITY(FCreateAssetMaterialCapability)

