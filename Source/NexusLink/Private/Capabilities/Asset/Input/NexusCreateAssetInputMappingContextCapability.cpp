// Copyright byteyang. All Rights Reserved.

#include "Capabilities/Asset/Input/NexusCreateAssetInputMappingContextCapability.h"

#if WITH_ENHANCED_INPUT

#include "Utils/NexusCapabilityResultBuilder.h"
#include "NexusCapabilityRegistry.h"
#include "NexusMcpSchemaBuilder.h"
#include "Utils/NexusAssetUtils.h"
#include "NexusMcpTool.h"
#include "InputMappingContext.h"

void FCreateAssetInputMappingContextCapability::BuildDefinition(FNexusCapabilityDefinition& Out) const
{
	Out.Name = TEXT("create_asset_input_mapping_context");
	Out.Description = TEXT("创建空白 UInputMappingContext。用 manage 绑定 Action 与按键。");
	Out.InputSchema = FNexusSchema::Object()
		.Required(TEXT("assetPath"), FNexusSchema::Str(TEXT("资产包路径（/Game/…/IMC_Default）")))
		.Build();
	Out.Tags = { FNexusMcpTags::Write, FNexusMcpTags::Editor };
	Out.ExtraSearchKeywords = { TEXT("input"), TEXT("mapping"), TEXT("imc"), TEXT("enhanced"), TEXT("keybind"), TEXT("context") };
	Out.RelatedCapabilities = { TEXT("get_asset_input_mapping_context"), TEXT("manage_asset_input_mapping_context"), TEXT("create_asset_input_action") };
	Out.WhenToUse = TEXT("新建 InputMappingContext；之后用 manage 添加 Action-Key 绑定");
}

FCapabilityResult FCreateAssetInputMappingContextCapability::Execute(const TSharedPtr<FJsonObject>& Arguments) const
{
	return FNexusCapabilityResultBuilder::Build([&](auto& OutEntries, auto& OutTop, auto& OutError)
	{
		TSharedPtr<FJsonObject> OutEntry = MakeShared<FJsonObject>();

		FString AssetPath;
		if (!Arguments->TryGetStringField(TEXT("assetPath"), AssetPath) || AssetPath.IsEmpty())
		{
			OutError = TEXT("assetPath 为必填项");
			return;
		}

		if (LoadObject<UInputMappingContext>(nullptr, *AssetPath))
		{
			OutEntry->SetStringField(TEXT("error"), FString::Printf(TEXT("InputMappingContext 已存在: %s"), *AssetPath));
			OutEntries.Add(MakeShared<FJsonValueObject>(OutEntry));
			return;
		}

		const FString AssetName = FPaths::GetBaseFilename(AssetPath);
		UPackage* Pkg = CreatePackage(*AssetPath);
		if (!Pkg)
		{
			FNexusCapabilityResultBuilder::AddEntryError(OutEntries, TEXT("创建 Package 失败"));
			return;
		}

		UInputMappingContext* IMC = NewObject<UInputMappingContext>(Pkg, *AssetName, RF_Public | RF_Standalone);
		if (!IMC)
		{
			FNexusCapabilityResultBuilder::AddEntryError(OutEntries, TEXT("InputMappingContext 创建失败"));
			return;
		}

		FNexusAssetUtils::NotifyAndSaveCreated(Pkg, IMC, AssetPath);

		OutEntry->SetStringField(TEXT("name"), IMC->GetName());
		OutEntry->SetStringField(TEXT("path"), FNexusAssetUtils::PackagePathOf(IMC));
		OutEntry->SetBoolField(TEXT("success"), true);
		OutEntries.Add(MakeShared<FJsonValueObject>(OutEntry));
	});
}

REGISTER_MCP_CAPABILITY(FCreateAssetInputMappingContextCapability)

#endif // WITH_ENHANCED_INPUT
