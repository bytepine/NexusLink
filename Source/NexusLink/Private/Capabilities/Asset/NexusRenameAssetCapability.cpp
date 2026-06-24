// Copyright byteyang. All Rights Reserved.

#include "Capabilities/Asset/NexusRenameAssetCapability.h"

#if WITH_EDITOR

#include "Utils/NexusCapabilityResultBuilder.h"
#include "NexusCapabilityRegistry.h"
#include "NexusMcpSchemaBuilder.h"
#include "AssetToolsModule.h"
#include "IAssetTools.h"
#include "NexusMcpTool.h"

void FRenameAssetCapability::BuildDefinition(FNexusCapabilityDefinition& Out) const
{
	Out.Name = TEXT("rename_asset");
	Out.Description = TEXT("移动或重命名资产。自动生成重定向器修复引用。");
	Out.InputSchema = FNexusSchema::Object()
		.Prop(TEXT("assetPath"), FNexusSchema::Str(TEXT("当前资产路径")))
		.Prop(TEXT("newPath"),   FNexusSchema::Str(TEXT("新完整资产路径")))
		.Required({ TEXT("assetPath"), TEXT("newPath") })
		.Build();
	Out.Tags = {FNexusMcpTags::Write, FNexusMcpTags::Editor };
	Out.ExtraSearchKeywords = { TEXT("move"), TEXT("relocate"), TEXT("path"), TEXT("redirect"), TEXT("package") };
	Out.RelatedCapabilities = { TEXT("save_asset"), TEXT("delete_asset") };
}

FCapabilityResult FRenameAssetCapability::Execute(const TSharedPtr<FJsonObject>& Arguments) const
{

	return FNexusCapabilityResultBuilder::Build([&](auto& OutEntries, auto& OutTop, auto& OutError)
	{

		if (!Arguments.IsValid())
		{
			OutError = TEXT("缺少参数");
			return;
		}

		FString OldPath, NewPath;
		if (!Arguments->TryGetStringField(TEXT("assetPath"), OldPath) || OldPath.IsEmpty())
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

		UObject* Asset = LoadObject<UObject>(nullptr, *OldPath);
		if (!Asset)
		{
			Asset = LoadObject<UObject>(nullptr, *(OldPath + TEXT(".") + FPaths::GetBaseFilename(OldPath)));
		}
		if (!Asset)
		{
			OutError = FString::Printf(TEXT("资产未找到: %s"), *OldPath);
			return;
		}

		const FString NewName    = FPaths::GetBaseFilename(NewPath);
		const FString NewPkgPath = FPaths::GetPath(NewPath);

		IAssetTools& AssetTools = FModuleManager::LoadModuleChecked<FAssetToolsModule>(TEXT("AssetTools")).Get();
		TArray<FAssetRenameData> RenameData;
		RenameData.Add(FAssetRenameData(Asset, NewPkgPath, NewName));
		const bool bOk = AssetTools.RenameAssets(RenameData);

		TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
		if (!bOk)
		{
			Entry->SetBoolField(TEXT("success"), false);
			Entry->SetStringField(TEXT("error"), FString::Printf(TEXT("重命名失败: %s -> %s"), *OldPath, *NewPath));
		}
		else
		{
			Entry->SetStringField(TEXT("oldPath"), OldPath);
			Entry->SetStringField(TEXT("newPath"), NewPath);
			Entry->SetBoolField(TEXT("success"), true);
		}
		OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
	
	});
}

REGISTER_MCP_CAPABILITY(FRenameAssetCapability)

#endif // WITH_EDITOR
