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

// ── 辅助：将对象指针数组序列化为类名列表 ─────────────────────────────────────
// 5.0 的 Triggers/Modifiers 是裸指针数组，5.1+ 才换成 TObjectPtr，故两个重载都要有。

template<typename T>
static TArray<TSharedPtr<FJsonValue>> BuildClassNameArray(const TArray<T*>& Items)
{
	TArray<TSharedPtr<FJsonValue>> Result;
	for (const T* Item : Items)
	{
		if (Item)
		{
			Result.Add(MakeShared<FJsonValueString>(Item->GetClass()->GetName()));
		}
	}
	return Result;
}

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

void FNexusGetAssetInputActionCapability::BuildDefinition(FNexusCapabilityDefinition& Out) const
{
	Out.Name = TEXT("GetAssetInputAction");
	Out.SearchAssetTypes = {TEXT("InputAction")};
	Out.Description = TEXT("Read InputAction config: ValueType/Trigger/Modifier/flags. UE5+.");
	Out.InputSchema = FNexusSchema::Object()
		.Required(TEXT("AssetPath"), FNexusSchema::Str(TEXT("InputAction asset path")))
		.Build();
	Out.Tags = { FNexusMcpTags::Readonly, FNexusMcpTags::Editor };
	Out.ExtraSearchKeywords = { TEXT("input"), TEXT("Action"), TEXT("ia"), TEXT("enhanced"), TEXT("Trigger"), TEXT("modifier"), TEXT("axis") };
	Out.RelatedCapabilities = { TEXT("ManageAssetInputAction"), TEXT("CreateAssetInputAction"), TEXT("GetAssetInputMappingContext") };
	Out.WhenToUse = TEXT("Read InputAction ValueType, Trigger/Modifier class lists, flags");
}

FCapabilityResult FNexusGetAssetInputActionCapability::Execute(const TSharedPtr<FJsonObject>& Arguments) const
{
	return FNexusCapabilityResultBuilder::Build([&](auto& OutEntries, auto& OutTop, auto& OutError)
	{
		const FNexusArgs A(Arguments);
		TSharedPtr<FJsonObject> OutEntry = MakeShared<FJsonObject>();

		const FString AssetPath = A.Str(TEXT("AssetPath"));

		UInputAction* IA = FNexusAssetUtils::LoadAssetWithFallback<UInputAction>(AssetPath);
		if (!IA)
		{
			OutError = FString::Printf(TEXT("InputAction not found: %s"), *AssetPath);
			return;
		}

		OutEntry->SetStringField(TEXT("AssetType"),  TEXT("InputAction"));
		OutEntry->SetStringField(TEXT("name"),       IA->GetName());
		OutEntry->SetStringField(TEXT("Path"),       FNexusAssetUtils::PackagePathOf(IA));
		OutEntry->SetStringField(TEXT("ValueType"),  InputActionValueTypeToStr(IA->ValueType));
		OutEntry->SetBoolField(TEXT("bConsumesInput"),    IA->bConsumeInput);
		OutEntry->SetBoolField(TEXT("bReserveAllMappings"), IA->bReserveAllMappings);

		// Triggers
		OutEntry->SetNumberField(TEXT("TriggersCount"), IA->Triggers.Num());
		if (IA->Triggers.Num() > 0)
		{
			OutEntry->SetArrayField(TEXT("Triggers"), BuildClassNameArray(IA->Triggers));
		}

		// Modifiers
		OutEntry->SetNumberField(TEXT("ModifiersCount"), IA->Modifiers.Num());
		if (IA->Modifiers.Num() > 0)
		{
			OutEntry->SetArrayField(TEXT("Modifiers"), BuildClassNameArray(IA->Modifiers));
		}

		OutEntries.Add(MakeShared<FJsonValueObject>(OutEntry));
	});
}

REGISTER_MCP_CAPABILITY(FNexusGetAssetInputActionCapability)

#endif // WITH_ENHANCED_INPUT
