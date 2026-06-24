// Copyright byteyang. All Rights Reserved.

#include "Capabilities/Asset/GAS/NexusCreateAssetGameplayAbilityCapability.h"

#if WITH_GAS

#include "NexusCapabilityRegistry.h"
#include "NexusMcpSchemaBuilder.h"
#include "Utils/NexusAssetUtils.h"
#include "Utils/NexusCapabilityResultBuilder.h"
#include "Abilities/GameplayAbility.h"
#include "Engine/Blueprint.h"
#include "Engine/BlueprintGeneratedClass.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Misc/PackageName.h"
#include "NexusMcpTool.h"
#include "Utils/NexusVersionCompat.h"

void FCreateAssetGameplayAbilityCapability::BuildDefinition(FNexusCapabilityDefinition& Out) const
{
	Out.Name = TEXT("create_asset_gameplay_ability");
	Out.Description = TEXT("创建 GameplayAbility BP；用 manage_ga 设策略/Tag，manage_bp 编辑 Graph。");
	Out.InputSchema = FNexusSchema::Object()
		.Prop(TEXT("assetPath"),   FNexusSchema::Str(TEXT("新 GA Blueprint 包路径，如 '/Game/GAS/GA_Jump'")))
		.Prop(TEXT("parentClass"), FNexusSchema::Str(TEXT("父类名（默认 GameplayAbility）")))
		.Required({ TEXT("assetPath") })
		.Build();
	Out.Tags = { FNexusMcpTags::Write, FNexusMcpTags::Gas };
	Out.ExtraSearchKeywords = { TEXT("gas"), TEXT("ability"), TEXT("gameplay"), TEXT("ga"), TEXT("skill"), TEXT("new") };
	Out.RelatedCapabilities = { TEXT("get_asset_gameplay_ability"), TEXT("manage_asset_gameplay_ability"), TEXT("manage_asset_blueprint") };
	Out.WhenToUse = TEXT("创建空白 GA BP；Graph 编辑用 manage_asset_blueprint");
}

FCapabilityResult FCreateAssetGameplayAbilityCapability::Execute(const TSharedPtr<FJsonObject>& Arguments) const
{
	return FNexusCapabilityResultBuilder::Build([&](auto& OutEntries, auto& OutTop, auto& OutError)
	{
		TSharedPtr<FJsonObject> OutEntry = MakeShared<FJsonObject>();

		if (!Arguments.IsValid() || !Arguments->HasField(TEXT("assetPath")))
		{ FNexusCapabilityResultBuilder::AddEntryError(OutEntries, TEXT("缺少必填参数: assetPath")); return; }

		const FString AssetPath = Arguments->GetStringField(TEXT("assetPath"));
		FString ParentClassName;
		if (!Arguments->TryGetStringField(TEXT("parentClass"), ParentClassName) || ParentClassName.IsEmpty())
			ParentClassName = TEXT("GameplayAbility");

		if (FPackageName::DoesPackageExist(AssetPath))
		{ FNexusCapabilityResultBuilder::AddEntryError(OutEntries, FString::Printf(TEXT("资产已存在: %s"), *AssetPath)); return; }

		// 查找父类
		UClass* ParentClass = nullptr;
		#if NX_UE_HAS_FIND_FIRST_OBJECT
			ParentClass = FindFirstObject<UClass>(*ParentClassName, EFindFirstObjectOptions::NativeFirst);
		#else
			ParentClass = FindObject<UClass>(ANY_PACKAGE, *ParentClassName);
		#endif
		if (!ParentClass) ParentClass = LoadObject<UClass>(nullptr, *ParentClassName);
		if (!ParentClass || !ParentClass->IsChildOf(UGameplayAbility::StaticClass()))
		{ FNexusCapabilityResultBuilder::AddEntryError(OutEntries, FString::Printf(TEXT("父类未找到或不是 GameplayAbility 子类: %s"), *ParentClassName)); return; }

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

REGISTER_MCP_CAPABILITY(FCreateAssetGameplayAbilityCapability)

#endif // WITH_GAS
