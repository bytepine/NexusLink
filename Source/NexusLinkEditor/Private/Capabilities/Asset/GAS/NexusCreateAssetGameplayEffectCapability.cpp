// Copyright byteyang. All Rights Reserved.

#include "Capabilities/Asset/GAS/NexusCreateAssetGameplayEffectCapability.h"

#if WITH_GAS

#include "NexusCapabilityRegistry.h"
#include "NexusMcpSchemaBuilder.h"
#include "Utils/NexusAssetUtils.h"
#include "Utils/NexusAssetEditorUtils.h"
#include "Utils/NexusCapabilityResultBuilder.h"
#include "Utils/NexusArgs.h"
#include "GameplayEffect.h"
#include "Engine/Blueprint.h"
#include "NexusMcpTool.h"

void FNexusCreateAssetGameplayEffectCapability::BuildDefinition(FNexusCapabilityDefinition& Out) const
{
	Out.Name = TEXT("CreateAssetGameplayEffect");
	Out.Description = TEXT("Create GameplayEffect BP; Duration/Modifier/Tag via manage_ge.");
	Out.InputSchema = FNexusSchema::Object()
		.Prop(TEXT("AssetPath"),   FNexusSchema::Str(TEXT("New GE Blueprint package path, e.g. '/Game/GAS/GE_Damage'")))
		.Prop(TEXT("ParentClass"), FNexusSchema::Str(TEXT("Parent class (default GameplayEffect)")))
		.Required({ TEXT("AssetPath") })
		.Build();
	Out.Tags = { FNexusMcpTags::Write, FNexusMcpTags::Gas };
	Out.ExtraSearchKeywords = { TEXT("gas"), TEXT("effect"), TEXT("ge"), TEXT("damage"), TEXT("buff") };
	Out.RelatedCapabilities = { TEXT("GetAssetGameplayEffect"), TEXT("ManageAssetGameplayEffect") };
	Out.WhenToUse = TEXT("Create empty GE BP; edit Modifier/Duration via ManageAssetGameplayEffect");
}

FCapabilityResult FNexusCreateAssetGameplayEffectCapability::Execute(const TSharedPtr<FJsonObject>& Arguments) const
{
	return FNexusCapabilityResultBuilder::Build([&](auto& OutEntries, auto& OutTop, auto& OutError)
	{
		const FNexusArgs A(Arguments);
		const FString AssetPath = A.Str(TEXT("AssetPath"));
		const FString ParentClassName = A.Str(TEXT("ParentClass"), TEXT("GameplayEffect"));
		const FNexusAssetEditorUtils::FAssetCreateOutcome Created = FNexusAssetEditorUtils::CreateBlueprintAsset(
			AssetPath, ParentClassName, UGameplayEffect::StaticClass());
		if (!Created.Ok())
		{
			FNexusCapabilityResultBuilder::AddEntryError(OutEntries, Created.Error);
			return;
		}
		UBlueprint* NewBP = Cast<UBlueprint>(Created.Asset);
		TSharedPtr<FJsonObject> OutEntry = MakeShared<FJsonObject>();
		OutEntry->SetStringField(TEXT("name"),      NewBP->GetName());
		OutEntry->SetStringField(TEXT("Path"),      AssetPath);
		if (NewBP->ParentClass)
		{
			OutEntry->SetStringField(TEXT("ParentClass"), NewBP->ParentClass->GetName());
		}
		OutEntries.Add(MakeShared<FJsonValueObject>(OutEntry));
	});
}

REGISTER_MCP_CAPABILITY(FNexusCreateAssetGameplayEffectCapability)

#endif // WITH_GAS
