// Copyright byteyang. All Rights Reserved.

#include "Capabilities/Asset/Input/NexusGetAssetInputMappingContextCapability.h"

#if WITH_ENHANCED_INPUT

#include "Utils/NexusCapabilityResultBuilder.h"
#include "Utils/NexusArgs.h"
#include "NexusCapabilityRegistry.h"
#include "NexusMcpSchemaBuilder.h"
#include "Utils/NexusAssetUtils.h"
#include "NexusMcpTool.h"
#include "InputMappingContext.h"
#include "EnhancedActionKeyMapping.h"

void FNexusGetAssetInputMappingContextCapability::BuildDefinition(FNexusCapabilityDefinition& Out) const
{
	Out.Name = TEXT("GetAssetInputMappingContext");
	Out.SearchAssetTypes = {TEXT("InputMappingContext")};
	Out.Description = TEXT("List all IMC Action-Key bindings and Trigger/Modifier counts. UE5+.");
	Out.InputSchema = FNexusSchema::Object()
		.Required(TEXT("AssetPath"), FNexusSchema::Str(TEXT("InputMappingContext asset path")))
		.Build();
	Out.Tags = { FNexusMcpTags::Readonly, FNexusMcpTags::Editor };
	Out.ExtraSearchKeywords = { TEXT("input"), TEXT("mapping"), TEXT("imc"), TEXT("keybind"), TEXT("context"), TEXT("Key"), TEXT("Action") };
	Out.RelatedCapabilities = { TEXT("ManageAssetInputMappingContext"), TEXT("CreateAssetInputMappingContext"), TEXT("GetAssetInputAction") };
	Out.WhenToUse = TEXT("Read all IMC Action-Key bindings");
}

FCapabilityResult FNexusGetAssetInputMappingContextCapability::Execute(const TSharedPtr<FJsonObject>& Arguments) const
{
	return FNexusCapabilityResultBuilder::Build([&](auto& OutEntries, auto& OutTop, auto& OutError)
	{
		const FNexusArgs A(Arguments);
		TSharedPtr<FJsonObject> OutEntry = MakeShared<FJsonObject>();

		const FString AssetPath = A.Str(TEXT("AssetPath"));

		UInputMappingContext* IMC = FNexusAssetUtils::LoadAssetWithFallback<UInputMappingContext>(AssetPath);
		if (!IMC)
		{
			OutError = FString::Printf(TEXT("InputMappingContext not found: %s"), *AssetPath);
			return;
		}

		OutEntry->SetStringField(TEXT("AssetType"), TEXT("InputMappingContext"));
		OutEntry->SetStringField(TEXT("name"),      IMC->GetName());
		OutEntry->SetStringField(TEXT("Path"),      FNexusAssetUtils::PackagePathOf(IMC));
		OutEntry->SetNumberField(TEXT("MappingsCount"), IMC->GetMappings().Num());

		TArray<TSharedPtr<FJsonValue>> MappingArr;
		for (const FEnhancedActionKeyMapping& M : IMC->GetMappings())
		{
			TSharedPtr<FJsonObject> MObj = MakeShared<FJsonObject>();
			// 5.0 的 Action 是裸指针，5.1+ 才是 TObjectPtr：过一道裸指针避免 .Get()
			const UInputAction* Action = M.Action;
			if (Action)
			{
				MObj->SetStringField(TEXT("Action"), FNexusAssetUtils::PackagePathOf(Action));
				MObj->SetStringField(TEXT("ActionName"), Action->GetName());
			}
			MObj->SetStringField(TEXT("Key"), M.Key.ToString());
			MObj->SetNumberField(TEXT("TriggersCount"), M.Triggers.Num());
			MObj->SetNumberField(TEXT("ModifiersCount"), M.Modifiers.Num());
			MappingArr.Add(MakeShared<FJsonValueObject>(MObj));
		}
		OutEntry->SetArrayField(TEXT("Mappings"), MappingArr);

		OutEntries.Add(MakeShared<FJsonValueObject>(OutEntry));
	});
}

REGISTER_MCP_CAPABILITY(FNexusGetAssetInputMappingContextCapability)

#endif // WITH_ENHANCED_INPUT
