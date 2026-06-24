// Copyright byteyang. All Rights Reserved.

#include "Utils/NexusEditorLevelUtils.h"
#include "Utils/NexusJsonUtils.h"
#include "Utils/NexusStringMatchUtils.h"
#include "Dom/JsonObject.h"

#if WITH_EDITOR
#include "Editor.h"
#include "Engine/World.h"
#include "GameFramework/GameModeBase.h"
#include "GameFramework/WorldSettings.h"
#include "EngineUtils.h"
#include "GameFramework/Actor.h"
#include "UObject/Package.h"
#endif

FString FNexusEditorLevelUtils::NormalizeLevelPackagePath(const FString& AssetPath)
{
	FString PackagePath = AssetPath;
	int32 DotIdx = INDEX_NONE;
	if (PackagePath.FindLastChar(TEXT('.'), DotIdx))
	{
		PackagePath = PackagePath.Left(DotIdx);
	}
	return PackagePath;
}

UWorld* FNexusEditorLevelUtils::LoadLevelWorldForRead(const FString& AssetPath, bool& bOutIsEditorWorld, FString& OutError)
{
	bOutIsEditorWorld = false;
#if !WITH_EDITOR
	OutError = TEXT("get_asset_level 仅在编辑器模式可用");
	return nullptr;
#else
	const FString PackagePath = NormalizeLevelPackagePath(AssetPath);
	if (PackagePath.IsEmpty())
	{
		OutError = TEXT("assetPath 为空");
		return nullptr;
	}

	if (GEditor)
	{
		if (UWorld* EditorWorld = GEditor->GetEditorWorldContext().World())
		{
			if (EditorWorld->GetOutermost() && EditorWorld->GetOutermost()->GetName() == PackagePath)
			{
				bOutIsEditorWorld = true;
				return EditorWorld;
			}
		}
	}

	UPackage* Package = LoadPackage(nullptr, *PackagePath, LOAD_NoWarn);
	if (!Package)
	{
		OutError = FString::Printf(TEXT("关卡包未找到: %s"), *PackagePath);
		return nullptr;
	}

	UWorld* World = UWorld::FindWorldInPackage(Package);
	if (!World)
	{
		OutError = FString::Printf(TEXT("包内无 UWorld: %s"), *PackagePath);
		return nullptr;
	}
	return World;
#endif
}

UWorld* FNexusEditorLevelUtils::LoadLevelWorldForWrite(const FString& AssetPath, bool& bOutIsEditorWorld, FString& OutError)
{
	return LoadLevelWorldForRead(AssetPath, bOutIsEditorWorld, OutError);
}

AActor* FNexusEditorLevelUtils::FindLevelActorByNameOrLabel(UWorld* World, const FString& ActorNameOrLabel)
{
#if !WITH_EDITOR
	return nullptr;
#else
	if (!World || ActorNameOrLabel.IsEmpty())
	{
		return nullptr;
	}
	for (TActorIterator<AActor> It(World); It; ++It)
	{
		AActor* Actor = *It;
		if (!Actor) continue;
		if (Actor->GetName() == ActorNameOrLabel || Actor->GetActorLabel() == ActorNameOrLabel)
		{
			return Actor;
		}
	}
	return nullptr;
#endif
}

bool FNexusEditorLevelUtils::SpawnActorInLevelWorld(UWorld* World, UClass* Class, const FVector& Location,
	const FRotator& Rotation, AActor*& OutActor, FString& OutError)
{
#if !WITH_EDITOR
	OutError = TEXT("spawn_actor 仅在编辑器模式可用");
	return false;
#else
	OutActor = nullptr;
	if (!World || !Class)
	{
		OutError = TEXT("World 或 Class 无效");
		return false;
	}
	FActorSpawnParameters Params;
	Params.OverrideLevel = World->PersistentLevel;
	OutActor = World->SpawnActor<AActor>(Class, Location, Rotation, Params);
	if (!OutActor)
	{
		OutError = TEXT("SpawnActor 失败");
		return false;
	}
	World->MarkPackageDirty();
	return true;
#endif
}

bool FNexusEditorLevelUtils::RemoveLevelActor(UWorld* World, AActor* Actor, FString& OutError)
{
#if !WITH_EDITOR
	OutError = TEXT("remove_actor 仅在编辑器模式可用");
	return false;
#else
	if (!World || !Actor)
	{
		OutError = TEXT("World 或 Actor 无效");
		return false;
	}
	const bool bDestroyed = World->EditorDestroyActor(Actor, true);
	if (!bDestroyed)
	{
		OutError = TEXT("EditorDestroyActor 失败");
		return false;
	}
	World->MarkPackageDirty();
	return true;
#endif
}

void FNexusEditorLevelUtils::AppendLevelActorsSection(UWorld* World, const TSharedPtr<FJsonObject>& Args, TSharedPtr<FJsonObject>& Entry)
{
#if !WITH_EDITOR
	return;
#else
	if (!World || !Entry.IsValid())
	{
		return;
	}

	FString ClassFilter, NameFilter, TagFilter;
	int32 Offset = 0;
	int32 Limit = 100;
	if (Args.IsValid())
	{
		Args->TryGetStringField(TEXT("classFilter"), ClassFilter);
		Args->TryGetStringField(TEXT("nameFilter"), NameFilter);
		Args->TryGetStringField(TEXT("tagFilter"), TagFilter);
		if (Args->HasField(TEXT("offset")))
		{
			Offset = FMath::Max(0, static_cast<int32>(Args->GetNumberField(TEXT("offset"))));
		}
		if (Args->HasField(TEXT("limit")))
		{
			Limit = FMath::Clamp(static_cast<int32>(Args->GetNumberField(TEXT("limit"))), 1, 500);
		}
	}

	struct FActorRow
	{
		FString Name;
		FString Label;
		FString Class;
		FString Location;
	};

	TArray<FActorRow> All;
	for (TActorIterator<AActor> It(World); It; ++It)
	{
		AActor* Actor = *It;
		if (!Actor) continue;

		const FString ClassName = Actor->GetClass()->GetName();
		const FString ActorName = Actor->GetName();
		const FString ActorLabel = Actor->GetActorLabel();

		if (!ClassFilter.IsEmpty() && !FNexusStringMatchUtils::Matches(ClassName, ClassFilter)) continue;
		if (!NameFilter.IsEmpty()
			&& !FNexusStringMatchUtils::Matches(ActorName, NameFilter)
			&& !FNexusStringMatchUtils::Matches(ActorLabel, NameFilter))
		{
			continue;
		}
		if (!TagFilter.IsEmpty())
		{
			bool bHasTag = false;
			for (const FName& Tag : Actor->Tags)
			{
				if (Tag.ToString() == TagFilter)
				{
					bHasTag = true;
					break;
				}
			}
			if (!bHasTag) continue;
		}

		FActorRow Row;
		Row.Name = ActorName;
		Row.Label = ActorLabel;
		Row.Class = ClassName;
		const FVector Loc = Actor->GetActorLocation();
		Row.Location = FString::Printf(TEXT("%.1f, %.1f, %.1f"), Loc.X, Loc.Y, Loc.Z);
		All.Add(Row);
	}

	const int32 Total = All.Num();
	int32 Start = 0;
	int32 End = 0;
	FNexusJsonUtils::ComputeSlice(Total, Offset, Limit, Start, End);

	TArray<TSharedPtr<FJsonValue>> Page;
	for (int32 i = Start; i < End; ++i)
	{
		TSharedPtr<FJsonObject> O = MakeShared<FJsonObject>();
		O->SetStringField(TEXT("name"), All[i].Name);
		if (!All[i].Label.IsEmpty())
		{
			O->SetStringField(TEXT("label"), All[i].Label);
		}
		O->SetStringField(TEXT("class"), All[i].Class);
		O->SetStringField(TEXT("location"), All[i].Location);
		Page.Add(MakeShared<FJsonValueObject>(O));
	}

	Entry->SetNumberField(TEXT("actorTotalCount"), Total);
	Entry->SetNumberField(TEXT("offset"), Start);
	Entry->SetNumberField(TEXT("limit"), Limit);
	Entry->SetArrayField(TEXT("actors"), Page);
#endif
}

void FNexusEditorLevelUtils::AppendLevelSettingsSection(UWorld* World, TSharedPtr<FJsonObject>& Entry)
{
#if !WITH_EDITOR
	return;
#else
	if (!World || !Entry.IsValid())
	{
		return;
	}

	TSharedPtr<FJsonObject> SettingsObj = MakeShared<FJsonObject>();
	AWorldSettings* WS = World->GetWorldSettings();
	if (WS)
	{
		SettingsObj->SetNumberField(TEXT("gravityZ"), WS->GetGravityZ());
		if (WS->DefaultGameMode)
		{
			SettingsObj->SetStringField(TEXT("defaultGameMode"), WS->DefaultGameMode->GetPathName());
		}
		SettingsObj->SetNumberField(TEXT("killZ"), WS->KillZ);
		SettingsObj->SetBoolField(TEXT("enableWorldBoundsChecks"), WS->bEnableWorldBoundsChecks);
	}
	Entry->SetObjectField(TEXT("settings"), SettingsObj);
#endif
}
