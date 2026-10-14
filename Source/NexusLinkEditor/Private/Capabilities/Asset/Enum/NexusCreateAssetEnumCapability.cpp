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

void FNexusCreateAssetEnumCapability::BuildDefinition(FNexusCapabilityDefinition& Out) const
{
	Out.Name = TEXT("CreateAssetEnum");
	Out.Description = TEXT("Create UserDefinedEnum asset; add/remove entries via manage.");
	Out.InputSchema = FNexusSchema::Object()
		.Prop(TEXT("AssetPath"), FNexusSchema::Str(TEXT("Enum asset package path")))
		.Required({ TEXT("AssetPath") })
		.Build();
	Out.Tags = { FNexusMcpTags::Write, FNexusMcpTags::Data };
	Out.ExtraSearchKeywords = { TEXT("enum"), TEXT("enumeration"), TEXT("blueprint"), TEXT("user"), TEXT("defined") };
	Out.RelatedCapabilities = { TEXT("GetAssetEnum"), TEXT("ManageAssetEnum") };
	Out.WhenToUse = TEXT("Create new Blueprint enum asset");
}

FCapabilityResult FNexusCreateAssetEnumCapability::Execute(const TSharedPtr<FJsonObject>& Arguments) const
{
	return FNexusCapabilityResultBuilder::Build([&](auto& OutEntries, auto& OutTop, auto& OutError)
	{
		const FNexusArgs A(Arguments);
#if !WITH_EDITOR
		OutError = TEXT("CreateAssetEnum only available in Editor builds");
		return;
#else

		const FString AssetPath = A.Str(TEXT("AssetPath"));

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

		TSharedPtr<FJsonObject> Entry = FNexusCapabilityResultBuilder::MakeCreatedEntry(NewEnum);
		Entry->SetNumberField(TEXT("EntryCount"),   NewEnum->NumEnums() - 1); // 减去内部 _MAX
		OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
#endif // WITH_EDITOR
	});
}

REGISTER_MCP_CAPABILITY(FNexusCreateAssetEnumCapability)
