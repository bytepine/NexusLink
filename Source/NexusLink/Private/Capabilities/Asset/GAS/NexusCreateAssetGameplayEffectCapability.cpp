// Copyright byteyang. All Rights Reserved.

#include "Capabilities/Asset/GAS/NexusCreateAssetGameplayEffectCapability.h"

#if WITH_GAS

#include "NexusCapabilityRegistry.h"
#include "NexusMcpSchemaBuilder.h"
#include "Utils/NexusAssetUtils.h"
#include "Utils/NexusCapabilityResultBuilder.h"
#include "GameplayEffect.h"
#include "Engine/Blueprint.h"
#include "Engine/BlueprintGeneratedClass.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Misc/PackageName.h"
#include "NexusMcpTool.h"
#include "Utils/NexusVersionCompat.h"

void FCreateAssetGameplayEffectCapability::BuildDefinition(FNexusCapabilityDefinition& Out) const
{
	Out.Name = TEXT("create_asset_gameplay_effect");
	Out.Description = TEXT("创建 GameplayEffect BP；用 manage_ge 设 Duration/Modifier/Tag。");
	Out.InputSchema = FNexusSchema::Object()
		.Prop(TEXT("assetPath"),   FNexusSchema::Str(TEXT("新 GE Blueprint 包路径，如 '/Game/GAS/GE_Damage'")))
		.Prop(TEXT("parentClass"), FNexusSchema::Str(TEXT("父类名（默认 GameplayEffect）")))
		.Required({ TEXT("assetPath") })
		.Build();
	Out.Tags = { FNexusMcpTags::Write, FNexusMcpTags::Gas };
	Out.ExtraSearchKeywords = { TEXT("gas"), TEXT("effect"), TEXT("ge"), TEXT("damage"), TEXT("buff") };
	Out.RelatedCapabilities = { TEXT("get_asset_gameplay_effect"), TEXT("manage_asset_gameplay_effect") };
	Out.WhenToUse = TEXT("创建空白 GE BP；修改 Modifier/Duration 用 manage_asset_gameplay_effect");
}

FCapabilityResult FCreateAssetGameplayEffectCapability::Execute(const TSharedPtr<FJsonObject>& Arguments) const
{
	return FNexusCapabilityResultBuilder::Build([&](auto& OutEntries, auto& OutTop, auto& OutError)
	{
		TSharedPtr<FJsonObject> OutEntry = MakeShared<FJsonObject>();

		if (!Arguments.IsValid() || !Arguments->HasField(TEXT("assetPath")))
		{ FNexusCapabilityResultBuilder::AddEntryError(OutEntries, TEXT("缺少必填参数: assetPath")); return; }

		const FString AssetPath = Arguments->GetStringField(TEXT("assetPath"));
		FString ParentClassName;
		if (!Arguments->TryGetStringField(TEXT("parentClass"), ParentClassName) || ParentClassName.IsEmpty())
			ParentClassName = TEXT("GameplayEffect");

		if (FPackageName::DoesPackageExist(AssetPath))
		{ FNexusCapabilityResultBuilder::AddEntryError(OutEntries, FString::Printf(TEXT("资产已存在: %s"), *AssetPath)); return; }

		UClass* ParentClass = nullptr;
		#if NX_UE_HAS_FIND_FIRST_OBJECT
			ParentClass = FindFirstObject<UClass>(*ParentClassName, EFindFirstObjectOptions::NativeFirst);
		#else
			ParentClass = FindObject<UClass>(ANY_PACKAGE, *ParentClassName);
		#endif
		if (!ParentClass) ParentClass = LoadObject<UClass>(nullptr, *ParentClassName);
		if (!ParentClass || !ParentClass->IsChildOf(UGameplayEffect::StaticClass()))
		{ FNexusCapabilityResultBuilder::AddEntryError(OutEntries, FString::Printf(TEXT("父类未找到或不是 GameplayEffect 子类: %s"), *ParentClassName)); return; }

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

REGISTER_MCP_CAPABILITY(FCreateAssetGameplayEffectCapability)

#endif // WITH_GAS
