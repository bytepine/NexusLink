// Copyright byteyang. All Rights Reserved.

#include "Utils/NexusGameplayTagReferencerUtils.h"
#include "Utils/NexusVersionCompat.h"
#include "GameplayTagsManager.h"
#include "GameplayTagContainer.h"
#include "AssetRegistry/AssetRegistryModule.h"

#if NX_UE_HAS_TAG_SEARCHABLE_REFERENCERS
#include "AssetRegistry/IAssetRegistry.h"
#endif

bool FNexusGameplayTagReferencerUtils::FindReferencerPackagePaths(
	const FString& TagName, TArray<FString>& OutPaths, FString& OutError)
{
	if (TagName.IsEmpty())
	{
		OutError = TEXT("tag is required");
		return false;
	}

	const FGameplayTag Tag = UGameplayTagsManager::Get().RequestGameplayTag(FName(*TagName), false);
	if (!Tag.IsValid())
	{
		OutError = FString::Printf(TEXT("GameplayTag not found: '%s'"), *TagName);
		return false;
	}

#if NX_UE_HAS_TAG_SEARCHABLE_REFERENCERS
	FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"));
	IAssetRegistry& Registry = AssetRegistryModule.Get();

	const FAssetIdentifier TagIdentifier(FGameplayTag::StaticStruct(), Tag.GetTagName());
	TArray<FAssetIdentifier> Referencers;
	Registry.GetReferencers(TagIdentifier, Referencers, UE::AssetRegistry::EDependencyCategory::SearchableName);

	OutPaths.Reserve(Referencers.Num());
	for (const FAssetIdentifier& Id : Referencers)
	{
		if (!Id.PackageName.IsNone())
		{
			OutPaths.Add(Id.PackageName.ToString());
		}
	}
	OutPaths.Sort();
	return true;
#else
	OutError = TEXT("referencers query requires UE 5.0+ (SearchableName dependency)");
	return false;
#endif
}
