// Copyright byteyang. All Rights Reserved.

#include "Capabilities/Asset/Input/NexusGetAssetInputMappingContextCapability.h"

#if WITH_ENHANCED_INPUT

#include "Utils/NexusCapabilityResultBuilder.h"
#include "NexusCapabilityRegistry.h"
#include "NexusMcpSchemaBuilder.h"
#include "Utils/NexusAssetUtils.h"
#include "NexusMcpTool.h"
#include "InputMappingContext.h"
#include "EnhancedActionKeyMapping.h"

void FGetAssetInputMappingContextCapability::BuildDefinition(FNexusCapabilityDefinition& Out) const
{
	Out.Name = TEXT("get_asset_input_mapping_context");
	Out.SearchAssetTypes = {TEXT("InputMappingContext")};
	Out.Description = TEXT("列举 InputMappingContext 全部 Action-Key 绑定及其 Trigger/Modifier 数量。UE5+。");
	Out.InputSchema = FNexusSchema::Object()
		.Required(TEXT("assetPath"), FNexusSchema::Str(TEXT("InputMappingContext 资产路径")))
		.Build();
	Out.Tags = { FNexusMcpTags::Readonly, FNexusMcpTags::Editor };
	Out.ExtraSearchKeywords = { TEXT("input"), TEXT("mapping"), TEXT("imc"), TEXT("keybind"), TEXT("context"), TEXT("key"), TEXT("action") };
	Out.RelatedCapabilities = { TEXT("manage_asset_input_mapping_context"), TEXT("create_asset_input_mapping_context"), TEXT("get_asset_input_action") };
	Out.WhenToUse = TEXT("读 IMC 的全部 Action-Key 绑定列表");
}

FCapabilityResult FGetAssetInputMappingContextCapability::Execute(const TSharedPtr<FJsonObject>& Arguments) const
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

		UInputMappingContext* IMC = FNexusAssetUtils::LoadAssetWithFallback<UInputMappingContext>(AssetPath);
		if (!IMC)
		{
			OutError = FString::Printf(TEXT("InputMappingContext 未找到: %s"), *AssetPath);
			return;
		}

		OutEntry->SetStringField(TEXT("assetType"), TEXT("InputMappingContext"));
		OutEntry->SetStringField(TEXT("name"),      IMC->GetName());
		OutEntry->SetStringField(TEXT("path"),      FNexusAssetUtils::PackagePathOf(IMC));
		OutEntry->SetNumberField(TEXT("mappingsCount"), IMC->Mappings.Num());

		TArray<TSharedPtr<FJsonValue>> MappingArr;
		for (const FEnhancedActionKeyMapping& M : IMC->Mappings)
		{
			TSharedPtr<FJsonObject> MObj = MakeShared<FJsonObject>();
			if (M.Action)
			{
				MObj->SetStringField(TEXT("action"), FNexusAssetUtils::PackagePathOf(M.Action.Get()));
				MObj->SetStringField(TEXT("actionName"), M.Action->GetName());
			}
			MObj->SetStringField(TEXT("key"), M.Key.ToString());
			MObj->SetNumberField(TEXT("triggersCount"), M.Triggers.Num());
			MObj->SetNumberField(TEXT("modifiersCount"), M.Modifiers.Num());
			MappingArr.Add(MakeShared<FJsonValueObject>(MObj));
		}
		OutEntry->SetArrayField(TEXT("mappings"), MappingArr);

		OutEntries.Add(MakeShared<FJsonValueObject>(OutEntry));
	});
}

REGISTER_MCP_CAPABILITY(FGetAssetInputMappingContextCapability)

#endif // WITH_ENHANCED_INPUT
