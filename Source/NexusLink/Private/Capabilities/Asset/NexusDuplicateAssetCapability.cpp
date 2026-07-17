// Copyright byteyang. All Rights Reserved.

#include "Capabilities/Asset/NexusDuplicateAssetCapability.h"

#if WITH_EDITOR

#include "Utils/NexusCapabilityResultBuilder.h"
#include "NexusCapabilityRegistry.h"
#include "NexusMcpSchemaBuilder.h"
#include "AssetToolsModule.h"
#include "IAssetTools.h"
#include "NexusMcpTool.h"

void FDuplicateAssetCapability::BuildDefinition(FNexusCapabilityDefinition& Out) const
{
	Out.Name = TEXT("duplicate_asset");
	Out.Description = TEXT("复制编辑器资产到新路径。源资产不变。");
	Out.InputSchema = FNexusSchema::Object()
		.Prop(TEXT("assetPath"), FNexusSchema::Str(TEXT("源资产路径")))
		.Prop(TEXT("newPath"),   FNexusSchema::Str(TEXT("新完整资产路径（包路径 + 资产名）")))
		.Required({ TEXT("assetPath"), TEXT("newPath") })
		.Build();
	Out.Tags = { FNexusMcpTags::Write, FNexusMcpTags::Editor };
	Out.ExtraSearchKeywords = { TEXT("copy"), TEXT("clone"), TEXT("duplicate"), TEXT("blueprint"), TEXT("bp") };
	Out.RelatedCapabilities = { TEXT("rename_asset"), TEXT("delete_asset") };
}

FCapabilityResult FDuplicateAssetCapability::Execute(const TSharedPtr<FJsonObject>& Arguments) const
{
	return FNexusCapabilityResultBuilder::Build([&](auto& OutEntries, auto& OutTop, auto& OutError)
	{
		if (!Arguments.IsValid())
		{
			OutError = TEXT("缺少参数");
			return;
		}

		FString SrcPath, NewPath;
		if (!Arguments->TryGetStringField(TEXT("assetPath"), SrcPath) || SrcPath.IsEmpty())
		{
			OutError = TEXT("assetPath 为必填项");
			return;
		}
		if (!Arguments->TryGetStringField(TEXT("newPath"), NewPath) || NewPath.IsEmpty())
		{
			OutError = TEXT("缺少 newPath");
			return;
		}

		FText PathErrText;
		if (!FPackageName::IsValidLongPackageName(NewPath, false, &PathErrText))
		{
			OutError = FString::Printf(TEXT("无效的 newPath: %s"), *PathErrText.ToString());
			return;
		}

		if (FPackageName::DoesPackageExist(NewPath))
		{
			OutError = FString::Printf(TEXT("目标已存在: %s"), *NewPath);
			return;
		}

		// 源资产容错加载：先按纯包路径，失败再补 .对象名
		UObject* SrcAsset = LoadObject<UObject>(nullptr, *SrcPath);
		if (!SrcAsset)
		{
			SrcAsset = LoadObject<UObject>(nullptr, *(SrcPath + TEXT(".") + FPaths::GetBaseFilename(SrcPath)));
		}
		if (!SrcAsset)
		{
			OutError = FString::Printf(TEXT("资产未找到: %s"), *SrcPath);
			return;
		}

		const FString NewName    = FPaths::GetBaseFilename(NewPath);
		const FString NewPkgPath = FPaths::GetPath(NewPath);

		IAssetTools& AssetTools = FModuleManager::LoadModuleChecked<FAssetToolsModule>(TEXT("AssetTools")).Get();
		UObject* NewAsset = AssetTools.DuplicateAsset(NewName, NewPkgPath, SrcAsset);

		TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
		if (!NewAsset)
		{
			Entry->SetBoolField(TEXT("success"), false);
			Entry->SetStringField(TEXT("error"), FString::Printf(TEXT("复制失败: %s -> %s"), *SrcPath, *NewPath));
		}
		else
		{
			Entry->SetStringField(TEXT("sourcePath"), SrcPath);
			Entry->SetStringField(TEXT("newPath"), NewPath);
		}
		OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
	});
}

REGISTER_MCP_CAPABILITY(FDuplicateAssetCapability)

#endif // WITH_EDITOR
