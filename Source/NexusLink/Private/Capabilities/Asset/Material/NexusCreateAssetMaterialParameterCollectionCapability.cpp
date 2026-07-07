// Copyright byteyang. All Rights Reserved.

#include "Capabilities/Asset/Material/NexusCreateAssetMaterialParameterCollectionCapability.h"
#include "Utils/NexusCapabilityResultBuilder.h"
#include "NexusCapabilityRegistry.h"
#include "NexusMcpSchemaBuilder.h"
#include "Utils/NexusAssetUtils.h"
#include "NexusMcpTool.h"
#include "Materials/MaterialParameterCollection.h"
#if WITH_EDITOR
#include "Factories/MaterialParameterCollectionFactoryNew.h"
#include "AssetToolsModule.h"
#include "IAssetTools.h"
#endif
#include "Misc/PackageName.h"

void FCreateAssetMaterialParameterCollectionCapability::BuildDefinition(FNexusCapabilityDefinition& Out) const
{
	Out.Name = TEXT("create_asset_material_parameter_collection");
	Out.Description = TEXT("创建空白 MaterialParameterCollection。用 manage 添加参数。");
	Out.InputSchema = FNexusSchema::Object()
		.Required(TEXT("assetPath"), FNexusSchema::Str(TEXT("资产包路径（/Game/…/MPC_Global）")))
		.Build();
	Out.Tags = { FNexusMcpTags::Write, FNexusMcpTags::Material };
	Out.ExtraSearchKeywords = { TEXT("mpc"), TEXT("parameter"), TEXT("collection"), TEXT("global"), TEXT("material"), TEXT("shader") };
	Out.RelatedCapabilities = { TEXT("get_asset_material_parameter_collection"), TEXT("manage_asset_material_parameter_collection") };
	Out.WhenToUse = TEXT("新建 MaterialParameterCollection；之后用 manage 添加标量/向量参数");
}

FCapabilityResult FCreateAssetMaterialParameterCollectionCapability::Execute(const TSharedPtr<FJsonObject>& Arguments) const
{
	return FNexusCapabilityResultBuilder::Build([&](auto& OutEntries, auto& OutTop, auto& OutError)
	{
#if !WITH_EDITOR
		OutError = TEXT("create_asset_material_parameter_collection 仅在编辑器构建可用");
		return;
#else
		TSharedPtr<FJsonObject> OutEntry = MakeShared<FJsonObject>();

		FString AssetPath;
		if (!Arguments->TryGetStringField(TEXT("assetPath"), AssetPath) || AssetPath.IsEmpty())
		{
			OutError = TEXT("assetPath 为必填项");
			return;
		}

		if (FPackageName::DoesPackageExist(AssetPath))
		{
			OutEntry->SetStringField(TEXT("error"), FString::Printf(TEXT("MaterialParameterCollection 已存在: %s"), *AssetPath));
			OutEntries.Add(MakeShared<FJsonValueObject>(OutEntry));
			return;
		}

		const FString PackagePath = FPackageName::GetLongPackagePath(AssetPath);
		const FString AssetName   = FPackageName::GetShortName(AssetPath);
		IAssetTools& AT = FModuleManager::LoadModuleChecked<FAssetToolsModule>(TEXT("AssetTools")).Get();

		UMaterialParameterCollectionFactoryNew* Factory = NewObject<UMaterialParameterCollectionFactoryNew>();
		UObject* NewAsset = AT.CreateAsset(AssetName, PackagePath, UMaterialParameterCollection::StaticClass(), Factory);
		if (!NewAsset)
		{
			FNexusCapabilityResultBuilder::AddEntryError(OutEntries, FString::Printf(TEXT("MPC 创建失败: %s"), *AssetPath));
			return;
		}

		UMaterialParameterCollection* MPC = Cast<UMaterialParameterCollection>(NewAsset);
		OutEntry->SetStringField(TEXT("assetType"), TEXT("MaterialParameterCollection"));
		OutEntry->SetStringField(TEXT("name"),    MPC->GetName());
		OutEntry->SetStringField(TEXT("path"),    FNexusAssetUtils::PackagePathOf(MPC));
		OutEntry->SetBoolField(TEXT("success"),   true);
		OutEntries.Add(MakeShared<FJsonValueObject>(OutEntry));
#endif
	});
}

REGISTER_MCP_CAPABILITY(FCreateAssetMaterialParameterCollectionCapability)
