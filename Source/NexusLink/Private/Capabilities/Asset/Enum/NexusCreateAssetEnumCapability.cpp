// Copyright byteyang. All Rights Reserved.

#include "Capabilities/Asset/Enum/NexusCreateAssetEnumCapability.h"
#include "NexusCapabilityRegistry.h"
#include "NexusMcpSchemaBuilder.h"
#include "Utils/NexusAssetUtils.h"
#include "Utils/NexusCapabilityResultBuilder.h"
#include "Utils/NexusArgs.h"
#include "Engine/UserDefinedEnum.h"
#include "NexusMcpTool.h"

// EnumEditorUtils 属于 UnrealEd（Editor-only），Game 目标不可用
#if WITH_EDITOR
#include "Kismet2/EnumEditorUtils.h"
#endif

void FCreateAssetEnumCapability::BuildDefinition(FNexusCapabilityDefinition& Out) const
{
	Out.Name = TEXT("create_asset_enum");
	Out.Description = TEXT("Create UserDefinedEnum asset; add/remove entries via manage.");
	Out.InputSchema = FNexusSchema::Object()
		.Prop(TEXT("assetPath"), FNexusSchema::Str(TEXT("Enum asset package path")))
		.Required({ TEXT("assetPath") })
		.Build();
	Out.Tags = { FNexusMcpTags::Write, FNexusMcpTags::Data };
	Out.ExtraSearchKeywords = { TEXT("enum"), TEXT("enumeration"), TEXT("blueprint"), TEXT("user"), TEXT("defined") };
	Out.RelatedCapabilities = { TEXT("get_asset_enum"), TEXT("manage_asset_enum") };
	Out.WhenToUse = TEXT("Create new Blueprint enum asset");
}

FCapabilityResult FCreateAssetEnumCapability::Execute(const TSharedPtr<FJsonObject>& Arguments) const
{
	return FNexusCapabilityResultBuilder::Build([&](auto& OutEntries, auto& OutTop, auto& OutError)
	{
		const FNexusArgs A(Arguments);
#if !WITH_EDITOR
		OutError = TEXT("create_asset_enum only available in Editor builds");
		return;
#else

		const FString AssetPath = A.Str(TEXT("assetPath"));

		if (LoadObject<UUserDefinedEnum>(nullptr, *AssetPath))
		{
			FNexusCapabilityResultBuilder::AddEntryError(OutEntries,
				FString::Printf(TEXT("UserDefinedEnum already exists: %s"), *AssetPath));
			return;
		}

		UPackage* Package = CreatePackage(*AssetPath);
		if (!Package) { FNexusCapabilityResultBuilder::AddEntryError(OutEntries, TEXT("Failed to create package")); return; }

		const FString AssetName = FPaths::GetBaseFilename(AssetPath);
		// 必须走 CreateUserDefinedEnum：内部 SetEnums(..., Namespaced)；裸 NewObject 的 CppForm 默认 Regular，
		// 再调 AddNewEnumerator 会触发 UserDefinedEnum.cpp ensure(CppForm == Namespaced) 崩溃。
		UEnum* Created = FEnumEditorUtils::CreateUserDefinedEnum(
			Package, *AssetName, RF_Public | RF_Standalone | RF_Transactional);
		UUserDefinedEnum* NewEnum = Cast<UUserDefinedEnum>(Created);
		if (!NewEnum) { FNexusCapabilityResultBuilder::AddEntryError(OutEntries, TEXT("Enum object creation failed")); return; }

		// 添加默认首项，使枚举有效（Create 时 names 为空，尚无用户可见 enumerator）
		FEnumEditorUtils::AddNewEnumeratorForUserDefinedEnum(NewEnum);

		FNexusAssetUtils::NotifyAndSaveCreated(Package, NewEnum, AssetPath);

		TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
		Entry->SetStringField(TEXT("name"),         NewEnum->GetName());
		Entry->SetStringField(TEXT("path"),         NewEnum->GetPathName());
		Entry->SetNumberField(TEXT("entryCount"),   NewEnum->NumEnums() - 1); // 减去内部 _MAX
		OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
#endif // WITH_EDITOR
	});
}

REGISTER_MCP_CAPABILITY(FCreateAssetEnumCapability)
