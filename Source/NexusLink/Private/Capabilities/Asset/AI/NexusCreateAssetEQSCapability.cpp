// Copyright byteyang. All Rights Reserved.

#include "Capabilities/Asset/AI/NexusCreateAssetEQSCapability.h"

#if NX_UE_HAS_APP_STYLE

#include "Utils/NexusCapabilityResultBuilder.h"
#include "NexusCapabilityRegistry.h"
#include "NexusMcpSchemaBuilder.h"
#include "Utils/NexusAssetUtils.h"
#include "NexusMcpTool.h"
#include "EnvironmentQuery/EnvQuery.h"
#include "Misc/PackageName.h"
#if WITH_EDITOR
#include "AssetRegistry/AssetRegistryModule.h"
#endif

void FCreateAssetEQSCapability::BuildDefinition(FNexusCapabilityDefinition& Out) const
{
	Out.Name = TEXT("create_asset_eqs");
	Out.Description = TEXT("创建空白 UEnvQuery（EQS 环境查询）。用 manage 添加 Generator/Test。");
	Out.InputSchema = FNexusSchema::Object()
		.Required(TEXT("assetPath"), FNexusSchema::Str(TEXT("资产包路径（/Game/…/EQ_FindCover）")))
		.Build();
	Out.Tags = { FNexusMcpTags::Write, FNexusMcpTags::Blueprint };
	Out.ExtraSearchKeywords = { TEXT("eqs"), TEXT("query"), TEXT("environment"), TEXT("ai"), TEXT("perception"), TEXT("pathfind") };
	Out.RelatedCapabilities = { TEXT("get_asset_eqs"), TEXT("manage_asset_eqs"), TEXT("create_asset_behavior_tree") };
	Out.WhenToUse = TEXT("新建 EQS 环境查询资产；之后用 manage_asset_eqs 添加 Generator/Test");
}

FCapabilityResult FCreateAssetEQSCapability::Execute(const TSharedPtr<FJsonObject>& Arguments) const
{
	return FNexusCapabilityResultBuilder::Build([&](auto& OutEntries, auto& OutTop, auto& OutError)
	{
#if !WITH_EDITOR
		OutError = TEXT("create_asset_eqs 仅在编辑器构建可用");
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
			OutEntry->SetStringField(TEXT("error"), FString::Printf(TEXT("EQS 已存在: %s"), *AssetPath));
			OutEntries.Add(MakeShared<FJsonValueObject>(OutEntry));
			return;
		}

		const FString AssetName = FPaths::GetBaseFilename(AssetPath);
		UPackage* Pkg = CreatePackage(*AssetPath);
		if (!Pkg)
		{
			FNexusCapabilityResultBuilder::AddEntryError(OutEntries, TEXT("创建 Package 失败"));
			return;
		}

		UEnvQuery* EQ = NewObject<UEnvQuery>(Pkg, *AssetName, RF_Public | RF_Standalone);
		if (!EQ)
		{
			FNexusCapabilityResultBuilder::AddEntryError(OutEntries, FString::Printf(TEXT("EQS 创建失败: %s"), *AssetPath));
			return;
		}
		OutEntry->SetStringField(TEXT("assetType"), TEXT("EnvQuery"));
		OutEntry->SetStringField(TEXT("name"),    EQ->GetName());
		OutEntry->SetStringField(TEXT("path"),    FNexusAssetUtils::PackagePathOf(EQ));
		OutEntry->SetBoolField(TEXT("success"),   true);
		OutEntries.Add(MakeShared<FJsonValueObject>(OutEntry));
#endif
	});
}

REGISTER_MCP_CAPABILITY(FCreateAssetEQSCapability)

#endif // NX_UE_HAS_APP_STYLE
