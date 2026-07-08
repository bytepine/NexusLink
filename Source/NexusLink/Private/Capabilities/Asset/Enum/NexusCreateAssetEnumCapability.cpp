// Copyright byteyang. All Rights Reserved.

#include "Capabilities/Asset/Enum/NexusCreateAssetEnumCapability.h"
#include "NexusCapabilityRegistry.h"
#include "NexusMcpSchemaBuilder.h"
#include "Utils/NexusAssetUtils.h"
#include "Utils/NexusCapabilityResultBuilder.h"
#include "Engine/UserDefinedEnum.h"
#include "NexusMcpTool.h"

// EnumEditorUtils 属于 UnrealEd（Editor-only），Game 目标不可用
#if WITH_EDITOR
#include "Kismet2/EnumEditorUtils.h"
#endif

void FCreateAssetEnumCapability::BuildDefinition(FNexusCapabilityDefinition& Out) const
{
	Out.Name = TEXT("create_asset_enum");
	Out.Description = TEXT("创建 UserDefinedEnum（蓝图枚举）资产；用 manage 增删枚举项。");
	Out.InputSchema = FNexusSchema::Object()
		.Prop(TEXT("assetPath"), FNexusSchema::Str(TEXT("枚举资产包路径")))
		.Required({ TEXT("assetPath") })
		.Build();
	Out.Tags = { FNexusMcpTags::Write, FNexusMcpTags::Data };
	Out.ExtraSearchKeywords = { TEXT("enum"), TEXT("enumeration"), TEXT("blueprint"), TEXT("user"), TEXT("defined") };
	Out.RelatedCapabilities = { TEXT("get_asset_enum"), TEXT("manage_asset_enum") };
	Out.WhenToUse = TEXT("创建新的蓝图枚举资产");
}

FCapabilityResult FCreateAssetEnumCapability::Execute(const TSharedPtr<FJsonObject>& Arguments) const
{
	return FNexusCapabilityResultBuilder::Build([&](auto& OutEntries, auto& OutTop, auto& OutError)
	{
#if !WITH_EDITOR
		OutError = TEXT("create_asset_enum 仅在 Editor 版本中可用");
		return;
#else
		if (!Arguments.IsValid() || !Arguments->HasField(TEXT("assetPath")))
		{
			OutError = TEXT("缺少 assetPath");
			return;
		}

		const FString AssetPath = Arguments->GetStringField(TEXT("assetPath"));

		if (LoadObject<UUserDefinedEnum>(nullptr, *AssetPath))
		{
			FNexusCapabilityResultBuilder::AddEntryError(OutEntries,
				FString::Printf(TEXT("UserDefinedEnum already exists: %s"), *AssetPath));
			return;
		}

		UPackage* Package = CreatePackage(*AssetPath);
		if (!Package) { FNexusCapabilityResultBuilder::AddEntryError(OutEntries, TEXT("创建包失败")); return; }

		const FString AssetName = FPaths::GetBaseFilename(AssetPath);
		UUserDefinedEnum* NewEnum = NewObject<UUserDefinedEnum>(Package, *AssetName, RF_Public | RF_Standalone | RF_Transactional);
		if (!NewEnum) { FNexusCapabilityResultBuilder::AddEntryError(OutEntries, TEXT("枚举对象创建失败")); return; }

		NewEnum->SetMetaData(TEXT("BlueprintType"), TEXT("true"));
		// 添加默认首项，使枚举有效
		FEnumEditorUtils::AddNewEnumeratorForUserDefinedEnum(NewEnum);

		FNexusAssetUtils::NotifyAndSaveCreated(Package, NewEnum, AssetPath);

		TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
		Entry->SetStringField(TEXT("name"),         NewEnum->GetName());
		Entry->SetStringField(TEXT("path"),         NewEnum->GetPathName());
		Entry->SetNumberField(TEXT("entryCount"),   NewEnum->NumEnums() - 1); // 减去内部 _MAX
		Entry->SetBoolField(TEXT("success"),        true);
		OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
#endif // WITH_EDITOR
	});
}

REGISTER_MCP_CAPABILITY(FCreateAssetEnumCapability)
