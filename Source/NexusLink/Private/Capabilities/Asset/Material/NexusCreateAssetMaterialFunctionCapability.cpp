// Copyright byteyang. All Rights Reserved.

#include "Capabilities/Asset/Material/NexusCreateAssetMaterialFunctionCapability.h"
#include "Utils/NexusCapabilityResultBuilder.h"
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

void FCreateAssetMaterialFunctionCapability::BuildDefinition(FNexusCapabilityDefinition& Out) const
{
	Out.Name = TEXT("create_asset_material_function");
	Out.Description = TEXT("创建空白 UMaterialFunction。可设 description 和 bExposeToLibrary。");
	Out.InputSchema = FNexusSchema::Object()
		.Required(TEXT("assetPath"), FNexusSchema::Str(TEXT("资产包路径（/Game/…/MF_MyFunc）")))
		.Prop(TEXT("description"),     FNexusSchema::Str(TEXT("函数描述（可选）")))
		.Prop(TEXT("exposeToLibrary"), FNexusSchema::Bool(TEXT("是否在材质函数库中显示")))
		.Build();
	Out.Tags = { FNexusMcpTags::Write, FNexusMcpTags::Material };
	Out.ExtraSearchKeywords = { TEXT("mf"), TEXT("function"), TEXT("material"), TEXT("shader"), TEXT("reuse") };
	Out.RelatedCapabilities = { TEXT("get_asset_material"), TEXT("manage_asset_material"), TEXT("create_asset_material") };
	Out.WhenToUse = TEXT("新建 MaterialFunction；之后用 manage_asset_material 添加节点");
}

FCapabilityResult FCreateAssetMaterialFunctionCapability::Execute(const TSharedPtr<FJsonObject>& Arguments) const
{
	return FNexusCapabilityResultBuilder::Build([&](auto& OutEntries, auto& OutTop, auto& OutError)
	{
#if !WITH_EDITOR
		OutError = TEXT("create_asset_material_function 仅在编辑器构建可用");
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
			OutEntry->SetStringField(TEXT("error"), FString::Printf(TEXT("MaterialFunction 已存在: %s"), *AssetPath));
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
			FNexusCapabilityResultBuilder::AddEntryError(OutEntries, FString::Printf(TEXT("MaterialFunction 创建失败: %s"), *AssetPath));
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
		if (Arguments->TryGetBoolField(TEXT("exposeToLibrary"), bExpose))
		{
			MF->bExposeToLibrary = bExpose;
			bDirty = true;
		}

		if (bDirty)
		{
			MF->MarkPackageDirty();
		}

		OutEntry->SetStringField(TEXT("assetType"), TEXT("MaterialFunction"));
		OutEntry->SetStringField(TEXT("name"),    MF->GetName());
		OutEntry->SetStringField(TEXT("path"),    FNexusAssetUtils::PackagePathOf(MF));
		OutEntries.Add(MakeShared<FJsonValueObject>(OutEntry));
#endif
	});
}

REGISTER_MCP_CAPABILITY(FCreateAssetMaterialFunctionCapability)
