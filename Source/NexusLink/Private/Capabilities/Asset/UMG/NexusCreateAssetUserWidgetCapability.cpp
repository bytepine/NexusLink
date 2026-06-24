// Copyright byteyang. All Rights Reserved.

#include "Capabilities/Asset/UMG/NexusCreateAssetUserWidgetCapability.h"
#include "Utils/NexusCapabilityResultBuilder.h"
#include "NexusCapabilityRegistry.h"
#include "NexusMcpSchemaBuilder.h"
#include "Utils/NexusAssetUtils.h"
#if WITH_EDITOR
#include "Kismet2/KismetEditorUtilities.h"
#endif
#include "AssetRegistry/AssetRegistryModule.h"
#include "Misc/PackageName.h"
#if WITH_EDITOR
#include "WidgetBlueprint.h"
#endif
#include "Blueprint/UserWidget.h"
#include "NexusMcpTool.h"

void FCreateAssetUserWidgetCapability::BuildDefinition(FNexusCapabilityDefinition& Out) const
{
	Out.Name = TEXT("create_asset_user_widget");
	Out.Description = TEXT("创建 WBP。parentClass 设 UI 基类；用 manage 填控件树。");
	Out.InputSchema = FNexusSchema::Object()
		.Prop(TEXT("assetPath"),   FNexusSchema::Str(TEXT("新 WidgetBlueprint 包路径")))
		.Prop(TEXT("parentClass"), FNexusSchema::Str(TEXT("父类名（默认 UserWidget）")))
		.Required({ TEXT("assetPath") })
		.Build();
	Out.Tags = {FNexusMcpTags::Write, FNexusMcpTags::Widget };
	Out.ExtraSearchKeywords = { TEXT("wbp"), TEXT("umg"), TEXT("new"), TEXT("ui"), TEXT("panel") };
	Out.RelatedCapabilities = { TEXT("manage_asset_user_widget"), TEXT("get_asset_user_widget") };
	Out.WhenToUse = TEXT("创建空白 WBP；parentClass 可选（默认 UserWidget）");
}

FCapabilityResult FCreateAssetUserWidgetCapability::Execute(const TSharedPtr<FJsonObject>& Arguments) const
{
#if WITH_EDITOR
	return FNexusCapabilityResultBuilder::Build([&](auto& OutEntries, auto& OutTop, auto& OutError)
	{

		if (!Arguments.IsValid())
		{
			OutError = TEXT("参数无效");
			return;
		}

		FString AssetPath;
		if (!Arguments->TryGetStringField(TEXT("assetPath"), AssetPath) || AssetPath.IsEmpty())
		{
			OutError = TEXT("assetPath 为必填项");
			return;
		}

		FText PathErrText;
		if (!FPackageName::IsValidLongPackageName(AssetPath, false, &PathErrText))
		{
			OutError = FString::Printf(TEXT("无效的 assetPath: %s"), *PathErrText.ToString());
			return;
		}

		if (FPackageName::DoesPackageExist(AssetPath))
		{
			OutError = FString::Printf(TEXT("资产已存在: %s"), *AssetPath);
			return;
		}

		const FString AssetName = FPaths::GetBaseFilename(AssetPath);

		FString ParentClassName = TEXT("UserWidget");
		Arguments->TryGetStringField(TEXT("parentClass"), ParentClassName);

	#if NX_UE_HAS_FIND_FIRST_OBJECT
		UClass* ParentClass = FindFirstObject<UClass>(*ParentClassName, EFindFirstObjectOptions::None, ELogVerbosity::Warning, TEXT("ParentClass"));
	#else
		UClass* ParentClass = FindObject<UClass>(ANY_PACKAGE, *ParentClassName);
	#endif
		if (!ParentClass) ParentClass = LoadObject<UClass>(nullptr, *ParentClassName);
		if (!ParentClass)
		{
			OutError = FString::Printf(TEXT("父类未找到: %s"), *ParentClassName);
			return;
		}
		if (!ParentClass->IsChildOf(UUserWidget::StaticClass()))
		{
			OutError = FString::Printf(TEXT("%s 不是 UserWidget 子类"), *ParentClassName);
			return;
		}

		UPackage* Package = CreatePackage(*AssetPath);
		if (!Package)
		{
			OutError = TEXT("创建包失败");
			return;
		}

		UBlueprint* NewBP = FKismetEditorUtilities::CreateBlueprint(
			ParentClass, Package, *AssetName,
			BPTYPE_Normal, UWidgetBlueprint::StaticClass(), UBlueprintGeneratedClass::StaticClass()
		);
		if (!NewBP)
		{
			OutError = TEXT("WidgetBlueprint 创建失败");
			return;
		}

		FNexusAssetUtils::NotifyCompileAndSave(Package, NewBP, AssetPath);

		TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
		Entry->SetStringField(TEXT("path"),        NewBP->GetOutermost()->GetName());
		Entry->SetStringField(TEXT("parentClass"), ParentClass->GetName());
		Entry->SetBoolField(TEXT("success"),       true);
		OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
	
	});
#else
	return FNexusCapabilityResultBuilder::Build([&](auto& OutEntries, auto& OutTop, auto& OutError)
	{
		OutError = TEXT("create_asset_user_widget 仅在编辑器构建可用");
	});
#endif
}

REGISTER_MCP_CAPABILITY(FCreateAssetUserWidgetCapability)
