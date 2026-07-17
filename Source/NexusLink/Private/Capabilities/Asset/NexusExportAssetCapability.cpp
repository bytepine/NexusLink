// Copyright byteyang. All Rights Reserved.

#include "Capabilities/Asset/NexusExportAssetCapability.h"

#if WITH_EDITOR

#include "Utils/NexusCapabilityResultBuilder.h"
#include "NexusCapabilityRegistry.h"
#include "NexusMcpSchemaBuilder.h"
#include "Utils/NexusAssetUtils.h"
#include "Exporters/Exporter.h"
#include "NexusMcpTool.h"

void FExportAssetCapability::BuildDefinition(FNexusCapabilityDefinition& Out) const
{
	Out.Name = TEXT("export_asset");
	Out.Description = TEXT("导出资产到磁盘文件。使用 UE ExportToFile。");
	Out.InputSchema = FNexusSchema::Object()
		.Prop(TEXT("assetPath"),  FNexusSchema::Str(TEXT("资产路径")))
		.Prop(TEXT("outputPath"), FNexusSchema::Str(TEXT("导出目标文件路径（含扩展名；留空自动生成到 Saved/Exported/）")))
		.Required({ TEXT("assetPath") })
		.Build();
	Out.Tags = { FNexusMcpTags::Write, FNexusMcpTags::Editor };
	Out.ExtraSearchKeywords = { TEXT("export"), TEXT("save"), TEXT("file"), TEXT("disk") };
	Out.RelatedCapabilities = { TEXT("search_asset"), TEXT("duplicate_asset") };
	Out.Prerequisites = { TEXT("editor_only") };
	Out.WhenToUse = TEXT("导出资产到外部文件（如 FBX/OBJ/PNG）");
}

/** 遍历所有 UExporter 子类，找到支持目标资产类型的导出器 */
static UExporter* FindExporterForAsset(UObject* Asset)
{
	if (!Asset) return nullptr;
	UClass* AssetClass = Asset->GetClass();

	for (TObjectIterator<UClass> It; It; ++It)
	{
		if (!It->IsChildOf(UExporter::StaticClass()) || *It == UExporter::StaticClass()) continue;
		if (It->HasAnyClassFlags(CLASS_Abstract)) continue;

		UExporter* ExpCDO = Cast<UExporter>(It->GetDefaultObject());
		if (!ExpCDO) continue;

		// 检查 ExportAssetType 或 SupportedClass
		if (ExpCDO->SupportedClass && AssetClass->IsChildOf(ExpCDO->SupportedClass))
		{
			return NewObject<UExporter>(GetTransientPackage(), *It);
		}
	}
	return nullptr;
}

FCapabilityResult FExportAssetCapability::Execute(const TSharedPtr<FJsonObject>& Arguments) const
{
	return FNexusCapabilityResultBuilder::Build([&](auto& OutEntries, auto& OutTop, auto& OutError)
	{
		FString AssetPath;
		if (!FNexusCapability::RequireString(Arguments, TEXT("assetPath"), AssetPath, OutEntries, {})) return;

		UObject* Asset = FNexusAssetUtils::LoadAssetWithFallback<UObject>(AssetPath);
		if (!Asset)
		{
			FNexusCapability::EmitError(OutEntries, {{TEXT("path"), AssetPath}},
				FString::Printf(TEXT("资产未找到: %s"), *AssetPath));
			return;
		}

		FString OutputPath;
		if (Arguments.IsValid()) Arguments->TryGetStringField(TEXT("outputPath"), OutputPath);

		UExporter* Exporter = FindExporterForAsset(Asset);
		if (!Exporter)
		{
			FNexusCapability::EmitError(OutEntries, {{TEXT("path"), AssetPath}},
				TEXT("未找到适用的导出器"));
			return;
		}

		// 自动生成输出路径
		if (OutputPath.IsEmpty())
		{
			FString Extension = TEXT(".export");
			// 尝试从导出器获取扩展名（跨版本兼容：反射读取 PreferredFormatIndex 对应格式）
			if (Exporter->FormatExtension.Num() > 0)
			{
				Extension = Exporter->FormatExtension[0];
				if (!Extension.StartsWith(TEXT("."))) Extension = TEXT(".") + Extension;
			}
			OutputPath = FPaths::ProjectSavedDir() / TEXT("Exported") / (Asset->GetName() + Extension);
		}

		// 确保目录存在
		{
			FString Dir = FPaths::GetPath(OutputPath);
			if (!Dir.IsEmpty()) IFileManager::Get().MakeDirectory(*Dir, true);
		}

		const int32 ExportResult = UExporter::ExportToFile(Asset, Exporter, *OutputPath, false, false, false);
		const bool bSuccess = (ExportResult != 0);

		TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
		Entry->SetStringField(TEXT("path"), AssetPath);
		Entry->SetStringField(TEXT("assetClass"), Asset->GetClass()->GetName());
		Entry->SetStringField(TEXT("outputPath"), OutputPath);
		Entry->SetStringField(TEXT("exporter"), Exporter->GetClass()->GetName());
		if (!bSuccess) Entry->SetStringField(TEXT("error"), TEXT("导出失败"));

		OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
	});
}

REGISTER_MCP_CAPABILITY(FExportAssetCapability)

#endif // WITH_EDITOR
