// Copyright byteyang. All Rights Reserved.

#include "Capabilities/Asset/DataAsset/NexusCreateAssetDataAssetCapability.h"
#include "Utils/NexusCapabilityResultBuilder.h"
#include "NexusCapabilityRegistry.h"
#include "NexusMcpSchemaBuilder.h"
#include "Utils/NexusAssetUtils.h"
#include "Engine/DataAsset.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "NexusMcpTool.h"

void FCreateAssetDataAssetCapability::BuildDefinition(FNexusCapabilityDefinition& Out) const
{
	Out.Name = TEXT("create_asset_data_asset");
	Out.Description = TEXT("创建类型化 DataAsset。需子类名；非抽象类。");
	Out.InputSchema = FNexusSchema::Object()
		.Prop(TEXT("assetPath"),   FNexusSchema::Str(TEXT("新 DataAsset 包路径")))
		.Prop(TEXT("parentClass"), FNexusSchema::Str(TEXT("非抽象父类名"), TEXT("PrimaryDataAsset")))
		.Required({ TEXT("assetPath") })
		.Build();
	Out.Tags = {FNexusMcpTags::Write, FNexusMcpTags::Data };
	Out.ExtraSearchKeywords = { TEXT("dataasset"), TEXT("primarydataasset"), TEXT("config"), TEXT("new") };
	Out.RelatedCapabilities = { TEXT("manage_asset_data_asset"), TEXT("get_asset_data_asset") };
	Out.WhenToUse = TEXT("创建 DataAsset；parentClass 默认 PrimaryDataAsset");
}

FCapabilityResult FCreateAssetDataAssetCapability::Execute(const TSharedPtr<FJsonObject>& Arguments) const
{

	return FNexusCapabilityResultBuilder::Build([&](auto& OutEntries, auto& OutTop, auto& OutError)
	{

		TSharedPtr<FJsonObject> OutEntry = MakeShared<FJsonObject>();

		if (!Arguments.IsValid() || !Arguments->HasField(TEXT("assetPath")))
		{
			OutError = TEXT("缺少 assetPath");
			return;
		}

		const FString AssetPath = Arguments->GetStringField(TEXT("assetPath"));

		// 磁盘上已存在包时也会命中，LoadObject 只能发现已加载对象
		if (FPackageName::DoesPackageExist(AssetPath))
		{
			OutEntry->SetStringField(TEXT("error"), FString::Printf(TEXT("DataAsset already exists: %s"), *AssetPath));
			OutEntries.Add(MakeShared<FJsonValueObject>(OutEntry));
			return;
		}

		FString ParentClassName = TEXT("PrimaryDataAsset");
		if (Arguments->HasField(TEXT("parentClass"))) ParentClassName = Arguments->GetStringField(TEXT("parentClass"));

		UClass* ParentClass = FNexusAssetUtils::FindClassWithUPrefix(ParentClassName);
		if (!ParentClass || !ParentClass->IsChildOf(UDataAsset::StaticClass()))
		{
			OutEntry->SetStringField(TEXT("error"), FString::Printf(TEXT("DataAsset 子类未找到: %s"), *ParentClassName));
			OutEntries.Add(MakeShared<FJsonValueObject>(OutEntry));
			return;
		}

		if (ParentClass->HasAnyClassFlags(CLASS_Abstract))
		{
			OutEntry->SetStringField(TEXT("error"), FString::Printf(
				TEXT("类 %s 为抽象类，无法直接实例化。请使用非抽象 UDataAsset 子类（如 PrimaryDataAsset 的 Blueprint 子类）作为 parentClass。"),
				*ParentClassName));
			OutEntries.Add(MakeShared<FJsonValueObject>(OutEntry));
			return;
		}

		FText PackageNameError;
		if (!FPackageName::IsValidLongPackageName(AssetPath, false, &PackageNameError))
		{
			OutEntry->SetStringField(TEXT("error"), FString::Printf(TEXT("无效的包路径 '%s': %s"), *AssetPath, *PackageNameError.ToString()));
			OutEntries.Add(MakeShared<FJsonValueObject>(OutEntry));
			return;
		}

		UPackage* Package = CreatePackage(*AssetPath);
		if (!Package) { FNexusCapabilityResultBuilder::AddEntryError(OutEntries, FString::Printf(TEXT("创建包失败: %s"), *AssetPath)); return; }

		const FString AssetName = FPaths::GetBaseFilename(AssetPath);
		UDataAsset* NewDA = NewObject<UDataAsset>(Package, ParentClass, *AssetName, RF_Public | RF_Standalone);
		if (!NewDA) { FNexusCapabilityResultBuilder::AddEntryError(OutEntries, TEXT("DataAsset 创建失败")); return; }

		FNexusAssetUtils::NotifyAndSaveCreated(Package, NewDA, AssetPath);

		OutEntry->SetStringField(TEXT("name"), NewDA->GetName());
		OutEntry->SetStringField(TEXT("path"), NewDA->GetOutermost()->GetName());
		OutEntry->SetStringField(TEXT("parentClass"), ParentClass->GetName());
		OutEntries.Add(MakeShared<FJsonValueObject>(OutEntry));
	
	});
}

REGISTER_MCP_CAPABILITY(FCreateAssetDataAssetCapability)
