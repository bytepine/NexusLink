// Copyright byteyang. All Rights Reserved.
#include "Capabilities/Asset/Material/NexusCreateAssetMaterialCapability.h"
#include "Utils/NexusCapabilityResultBuilder.h"
#include "NexusCapabilityRegistry.h"
#include "NexusMcpSchemaBuilder.h"
#include "Utils/NexusMaterialUtils.h"
#include "Utils/NexusAssetUtils.h"
#include "Materials/Material.h"
#if NX_UE_HAS_SCOPED_MATERIAL_DOMAIN
#if NX_UE_HAS_MATERIAL_DOMAIN_HEADER
#include "MaterialDomain.h"
#endif
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
	Out.Description = TEXT("创建材质或材质实例。MI 需 parentMaterial；domain 与 type 一致。");
	Out.InputSchema = FNexusSchema::Object()
		.Prop(TEXT("assetPath"), FNexusSchema::Str(TEXT("新资产完整包路径（如 /Game/Mats/M1.M1）")))
		.Prop(TEXT("type"), FNexusSchema::Enum(TEXT("资产种类"), { TEXT("Material"), TEXT("MaterialInstance") }, TEXT("Material")))
		.Prop(TEXT("parentMaterial"), FNexusSchema::Str(TEXT("父材质路径（type 为 MaterialInstance 时必填）")))
		.Prop(TEXT("materialDomain"), FNexusSchema::Enum(TEXT("材质域（仅 Material）"),
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
	Out.WhenToUse = TEXT("创建空白 Material 或 MaterialInstance");
}

FCapabilityResult FCreateAssetMaterialCapability::Execute(const TSharedPtr<FJsonObject>& Arguments) const
{
	return FNexusCapabilityResultBuilder::Build([&](auto& OutEntries, auto& OutTop, auto& OutError)
	{
#if !WITH_EDITOR
		OutError = TEXT("create_asset_material 仅在编辑器构建可用");
		return;
#else
		TSharedPtr<FJsonObject> OutEntry = MakeShared<FJsonObject>();
		if (!Arguments.IsValid())
		{
			OutError = TEXT("参数无效");
			return;
		}

		FString AssetPath;
		if (!Arguments->TryGetStringField(TEXT("assetPath"), AssetPath) || AssetPath.IsEmpty())
		{
			OutError = TEXT("缺少必填参数 assetPath");
			return;
		}
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
				FString::Printf(TEXT("无效的包路径 '%s': %s"), *AssetPath, *PackageNameError.ToString()));
			OutEntries.Add(MakeShared<FJsonValueObject>(OutEntry));
			return;
		}

		FString Type = TEXT("Material");
		if (Arguments->HasField(TEXT("type")))
		{
			Type = Arguments->GetStringField(TEXT("type"));
		}
		else if (Arguments->HasField(TEXT("parentMaterial")))
		{
			Type = TEXT("MaterialInstance");
		}
		const FString TypeLower = Type.TrimStartAndEnd().ToLower();
		if (TypeLower != TEXT("material") && TypeLower != TEXT("materialinstance"))
		{
			OutEntry->SetStringField(TEXT("error"),
				FString::Printf(TEXT("无效 type '%s'（Material|MaterialInstance）"), *Type));
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
				OutEntry->SetStringField(TEXT("error"), TEXT("创建 MaterialInstance 缺少必填参数 parentMaterial。"));
				OutEntries.Add(MakeShared<FJsonValueObject>(OutEntry));
				return;
			}
			const FString ParentPath = Arguments->GetStringField(TEXT("parentMaterial"));
			UObject* ParentObj = FNexusMaterialUtils::LoadMaterialAsset(ParentPath);
			if (!ParentObj)
			{
				OutEntry->SetStringField(TEXT("error"), FString::Printf(TEXT("父材质未找到: %s"), *ParentPath));
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
				OutEntry->SetStringField(TEXT("error"), FString::Printf(TEXT("在 %s 创建 MaterialInstance 失败"), *AssetPath));
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
				const FString DomainStr = Arguments->GetStringField(TEXT("materialDomain"));
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
				OutEntry->SetStringField(TEXT("error"), FString::Printf(TEXT("在 %s 创建 Material 失败"), *AssetPath));
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

