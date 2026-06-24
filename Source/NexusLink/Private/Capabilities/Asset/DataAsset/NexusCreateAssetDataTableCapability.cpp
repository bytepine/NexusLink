// Copyright byteyang. All Rights Reserved.

#include "Capabilities/Asset/DataAsset/NexusCreateAssetDataTableCapability.h"
#include "Utils/NexusCapabilityResultBuilder.h"
#include "NexusCapabilityRegistry.h"
#include "NexusMcpSchemaBuilder.h"
#include "Utils/NexusAssetUtils.h"
#include "Engine/DataTable.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Misc/PackageName.h"
#include "NexusMcpTool.h"

// 按裸名与 F 前缀解析行结构体；5.1+ 使用 NativeFirst 降低误命中
static UScriptStruct* FindRowStructByName(const FString& Name)
{
	auto TryOne = [](const FString& N) -> UScriptStruct*
	{
		UScriptStruct* S = nullptr;
#if NX_UE_HAS_FIND_FIRST_OBJECT
		S = FindFirstObject<UScriptStruct>(*N, EFindFirstObjectOptions::NativeFirst);
#else
		S = FindObject<UScriptStruct>(ANY_PACKAGE, *N);
#endif
		if (!S) { S = LoadObject<UScriptStruct>(nullptr, *N); }
		return S;
	};

	if (UScriptStruct* S = TryOne(Name)) { return S; }
	if (!Name.StartsWith(TEXT("F"))) { return TryOne(TEXT("F") + Name); }
	return nullptr;
}

void FCreateAssetDataTableCapability::BuildDefinition(FNexusCapabilityDefinition& Out) const
{
	Out.Name = TEXT("create_asset_data_table");
	Out.Description = TEXT("创建带行结构体的 DT；用 manage_asset_data_table 填行。");
	Out.InputSchema = FNexusSchema::Object()
		.Prop(TEXT("assetPath"),     FNexusSchema::Str(TEXT("新 DataTable 包路径")))
		.Prop(TEXT("rowStructName"), FNexusSchema::Str(TEXT("行结构体类名（须已存在）")))
		.Required({ TEXT("assetPath"), TEXT("rowStructName") })
		.Build();
	Out.Tags = {FNexusMcpTags::Write, FNexusMcpTags::Data };
	Out.ExtraSearchKeywords = { TEXT("dt"), TEXT("datatable"), TEXT("rowstruct"), TEXT("new"), TEXT("row") };
	Out.RelatedCapabilities = { TEXT("manage_asset_data_table"), TEXT("get_asset_data_table") };
	Out.WhenToUse = TEXT("创建空白 DT；需要 rowStructName");
}

FCapabilityResult FCreateAssetDataTableCapability::Execute(const TSharedPtr<FJsonObject>& Arguments) const
{

	return FNexusCapabilityResultBuilder::Build([&](auto& OutEntries, auto& OutTop, auto& OutError)
	{

		TSharedPtr<FJsonObject> OutEntry = MakeShared<FJsonObject>();

		if (!Arguments.IsValid()
			|| !Arguments->HasField(TEXT("assetPath"))
			|| !Arguments->HasField(TEXT("rowStructName")))
		{
			OutEntry->SetStringField(TEXT("error"), TEXT("缺少必填参数: assetPath, rowStructName"));
			OutEntries.Add(MakeShared<FJsonValueObject>(OutEntry));
			return;
		}

		const FString AssetPath     = Arguments->GetStringField(TEXT("assetPath"));
		const FString RowStructName = Arguments->GetStringField(TEXT("rowStructName"));
		if (AssetPath.IsEmpty() || RowStructName.IsEmpty())
		{
			OutEntry->SetStringField(TEXT("error"), TEXT("assetPath 与 rowStructName 不能为空"));
			OutEntries.Add(MakeShared<FJsonValueObject>(OutEntry));
			return;
		}

		// 覆盖磁盘上已存在但未加载的包
		if (FPackageName::DoesPackageExist(AssetPath))
		{
			OutEntry->SetStringField(TEXT("error"), FString::Printf(TEXT("DataTable already exists: %s"), *AssetPath));
			OutEntries.Add(MakeShared<FJsonValueObject>(OutEntry));
			return;
		}

		UScriptStruct* RowStruct = FindRowStructByName(RowStructName);
		if (!RowStruct)
		{
			OutEntry->SetStringField(TEXT("error"), FString::Printf(TEXT("行结构体未找到: %s"), *RowStructName));
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
		UDataTable* NewDT = NewObject<UDataTable>(Package, *AssetName, RF_Public | RF_Standalone);
		if (!NewDT) { FNexusCapabilityResultBuilder::AddEntryError(OutEntries, TEXT("DataTable 创建失败")); return; }

		NewDT->RowStruct = RowStruct;
		FNexusAssetUtils::NotifyAndSaveCreated(Package, NewDT, AssetPath);

		OutEntry->SetStringField(TEXT("name"), NewDT->GetName());
		OutEntry->SetBoolField(TEXT("success"), true);
		OutEntries.Add(MakeShared<FJsonValueObject>(OutEntry));
	
	});
}

REGISTER_MCP_CAPABILITY(FCreateAssetDataTableCapability)
