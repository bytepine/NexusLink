// Copyright byteyang. All Rights Reserved.

#include "Capabilities/Asset/NexusExportAssetCapability.h"

#if WITH_EDITOR

#include "Utils/NexusCapabilityResultBuilder.h"
#include "NexusCapabilityRegistry.h"
#include "NexusMcpSchemaBuilder.h"
#include "Utils/NexusAssetUtils.h"
#include "Utils/NexusArgs.h"
#include "Exporters/Exporter.h"
#include "AssetExportTask.h"
#include "NexusMcpTool.h"

void FNexusExportAssetCapability::BuildDefinition(FNexusCapabilityDefinition& Out) const
{
	Out.Name = TEXT("ExportAsset");
	Out.Description = TEXT("Export asset to disk without dialogs. Output extension selects a compatible UE exporter.");
	Out.InputSchema = FNexusSchema::Object()
		.Prop(TEXT("AssetPath"),  FNexusSchema::Str(TEXT("Asset path")))
		.Prop(TEXT("OutputPath"), FNexusSchema::Str(TEXT("Export file path (ext included; empty auto-exports to Saved/Exported/)")))
		.Prop(TEXT("ConflictPolicy"), FNexusSchema::Enum(TEXT("Primary output path conflict policy; sidecar files are exporter-specific"), {TEXT("Fail"), TEXT("Unique"), TEXT("Replace")}, TEXT("Replace")))
		.Prop(TEXT("bValidateOnly"), FNexusSchema::Bool(TEXT("Inspect exporter and destination without creating directories or writing files"), true, false))
		.Required({ TEXT("AssetPath") })
		.Build();
	Out.Tags = { FNexusMcpTags::Write, FNexusMcpTags::Editor };
	Out.ExtraSearchKeywords = { TEXT("export"), TEXT("save"), TEXT("File"), TEXT("disk") };
	Out.RelatedCapabilities = { TEXT("SearchAsset"), TEXT("DuplicateAsset"), TEXT("GetAssetExportOptions") };
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

		const FString RequestedExtension = FPaths::GetExtension(OutputPath);
		UExporter* Exporter = OutputPath.IsEmpty() ? FindExporterForAsset(Asset)
			: (RequestedExtension.IsEmpty() ? nullptr : UExporter::FindExporter(Asset, *RequestedExtension));
		if (!Exporter)
		{
			FNexusCapability::EmitError(OutEntries, {{TEXT("Path"), AssetPath}},
				TEXT("No applicable exporter for the asset and requested extension"));
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

		OutputPath = FPaths::ConvertRelativePathToFull(OutputPath);
		const FNexusArgs Args(Arguments);
		const FString Policy = Args.Str(TEXT("ConflictPolicy"), TEXT("Replace"));
		const bool bExisted = IFileManager::Get().FileExists(*OutputPath);
		if (bExisted && Policy == TEXT("Fail")) { OutError = TEXT("Primary export destination exists; choose Unique or explicitly Replace"); return; }
		if (Policy == TEXT("Unique"))
		{
			const FString Directory = FPaths::GetPath(OutputPath);
			const FString BaseName = FPaths::GetBaseFilename(OutputPath);
			const FString Extension = FPaths::GetExtension(OutputPath, true);
			int32 Suffix = 1;
			while (IFileManager::Get().FileExists(*OutputPath) && Suffix <= 10000) OutputPath = Directory / (BaseName + TEXT("_") + FString::FromInt(Suffix++) + Extension);
			if (IFileManager::Get().FileExists(*OutputPath)) { OutError = TEXT("Could not find an unused export output path"); return; }
		}
		auto Entry = MakeShared<FJsonObject>();
		Entry->SetStringField(TEXT("Path"), AssetPath);
		Entry->SetStringField(TEXT("AssetClass"), Asset->GetClass()->GetName());
		Entry->SetStringField(TEXT("OutputPath"), OutputPath);
		Entry->SetStringField(TEXT("Exporter"), Exporter->GetClass()->GetName());
		Entry->SetStringField(TEXT("ConflictPolicy"), Policy);
		Entry->SetBoolField(TEXT("bDestinationExisted"), bExisted);
		Entry->SetBoolField(TEXT("bValidateOnly"), Args.Bool(TEXT("bValidateOnly")));
		Entry->SetBoolField(TEXT("bAutomated"), true);
		if (Args.Bool(TEXT("bValidateOnly"))) { Entry->SetBoolField(TEXT("bWritten"), false); OutEntries.Add(MakeShared<FJsonValueObject>(Entry)); return; }
		// 确保目录存在
		{
			FString Dir = FPaths::GetPath(OutputPath);
			if (!Dir.IsEmpty()) IFileManager::Get().MakeDirectory(*Dir, true);
		}

		UAssetExportTask* Task = NewObject<UAssetExportTask>();
		Task->Object = Asset;
		Task->Exporter = Exporter;
		Task->Filename = OutputPath;
		Task->bAutomated = true;
		Task->bPrompt = false;
		Task->bReplaceIdentical = Policy == TEXT("Replace");
		const bool bSuccess = UExporter::RunAssetExportTask(Task);

		Entry->SetBoolField(TEXT("bWritten"), bSuccess);
		if (!bSuccess) Entry->SetStringField(TEXT("Error"), Task->Errors.Num() > 0 ? FString::Join(Task->Errors, TEXT("; ")) : TEXT("Export failed"));

		OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
	});
}

REGISTER_MCP_CAPABILITY(FNexusExportAssetCapability)

#endif // WITH_EDITOR
