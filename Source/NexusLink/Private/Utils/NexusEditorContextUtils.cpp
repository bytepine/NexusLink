// Copyright byteyang. All Rights Reserved.

#include "Utils/NexusEditorContextUtils.h"
#include "Utils/NexusVersionCompat.h"

#if WITH_EDITOR
#include "Editor.h"
#include "Engine/Selection.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "Modules/ModuleManager.h"
#include "IContentBrowserSingleton.h"
#include "ContentBrowserModule.h"
#include "AssetRegistry/AssetData.h"
#if NX_UE_HAS_CONTENT_BROWSER_ITEM_PATH
#include "ContentBrowserItemPath.h"
#endif
#endif

void FNexusEditorContextUtils::CollectSelectionActors(
	TArray<TSharedPtr<FJsonValue>>& OutActors, int32& OutCount, int32 Limit)
{
#if WITH_EDITOR
	if (!GEditor)
	{
		return;
	}

	USelection* Selection = GEditor->GetSelectedActors();
	if (!Selection)
	{
		return;
	}

	UWorld* EditorWorld = GEditor->GetEditorWorldContext().World();

	for (FSelectionIterator It(*Selection); It && OutCount < Limit; ++It)
	{
		AActor* Actor = Cast<AActor>(*It);
		if (!Actor)
		{
			continue;
		}

		TSharedPtr<FJsonObject> Obj = MakeShared<FJsonObject>();
		Obj->SetStringField(TEXT("name"), Actor->GetName());
		Obj->SetStringField(TEXT("label"), Actor->GetActorLabel());
		Obj->SetStringField(TEXT("class"), Actor->GetClass()->GetName());
		if (EditorWorld && Actor->GetWorld() == EditorWorld)
		{
			Obj->SetStringField(TEXT("level"), EditorWorld->GetName());
		}
		OutActors.Add(MakeShared<FJsonValueObject>(Obj));
		++OutCount;
	}
#else
	(void)OutActors;
	(void)OutCount;
	(void)Limit;
#endif
}

void FNexusEditorContextUtils::CollectSelectionAssets(
	TArray<TSharedPtr<FJsonValue>>& OutAssets, int32& OutCount, int32 Limit)
{
#if WITH_EDITOR
	if (!FModuleManager::Get().IsModuleLoaded(TEXT("ContentBrowser")))
	{
		return;
	}

	FContentBrowserModule& ContentBrowserModule =
		FModuleManager::LoadModuleChecked<FContentBrowserModule>(TEXT("ContentBrowser"));
	TArray<FAssetData> SelectedAssets;
	ContentBrowserModule.Get().GetSelectedAssets(SelectedAssets);

	for (const FAssetData& Asset : SelectedAssets)
	{
		if (OutCount >= Limit)
		{
			break;
		}

		TSharedPtr<FJsonObject> Obj = MakeShared<FJsonObject>();
#if NX_UE_HAS_ASSET_SOFT_OBJECT_PATH
		Obj->SetStringField(TEXT("path"), Asset.GetSoftObjectPath().ToString());
#else
		Obj->SetStringField(TEXT("path"), Asset.ObjectPath.ToString());
#endif
		Obj->SetStringField(TEXT("packageName"), Asset.PackageName.ToString());
#if NX_UE_HAS_CLASS_PATHS
		Obj->SetStringField(TEXT("assetClass"), Asset.AssetClassPath.GetAssetName().ToString());
#else
		Obj->SetStringField(TEXT("assetClass"), Asset.AssetClass.ToString());
#endif
		OutAssets.Add(MakeShared<FJsonValueObject>(Obj));
		++OutCount;
	}
#else
	(void)OutAssets;
	(void)OutCount;
	(void)Limit;
#endif
}

bool FNexusEditorContextUtils::CollectContentBrowserPath(FString& OutPath, FString& OutError)
{
#if WITH_EDITOR
	if (!FModuleManager::Get().IsModuleLoaded(TEXT("ContentBrowser")))
	{
		OutError = TEXT("ContentBrowser 模块未加载");
		return false;
	}

	FContentBrowserModule& ContentBrowserModule =
		FModuleManager::LoadModuleChecked<FContentBrowserModule>(TEXT("ContentBrowser"));

#if NX_UE_HAS_CONTENT_BROWSER_ITEM_PATH
	const FContentBrowserItemPath ItemPath = ContentBrowserModule.Get().GetCurrentPath();
	OutPath = ItemPath.HasInternalPath()
		? ItemPath.GetInternalPathString()
		: ItemPath.GetVirtualPathString();
#else
		OutPath = ContentBrowserModule.Get().GetCurrentPath();
#endif

	return true;
#else
	OutError = TEXT("仅编辑器构建可用");
	return false;
#endif
}
