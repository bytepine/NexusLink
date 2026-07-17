// Copyright byteyang. All Rights Reserved.

#include "Capabilities/Asset/Struct/NexusCreateAssetStructCapability.h"

#if WITH_EDITOR

#include "Utils/NexusCapabilityResultBuilder.h"
#include "NexusCapabilityRegistry.h"
#include "NexusMcpSchemaBuilder.h"
#include "Utils/NexusAssetUtils.h"
#if NX_UE_HAS_STRUCT_UTILS_HEADER
#include "StructUtils/UserDefinedStruct.h"
#else
#include "Engine/UserDefinedStruct.h"
#endif
#include "Kismet2/StructureEditorUtils.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "NexusMcpTool.h"

void FCreateAssetStructCapability::BuildDefinition(FNexusCapabilityDefinition& Out) const
{
	Out.Name = TEXT("create_asset_struct");
	Out.Description = TEXT("创建 UDS 文件，自动编译；用 manage_asset_struct_field 加字段。");
	Out.InputSchema = FNexusSchema::Object()
		.Prop(TEXT("assetPath"), FNexusSchema::Str(TEXT("新结构体包路径")))
		.Required({ TEXT("assetPath") })
		.Build();
	Out.Tags = {FNexusMcpTags::Write, FNexusMcpTags::Struct };
	Out.ExtraSearchKeywords = { TEXT("uds"), TEXT("fields"), TEXT("schema"), TEXT("record"), TEXT("new") };
	Out.RelatedCapabilities = { TEXT("manage_asset_struct_field"), TEXT("get_asset_struct") };
	Out.WhenToUse = TEXT("创建空白 UDS；用 manage 添加字段");
}

FCapabilityResult FCreateAssetStructCapability::Execute(const TSharedPtr<FJsonObject>& Arguments) const
{

	return FNexusCapabilityResultBuilder::Build([&](auto& OutEntries, auto& OutTop, auto& OutError)
	{

		if (!Arguments.IsValid() || !Arguments->HasField(TEXT("assetPath")))
		{
			OutError = TEXT("缺少 assetPath");
			return;
		}

		TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();

		const FString AssetPath = Arguments->GetStringField(TEXT("assetPath"));
		const FString AssetName = FPaths::GetBaseFilename(AssetPath);

		if (LoadObject<UUserDefinedStruct>(nullptr, *AssetPath))
		{ Entry->SetStringField(TEXT("error"), FString::Printf(TEXT("Struct 已存在: %s"), *AssetPath)); OutEntries.Add(MakeShared<FJsonValueObject>(Entry)); return; }

		UPackage* Package = CreatePackage(*AssetPath);
		if (!Package) { Entry->SetStringField(TEXT("error"), TEXT("创建包失败")); OutEntries.Add(MakeShared<FJsonValueObject>(Entry)); return; }

		UUserDefinedStruct* NewStruct = FStructureEditorUtils::CreateUserDefinedStruct(
			Package, FName(*AssetName), RF_Public | RF_Standalone);
		if (!NewStruct) { Entry->SetStringField(TEXT("error"), TEXT("Struct 创建失败")); OutEntries.Add(MakeShared<FJsonValueObject>(Entry)); return; }

		FNexusAssetUtils::NotifyAndSaveCreated(Package, NewStruct, AssetPath);

		Entry->SetStringField(TEXT("name"),   NewStruct->GetName());
		Entry->SetStringField(TEXT("path"),   NewStruct->GetPathName());
		OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
	
	});
}

REGISTER_MCP_CAPABILITY(FCreateAssetStructCapability)

#endif // WITH_EDITOR
