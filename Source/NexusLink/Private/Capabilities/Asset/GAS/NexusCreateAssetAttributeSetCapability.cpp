// Copyright byteyang. All Rights Reserved.

#include "Capabilities/Asset/GAS/NexusCreateAssetAttributeSetCapability.h"

#if WITH_GAS

#include "NexusCapabilityRegistry.h"
#include "NexusMcpSchemaBuilder.h"
#include "Utils/NexusAssetUtils.h"
#include "Utils/NexusCapabilityResultBuilder.h"
#include "AttributeSet.h"
#include "Engine/Blueprint.h"
#include "Engine/BlueprintGeneratedClass.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Misc/PackageName.h"
#include "NexusMcpTool.h"
#include "Utils/NexusVersionCompat.h"

void FCreateAssetAttributeSetCapability::BuildDefinition(FNexusCapabilityDefinition& Out) const
{
	Out.Name = TEXT("create_asset_attribute_set");
	Out.Description = TEXT("创建 AttributeSet BP；用 manage_as 设默认值，用 manage_bp 加属性变量。");
	Out.InputSchema = FNexusSchema::Object()
		.Prop(TEXT("assetPath"),   FNexusSchema::Str(TEXT("新 AS Blueprint 包路径，如 '/Game/GAS/AS_Hero'")))
		.Prop(TEXT("parentClass"), FNexusSchema::Str(TEXT("父类名（默认 AttributeSet）")))
		.Required({ TEXT("assetPath") })
		.Build();
	Out.Tags = { FNexusMcpTags::Write, FNexusMcpTags::Gas };
	Out.ExtraSearchKeywords = { TEXT("gas"), TEXT("attribute"), TEXT("attributeset"), TEXT("stats"), TEXT("health") };
	Out.RelatedCapabilities = { TEXT("get_asset_attribute_set"), TEXT("manage_asset_attribute_set"), TEXT("manage_asset_blueprint") };
	Out.WhenToUse = TEXT("创建空白 AttributeSet BP；属性变量用 manage_asset_blueprint add_variable");
}

FCapabilityResult FCreateAssetAttributeSetCapability::Execute(const TSharedPtr<FJsonObject>& Arguments) const
{
	return FNexusCapabilityResultBuilder::Build([&](auto& OutEntries, auto& OutTop, auto& OutError)
	{
		TSharedPtr<FJsonObject> OutEntry = MakeShared<FJsonObject>();

		if (!Arguments.IsValid() || !Arguments->HasField(TEXT("assetPath")))
		{ FNexusCapabilityResultBuilder::AddEntryError(OutEntries, TEXT("缺少必填参数: assetPath")); return; }

		const FString AssetPath = Arguments->GetStringField(TEXT("assetPath"));
		FString ParentClassName;
		if (!Arguments->TryGetStringField(TEXT("parentClass"), ParentClassName) || ParentClassName.IsEmpty())
			ParentClassName = TEXT("AttributeSet");

		if (FPackageName::DoesPackageExist(AssetPath))
		{ FNexusCapabilityResultBuilder::AddEntryError(OutEntries, FString::Printf(TEXT("资产已存在: %s"), *AssetPath)); return; }

		UClass* ParentClass = nullptr;
		#if NX_UE_HAS_FIND_FIRST_OBJECT
			ParentClass = FindFirstObject<UClass>(*ParentClassName, EFindFirstObjectOptions::NativeFirst);
		#else
			ParentClass = FindObject<UClass>(ANY_PACKAGE, *ParentClassName);
		#endif
		if (!ParentClass) ParentClass = LoadObject<UClass>(nullptr, *ParentClassName);
		// 允许无 UPrefix 的类名（如 "AttributeSet"）
		if (!ParentClass)
		{
			#if NX_UE_HAS_FIND_FIRST_OBJECT
				ParentClass = FindFirstObject<UClass>(*(TEXT("U") + ParentClassName), EFindFirstObjectOptions::NativeFirst);
			#else
				ParentClass = FindObject<UClass>(ANY_PACKAGE, *(TEXT("U") + ParentClassName));
			#endif
		}
		if (!ParentClass || !ParentClass->IsChildOf(UAttributeSet::StaticClass()))
		{ FNexusCapabilityResultBuilder::AddEntryError(OutEntries, FString::Printf(TEXT("父类未找到或不是 AttributeSet 子类: %s"), *ParentClassName)); return; }

		FText PackageNameError;
		if (!FPackageName::IsValidLongPackageName(AssetPath, false, &PackageNameError))
		{ FNexusCapabilityResultBuilder::AddEntryError(OutEntries, FString::Printf(TEXT("无效的包路径 '%s': %s"), *AssetPath, *PackageNameError.ToString())); return; }

		UPackage* Package = CreatePackage(*AssetPath);
		if (!Package)
		{ FNexusCapabilityResultBuilder::AddEntryError(OutEntries, FString::Printf(TEXT("创建包失败: %s"), *AssetPath)); return; }

		const FString AssetName = FPaths::GetBaseFilename(AssetPath);
		UBlueprint* NewBP = FKismetEditorUtilities::CreateBlueprint(
			ParentClass, Package, *AssetName,
			BPTYPE_Normal, UBlueprint::StaticClass(), UBlueprintGeneratedClass::StaticClass());
		if (!NewBP)
		{ FNexusCapabilityResultBuilder::AddEntryError(OutEntries, FString::Printf(TEXT("Blueprint 创建失败: %s"), *AssetPath)); return; }

		FNexusAssetUtils::NotifyCompileAndSave(Package, NewBP, AssetPath);

		OutEntry->SetStringField(TEXT("name"),      NewBP->GetName());
		OutEntry->SetStringField(TEXT("path"),      AssetPath);
		OutEntry->SetStringField(TEXT("parentClass"), ParentClass->GetName());
		OutEntry->SetBoolField(TEXT("success"),     true);
		OutEntries.Add(MakeShared<FJsonValueObject>(OutEntry));
	});
}

REGISTER_MCP_CAPABILITY(FCreateAssetAttributeSetCapability)

#endif // WITH_GAS
