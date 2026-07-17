// Copyright byteyang. All Rights Reserved.

#include "Capabilities/Asset/Input/NexusCreateAssetInputActionCapability.h"

#if WITH_ENHANCED_INPUT

#include "Utils/NexusCapabilityResultBuilder.h"
#include "NexusCapabilityRegistry.h"
#include "NexusMcpSchemaBuilder.h"
#include "Utils/NexusAssetUtils.h"
#include "NexusMcpTool.h"
#include "InputAction.h"

void FCreateAssetInputActionCapability::BuildDefinition(FNexusCapabilityDefinition& Out) const
{
	Out.Name = TEXT("create_asset_input_action");
	Out.Description = TEXT("创建空白 UInputAction。指定 valueType 后可用 manage 添加 Trigger/Modifier。");
	Out.InputSchema = FNexusSchema::Object()
		.Required(TEXT("assetPath"), FNexusSchema::Str(TEXT("资产包路径（/Game/…/IA_Jump）")))
		.Prop(TEXT("valueType"), FNexusSchema::Enum(
			TEXT("返回值类型"),
			{ TEXT("Boolean"), TEXT("Axis1D"), TEXT("Axis2D"), TEXT("Axis3D") },
			TEXT("Boolean")))
		.Build();
	Out.Tags = { FNexusMcpTags::Write, FNexusMcpTags::Editor };
	Out.ExtraSearchKeywords = { TEXT("input"), TEXT("action"), TEXT("ia"), TEXT("enhanced"), TEXT("trigger"), TEXT("axis") };
	Out.RelatedCapabilities = { TEXT("get_asset_input_action"), TEXT("manage_asset_input_action"), TEXT("create_asset_input_mapping_context") };
	Out.WhenToUse = TEXT("新建 InputAction 资产；之后用 manage 配置 Trigger/Modifier");
}

FCapabilityResult FCreateAssetInputActionCapability::Execute(const TSharedPtr<FJsonObject>& Arguments) const
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

		if (LoadObject<UInputAction>(nullptr, *AssetPath))
		{
			OutEntry->SetStringField(TEXT("error"), FString::Printf(TEXT("InputAction 已存在: %s"), *AssetPath));
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

		UInputAction* IA = NewObject<UInputAction>(Pkg, *AssetName, RF_Public | RF_Standalone);
		if (!IA)
		{
			FNexusCapabilityResultBuilder::AddEntryError(OutEntries, TEXT("InputAction 创建失败"));
			return;
		}

		// 设置 ValueType
		FString ValueTypeStr;
		if (Arguments->TryGetStringField(TEXT("valueType"), ValueTypeStr))
		{
			if (ValueTypeStr == TEXT("Axis1D"))
				IA->ValueType = EInputActionValueType::Axis1D;
			else if (ValueTypeStr == TEXT("Axis2D"))
				IA->ValueType = EInputActionValueType::Axis2D;
			else if (ValueTypeStr == TEXT("Axis3D"))
				IA->ValueType = EInputActionValueType::Axis3D;
			else
				IA->ValueType = EInputActionValueType::Boolean;
		}

		FNexusAssetUtils::NotifyAndSaveCreated(Pkg, IA, AssetPath);

		OutEntry->SetStringField(TEXT("name"), IA->GetName());
		OutEntry->SetStringField(TEXT("path"), FNexusAssetUtils::PackagePathOf(IA));
		OutEntries.Add(MakeShared<FJsonValueObject>(OutEntry));
	});
}

REGISTER_MCP_CAPABILITY(FCreateAssetInputActionCapability)

#endif // WITH_ENHANCED_INPUT
