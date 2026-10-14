// Copyright byteyang. All Rights Reserved.

#include "Capabilities/Asset/NexusExportAssetCapability.h"

#if WITH_EDITOR

#include "Utils/NexusCapabilityResultBuilder.h"
#include "NexusCapabilityRegistry.h"
#include "NexusMcpSchemaBuilder.h"
#include "Utils/NexusAssetUtils.h"
#include "Exporters/Exporter.h"
#include "NexusMcpTool.h"

void FNexusExportAssetCapability::BuildDefinition(FNexusCapabilityDefinition& Out) const
{
	Out.Name = TEXT("ExportAsset");
	Out.Description = TEXT("Export asset to disk file. Uses UE ExportToFile.");
	Out.InputSchema = FNexusSchema::Object()
		.Prop(TEXT("AssetPath"),  FNexusSchema::Str(TEXT("Asset path")))
		.Prop(TEXT("OutputPath"), FNexusSchema::Str(TEXT("Export file path (ext included; empty auto-exports to Saved/Exported/)")))
		.Required({ TEXT("AssetPath") })
		.Build();
	Out.Tags = { FNexusMcpTags::Write, FNexusMcpTags::Editor };
	Out.ExtraSearchKeywords = { TEXT("export"), TEXT("save"), TEXT("File"), TEXT("disk") };
	Out.RelatedCapabilities = { TEXT("SearchAsset"), TEXT("DuplicateAsset") };
	Out.Prerequisites = { TEXT("EditorOnly") };
	Out.WhenToUse = TEXT("Export asset to external file (FBX/OBJ/PNG)");
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

FCapabilityResult FNexusExportAssetCapability::Execute(const TSharedPtr<FJsonObject>& Arguments) const
{
	return FNexusCapabilityResultBuilder::Build([&](auto& OutEntries, auto& OutTop, auto& OutError)
	{
		FString AssetPath;
		if (!FNexusCapability::RequireString(Arguments, TEXT("AssetPath"), AssetPath, OutEntries, {})) return;

		UObject* Asset = FNexusAssetUtils::LoadAssetWithFallback<UObject>(AssetPath);
		if (!Asset)
		{
			FNexusCapability::EmitError(OutEntries, {{TEXT("Path"), AssetPath}},
				FString::Printf(TEXT("Asset not found: %s"), *AssetPath));
			return;
		}

		FString OutputPath;
		if (Arguments.IsValid()) Arguments->TryGetStringField(TEXT("OutputPath"), OutputPath);

		UExporter* Exporter = FindExporterForAsset(Asset);
		if (!Exporter)
		{
			FNexusCapability::EmitError(OutEntries, {{TEXT("Path"), AssetPath}},
				TEXT("No applicable exporter found"));
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
		Entry->SetStringField(TEXT("Path"), AssetPath);
		Entry->SetStringField(TEXT("AssetClass"), Asset->GetClass()->GetName());
		Entry->SetStringField(TEXT("OutputPath"), OutputPath);
		Entry->SetStringField(TEXT("Exporter"), Exporter->GetClass()->GetName());
		if (!bSuccess) Entry->SetStringField(TEXT("Error"), TEXT("Exportfailed"));

		OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
	});
}

REGISTER_MCP_CAPABILITY(FNexusExportAssetCapability)

#endif // WITH_EDITOR
