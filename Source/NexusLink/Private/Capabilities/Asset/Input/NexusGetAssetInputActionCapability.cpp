// Copyright byteyang. All Rights Reserved.

#include "Capabilities/Asset/Input/NexusGetAssetInputActionCapability.h"

#if WITH_ENHANCED_INPUT

#include "Utils/NexusCapabilityResultBuilder.h"
#include "Utils/NexusArgs.h"
#include "NexusCapabilityRegistry.h"
#include "NexusMcpSchemaBuilder.h"
#include "Utils/NexusAssetUtils.h"
#include "NexusMcpTool.h"
#include "InputAction.h"
#include "InputModifiers.h"
#include "InputTriggers.h"

// ── 辅助：ValueType 枚举转字符串 ─────────────────────────────────────────────

static FString InputActionValueTypeToStr(EInputActionValueType Type)
{
	switch (Type)
	{
		case EInputActionValueType::Boolean: return TEXT("Boolean");
		case EInputActionValueType::Axis1D:  return TEXT("Axis1D");
		case EInputActionValueType::Axis2D:  return TEXT("Axis2D");
		case EInputActionValueType::Axis3D:  return TEXT("Axis3D");
		default:                             return TEXT("Unknown");
	}
}

// ── 辅助：将 TObjectPtr<UObject> 数组序列化为类名列表 ─────────────────────────

template<typename T>
static TArray<TSharedPtr<FJsonValue>> BuildClassNameArray(const TArray<TObjectPtr<T>>& Items)
{
	TArray<TSharedPtr<FJsonValue>> Result;
	for (const TObjectPtr<T>& Item : Items)
	{
		if (Item)
		{
			Result.Add(MakeShared<FJsonValueString>(Item->GetClass()->GetName()));
		}
	}
	return Result;
}

// ── Capability ────────────────────────────────────────────────────────────────

void FGetAssetInputActionCapability::BuildDefinition(FNexusCapabilityDefinition& Out) const
{
	Out.Name = TEXT("get_asset_input_action");
	Out.SearchAssetTypes = {TEXT("InputAction")};
	Out.Description = TEXT("Read InputAction config: ValueType/Trigger/Modifier/flags. UE5+.");
	Out.InputSchema = FNexusSchema::Object()
		.Required(TEXT("assetPath"), FNexusSchema::Str(TEXT("InputAction asset path")))
		.Build();
	Out.Tags = { FNexusMcpTags::Readonly, FNexusMcpTags::Editor };
	Out.ExtraSearchKeywords = { TEXT("input"), TEXT("action"), TEXT("ia"), TEXT("enhanced"), TEXT("trigger"), TEXT("modifier"), TEXT("axis") };
	Out.RelatedCapabilities = { TEXT("manage_asset_input_action"), TEXT("create_asset_input_action"), TEXT("get_asset_input_mapping_context") };
	Out.WhenToUse = TEXT("Read InputAction ValueType, Trigger/Modifier class lists, flags");
}

FCapabilityResult FGetAssetInputActionCapability::Execute(const TSharedPtr<FJsonObject>& Arguments) const
{
	return FNexusCapabilityResultBuilder::Build([&](auto& OutEntries, auto& OutTop, auto& OutError)
	{
		const FNexusArgs A(Arguments);
		TSharedPtr<FJsonObject> OutEntry = MakeShared<FJsonObject>();

		const FString AssetPath = A.Str(TEXT("assetPath"));

		UInputAction* IA = FNexusAssetUtils::LoadAssetWithFallback<UInputAction>(AssetPath);
		if (!IA)
		{
			OutError = FString::Printf(TEXT("InputAction not found: %s"), *AssetPath);
			return;
		}

		OutEntry->SetStringField(TEXT("assetType"),  TEXT("InputAction"));
		OutEntry->SetStringField(TEXT("name"),       IA->GetName());
		OutEntry->SetStringField(TEXT("path"),       FNexusAssetUtils::PackagePathOf(IA));
		OutEntry->SetStringField(TEXT("valueType"),  InputActionValueTypeToStr(IA->ValueType));
		OutEntry->SetBoolField(TEXT("consumesInput"),    IA->bConsumesInput);
		OutEntry->SetBoolField(TEXT("reserveAllMappings"), IA->bReserveAllMappings);

		// Triggers
		OutEntry->SetNumberField(TEXT("triggersCount"), IA->Triggers.Num());
		if (IA->Triggers.Num() > 0)
		{
			OutEntry->SetArrayField(TEXT("triggers"), BuildClassNameArray(IA->Triggers));
		}

		// Modifiers
		OutEntry->SetNumberField(TEXT("modifiersCount"), IA->Modifiers.Num());
		if (IA->Modifiers.Num() > 0)
		{
			OutEntry->SetArrayField(TEXT("modifiers"), BuildClassNameArray(IA->Modifiers));
		}

		OutEntries.Add(MakeShared<FJsonValueObject>(OutEntry));
	});
}

REGISTER_MCP_CAPABILITY(FGetAssetInputActionCapability)

#endif // WITH_ENHANCED_INPUT
