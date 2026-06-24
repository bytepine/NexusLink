// Copyright byteyang. All Rights Reserved.

#include "Capabilities/Asset/Blueprint/NexusCreateAssetBlueprintCapability.h"
#include "Utils/NexusCapabilityResultBuilder.h"
#include "NexusCapabilityRegistry.h"
#include "NexusMcpSchemaBuilder.h"
#include "Utils/NexusAssetUtils.h"
#if WITH_EDITOR
#include "Kismet2/KismetEditorUtilities.h"
#endif
#include "AssetRegistry/AssetRegistryModule.h"
#include "Engine/Blueprint.h"
#include "NexusMcpTool.h"

void FCreateAssetBlueprintCapability::BuildDefinition(FNexusCapabilityDefinition& Out) const
{
	Out.Name = TEXT("create_asset_blueprint");
	Out.Description = TEXT("创建新 BP 资产，自动编译；用 manage 添加变量/节点/连线。");
	Out.InputSchema = FNexusSchema::Object()
		.Prop(TEXT("assetPath"),   FNexusSchema::Str(TEXT("新蓝图包路径，如 '/Game/Blueprints/BP_NewActor'")))
		.Prop(TEXT("parentClass"), FNexusSchema::Str(TEXT("父类名（任意 UObject 子类），如 Actor、Pawn、Character")))
		.Required({ TEXT("assetPath"), TEXT("parentClass") })
		.Build();
	Out.Tags = {FNexusMcpTags::Write, FNexusMcpTags::Blueprint };
	Out.ExtraSearchKeywords = { TEXT("bp"), TEXT("new"), TEXT("subclass"), TEXT("derive"), TEXT("parent") };
	Out.RelatedCapabilities = { TEXT("manage_asset_blueprint"), TEXT("get_asset_blueprint") };
	Out.WhenToUse = TEXT("创建空白 BP；不用于编辑现有 BP");
}

FCapabilityResult FCreateAssetBlueprintCapability::Execute(const TSharedPtr<FJsonObject>& Arguments) const
{
#if WITH_EDITOR
	return FNexusCapabilityResultBuilder::Build([&](auto& OutEntries, auto& OutTop, auto& OutError)
	{

		TSharedPtr<FJsonObject> OutEntry = MakeShared<FJsonObject>();

		if (!Arguments.IsValid() || !Arguments->HasField(TEXT("assetPath")) || !Arguments->HasField(TEXT("parentClass")))
		{ FNexusCapabilityResultBuilder::AddEntryError(OutEntries, TEXT("缺少必填参数: assetPath, parentClass")); return; }

		const FString AssetPath = Arguments->GetStringField(TEXT("assetPath"));
		const FString ParentClassName = Arguments->GetStringField(TEXT("parentClass"));

		// AssetRegistry 覆盖已存在但未加载的包，LoadObject 只能检测已在内存中的对象
		if (FPackageName::DoesPackageExist(AssetPath))
		{ FNexusCapabilityResultBuilder::AddEntryError(OutEntries, FString::Printf(TEXT("Blueprint already exists: %s"), *AssetPath)); return; }

		auto TryFindClass = [](const FString& Name) -> UClass*
		{
			UClass* C = nullptr;
	#if NX_UE_HAS_FIND_FIRST_OBJECT
			C = FindFirstObject<UClass>(*Name, EFindFirstObjectOptions::NativeFirst);
	#else
			C = FindObject<UClass>(ANY_PACKAGE, *Name);
	#endif
			if (!C) C = LoadObject<UClass>(nullptr, *Name);
			return C;
		};

		UClass* ParentClass = TryFindClass(ParentClassName);
		if (!ParentClass) ParentClass = TryFindClass(TEXT("A") + ParentClassName);
		if (!ParentClass)
		{ FNexusCapabilityResultBuilder::AddEntryError(OutEntries, FString::Printf(TEXT("父类未找到: %s"), *ParentClassName)); return; }
		if (!ParentClass->IsChildOf(UObject::StaticClass()))
		{ FNexusCapabilityResultBuilder::AddEntryError(OutEntries, FString::Printf(TEXT("无效的父类（非 UObject 子类）: %s"), *ParentClass->GetName())); return; }

		const FString AssetName = FPaths::GetBaseFilename(AssetPath);
		// 校验包名合法性，提前给出具体原因（非法字符 / 路径挂载点不存在等）
		FText PackageNameError;
		if (!FPackageName::IsValidLongPackageName(AssetPath, false, &PackageNameError))
		{ FNexusCapabilityResultBuilder::AddEntryError(OutEntries, FString::Printf(TEXT("无效的包路径 '%s': %s"), *AssetPath, *PackageNameError.ToString())); return; }
		UPackage* Package = CreatePackage(*AssetPath);
		if (!Package)
		{ FNexusCapabilityResultBuilder::AddEntryError(OutEntries, FString::Printf(TEXT("创建包失败: %s"), *AssetPath)); return; }

		UBlueprint* NewBlueprint = FKismetEditorUtilities::CreateBlueprint(
			ParentClass, Package, *AssetName,
			BPTYPE_Normal, UBlueprint::StaticClass(), UBlueprintGeneratedClass::StaticClass()
		);
		if (!NewBlueprint)
		{ FNexusCapabilityResultBuilder::AddEntryError(OutEntries, FString::Printf(TEXT("Blueprint 创建失败: %s"), *AssetPath)); return; }

		FNexusAssetUtils::NotifyCompileAndSave(Package, NewBlueprint, AssetPath);

		OutEntry->SetStringField(TEXT("name"),    NewBlueprint->GetName());
		OutEntry->SetBoolField(TEXT("success"),   true);
		OutEntries.Add(MakeShared<FJsonValueObject>(OutEntry));
	
	});
#else
	return FNexusCapabilityResultBuilder::Build([&](auto& OutEntries, auto& OutTop, auto& OutError)
	{
		OutError = TEXT("create_asset_blueprint 仅在编辑器构建可用");
	});
#endif
}

REGISTER_MCP_CAPABILITY(FCreateAssetBlueprintCapability)
