// Copyright byteyang. All Rights Reserved.

#include "Capabilities/Asset/DataAsset/NexusCreateAssetDataTableCapability.h"
#include "Utils/NexusCapabilityResultBuilder.h"
#include "Utils/NexusArgs.h"
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

void FNexusCreateAssetDataTableCapability::BuildDefinition(FNexusCapabilityDefinition& Out) const
{
	Out.Name = TEXT("CreateAssetDataTable");
	Out.Description = TEXT("Create DataTable with row struct; fill rows via ManageAssetDataTable.");
	Out.InputSchema = FNexusSchema::Object()
		.Prop(TEXT("AssetPath"),     FNexusSchema::Str(TEXT("New DataTable package path")))
		.Prop(TEXT("RowStructName"), FNexusSchema::Str(TEXT("Row struct class name (must exist)")))
		.Required({ TEXT("AssetPath"), TEXT("RowStructName") })
		.Build();
	Out.Tags = {FNexusMcpTags::Write, FNexusMcpTags::Data };
	Out.ExtraSearchKeywords = { TEXT("dt"), TEXT("datatable"), TEXT("rowstruct"), TEXT("new"), TEXT("Row") };
	Out.RelatedCapabilities = { TEXT("ManageAssetDataTable"), TEXT("GetAssetDataTable") };
	Out.WhenToUse = TEXT("Create empty DataTable; requires RowStructName");
}

FCapabilityResult FNexusCreateAssetDataTableCapability::Execute(const TSharedPtr<FJsonObject>& Arguments) const
{

	return FNexusCapabilityResultBuilder::Build([&](auto& OutEntries, auto& OutTop, auto& OutError)
	{
		const FNexusArgs A(Arguments);

		TSharedPtr<FJsonObject> OutEntry = MakeShared<FJsonObject>();


		const FString AssetPath     = A.Str(TEXT("AssetPath"));
		const FString RowStructName = A.Str(TEXT("RowStructName"));

		// 覆盖磁盘上已存在但未加载的包 — CreatePlainAsset 已检查
		UScriptStruct* RowStruct = FindRowStructByName(RowStructName);
		if (!RowStruct)
		{
			OutEntry->SetStringField(TEXT("Error"), FString::Printf(TEXT("Row struct not found: %s"), *RowStructName));
			OutEntries.Add(MakeShared<FJsonValueObject>(OutEntry));
			return;
		}

		const FNexusAssetUtils::FAssetCreateOutcome Created =
			FNexusAssetUtils::CreatePlainAsset<UDataTable>(AssetPath, RF_Public | RF_Standalone, false);
		if (!Created.Ok())
		{
			FNexusCapabilityResultBuilder::AddEntryError(OutEntries, Created.Error);
			return;
		}
		UDataTable* NewDT = Cast<UDataTable>(Created.Asset);
		NewDT->RowStruct = RowStruct;
		FNexusAssetUtils::NotifyAndSaveCreated(NewDT->GetOutermost(), NewDT, AssetPath);

		OutEntry->SetStringField(TEXT("Path"), AssetPath);
		OutEntry->SetStringField(TEXT("name"), NewDT->GetName());
		OutEntries.Add(MakeShared<FJsonValueObject>(OutEntry));
	
	});
}

REGISTER_MCP_CAPABILITY(FNexusCreateAssetDataTableCapability)
