// Copyright byteyang. All Rights Reserved.

#include "Capabilities/Asset/GAS/NexusCreateAssetGameplayAbilityCapability.h"

#if WITH_GAS

#include "NexusCapabilityRegistry.h"
#include "NexusMcpSchemaBuilder.h"
#include "Utils/NexusAssetUtils.h"
#include "Utils/NexusAssetEditorUtils.h"
#include "Utils/NexusCapabilityResultBuilder.h"
#include "Utils/NexusArgs.h"
#include "Abilities/GameplayAbility.h"
#include "Engine/Blueprint.h"
#include "Engine/BlueprintGeneratedClass.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Misc/PackageName.h"
#include "NexusMcpTool.h"
#include "Utils/NexusVersionCompat.h"

void FNexusCreateAssetGameplayAbilityCapability::BuildDefinition(FNexusCapabilityDefinition& Out) const
{
	Out.Name = TEXT("CreateAssetGameplayAbility");
	Out.Description = TEXT("Create GameplayAbility BP; policy/tags via manage_ga, graph via manage_bp.");
	Out.InputSchema = FNexusSchema::Object()
		.Prop(TEXT("AssetPath"),   FNexusSchema::Str(TEXT("New GA Blueprint package path, e.g. '/Game/GAS/GA_Jump'")))
		.Prop(TEXT("ParentClass"), FNexusSchema::Str(TEXT("Parent class (default GameplayAbility)")))
		.Required({ TEXT("AssetPath") })
		.Build();
	Out.Tags = { FNexusMcpTags::Write, FNexusMcpTags::Gas };
	Out.ExtraSearchKeywords = { TEXT("gas"), TEXT("ability"), TEXT("gameplay"), TEXT("ga"), TEXT("skill"), TEXT("new") };
	Out.RelatedCapabilities = { TEXT("GetAssetGameplayAbility"), TEXT("ManageAssetGameplayAbility"), TEXT("ManageAssetBlueprint") };
	Out.WhenToUse = TEXT("Create empty GA BP; graph edits via ManageAssetBlueprint");
}

FCapabilityResult FNexusCreateAssetGameplayAbilityCapability::Execute(const TSharedPtr<FJsonObject>& Arguments) const
{
	return FNexusCapabilityResultBuilder::Build([&](auto& OutEntries, auto& OutTop, auto& OutError)
	{
		const FNexusArgs A(Arguments);
		const FString AssetPath = A.Str(TEXT("AssetPath"));
		const FString ParentClassName = A.Str(TEXT("ParentClass"), TEXT("GameplayAbility"));
		const FNexusAssetEditorUtils::FAssetCreateOutcome Created = FNexusAssetEditorUtils::CreateBlueprintAsset(
			AssetPath, ParentClassName, UGameplayAbility::StaticClass());
		if (!Created.Ok())
		{
			FNexusCapabilityResultBuilder::AddEntryError(OutEntries, Created.Error);
			return;
		}
		UBlueprint* NewBP = Cast<UBlueprint>(Created.Asset);
		TSharedPtr<FJsonObject> OutEntry = MakeShared<FJsonObject>();
		OutEntry->SetStringField(TEXT("name"), NewBP->GetName());
		OutEntry->SetStringField(TEXT("Path"), AssetPath);
		if (NewBP->ParentClass)
		{
			OutEntry->SetStringField(TEXT("ParentClass"), NewBP->ParentClass->GetName());
		}
		OutEntries.Add(MakeShared<FJsonValueObject>(OutEntry));
	});
}

REGISTER_MCP_CAPABILITY(FNexusCreateAssetGameplayAbilityCapability)

#endif // WITH_GAS
