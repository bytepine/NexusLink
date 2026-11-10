// Copyright byteyang. All Rights Reserved.

#include "Utils/NexusEditorLevelUtils.h"
#include "Utils/NexusJsonUtils.h"
#include "Utils/NexusStringMatchUtils.h"
#include "Dom/JsonObject.h"

#if WITH_EDITOR
#include "Editor.h"
#include "Engine/World.h"
#include "Engine/Level.h"
#include "Components/SceneComponent.h"
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
	OutError = TEXT("GetAssetLevel only available in editor mode");
	return nullptr;
#else
	const FString PackagePath = NormalizeLevelPackagePath(AssetPath);
	if (PackagePath.IsEmpty())
	{
		OutError = TEXT("AssetPath is empty");
		return nullptr;
	}

	if (GEditor)
	{
		if (UWorld* EditorWorld = GEditor->GetEditorWorldContext().World())
		{
			for (ULevel* Level : EditorWorld->GetLevels())
			{
				if (!Level || Level->GetOutermost()->GetName() != PackagePath) continue;
				UWorld* AssetWorld = UWorld::FindWorldInPackage(Level->GetOutermost());
				if (!AssetWorld) continue;
				bOutIsEditorWorld = true;
				return AssetWorld;
			}
		}
	}

	// 优先复用已加载的地图（包括尚未保存的新地图），避免重复加载及忽略内存中的修改。
	UPackage* Package = FindPackage(nullptr, *PackagePath);
	UWorld* LoadedWorld = Package ? UWorld::FindWorldInPackage(Package) : nullptr;
	if (LoadedWorld && !LoadedWorld->HasAnyFlags(RF_NeedLoad | RF_NeedPostLoad)) return LoadedWorld;
	Package = LoadPackage(nullptr, *PackagePath, LOAD_NoWarn);
	if (!Package)
	{
		OutError = FString::Printf(TEXT("Level package not found: %s"), *PackagePath);
		return nullptr;
	}

	UWorld* World = UWorld::FindWorldInPackage(Package);
	if (!World)
	{
		OutError = FString::Printf(TEXT("No UWorld in package: %s"), *PackagePath);
		return nullptr;
	}
	return World;
#endif
}

UWorld* FNexusEditorLevelUtils::LoadLevelWorldForWrite(const FString& AssetPath, bool& bOutIsEditorWorld, FString& OutError)
{
	return LoadLevelWorldForRead(AssetPath, bOutIsEditorWorld, OutError);
}

ULevel* FNexusEditorLevelUtils::ResolveLevel(UWorld* World)
{
	if (!World) return nullptr;
	if (GEditor)
		if (UWorld* EditorWorld = GEditor->GetEditorWorldContext().World())
			for (ULevel* Level : EditorWorld->GetLevels())
				if (Level && Level->GetOutermost() == World->GetOutermost()) return Level;
	return World->PersistentLevel;
}

void FNexusEditorLevelUtils::RefreshActorAfterPropertyEdit(AActor* Actor)
{
	if (!Actor) return;
	TInlineComponentArray<USceneComponent*> Components(Actor);
	for (USceneComponent* Component : Components)
		if (Component) Component->UpdateComponentToWorld();
	Actor->PostEditChange();
	Actor->PostEditMove(true);
	Actor->MarkPackageDirty();
}

AActor* FNexusEditorLevelUtils::FindLevelActorByNameOrLabel(UWorld* World, const FString& ActorNameOrLabel, bool bAllowLabel)
{
#if !WITH_EDITOR
	return nullptr;
#else
	if (!World || ActorNameOrLabel.IsEmpty())
	{
		return nullptr;
	}
	ULevel* Level = ResolveLevel(World);
	if (!Level) return nullptr;
	AActor* Found = nullptr;
	for (AActor* Actor : Level->Actors)
	{
		if (!IsValid(Actor) || Actor->GetLevel() != Level) continue;
		if (Actor->GetName() == ActorNameOrLabel || (bAllowLabel && Actor->GetActorLabel() == ActorNameOrLabel))
		{
			if (Found) return nullptr;
			Found = Actor;
		}
	}
	return Found;
#endif
}

bool FNexusEditorLevelUtils::SpawnActorInLevelWorld(UWorld* World, UClass* Class, const FVector& Location,
	const FRotator& Rotation, AActor*& OutActor, FString& OutError)
{
#if !WITH_EDITOR
	OutError = TEXT("SpawnActor only available in editor mode");
	return false;
#else
	OutActor = nullptr;
	if (!World || !Class)
	{
		OutError = TEXT("Invalid World or Class");
		return false;
	}
	FActorSpawnParameters Params;
	Params.OverrideLevel = ResolveLevel(World);
	UWorld* OwningWorld = Params.OverrideLevel ? Params.OverrideLevel->GetWorld() : nullptr;
	if (!OwningWorld) { OutError = TEXT("Level has no owning world"); return false; }
	OutActor = OwningWorld->SpawnActor<AActor>(Class, Location, Rotation, Params);
	if (!OutActor)
	{
		OutError = TEXT("SpawnActor failed");
		return false;
	}
	OutActor->MarkPackageDirty();
	return true;
#endif
}

bool FNexusEditorLevelUtils::RemoveLevelActor(UWorld* World, AActor* Actor, FString& OutError)
{
#if !WITH_EDITOR
	OutError = TEXT("RemoveActor only available in editor mode");
	return false;
#else
	if (!World || !Actor)
	{
		OutError = TEXT("Invalid World or Actor");
		return false;
	}
	ULevel* Level = ResolveLevel(World);
	if (!Level || Actor->GetLevel() != Level || !Actor->GetWorld())
	{ OutError = TEXT("Actor does not belong to the requested level"); return false; }
	const bool bDestroyed = Actor->GetWorld()->EditorDestroyActor(Actor, true);
	if (!bDestroyed)
	{
		OutError = TEXT("EditorDestroyActor failed");
		return false;
	}
	Level->MarkPackageDirty();
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
		Args->TryGetStringField(TEXT("ClassFilter"), ClassFilter);
		Args->TryGetStringField(TEXT("NameFilter"), NameFilter);
		Args->TryGetStringField(TEXT("TagFilter"), TagFilter);
		if (Args->HasField(TEXT("Offset")))
		{
			Offset = FMath::Max(0, static_cast<int32>(Args->GetNumberField(TEXT("Offset"))));
		}
		if (Args->HasField(TEXT("Limit")))
		{
			Limit = FMath::Clamp(static_cast<int32>(Args->GetNumberField(TEXT("Limit"))), 1, 500);
		}
	}

	struct FActorRow
	{
		FString Name;
		FString Label;
		FString Class;
		FString Location;
		FString Path;
		FString LevelPath;
	};

	TArray<FActorRow> All;
	ULevel* Level = ResolveLevel(World);
	if (!Level) return;
	for (AActor* Actor : Level->Actors)
	{
		if (!IsValid(Actor) || Actor->GetLevel() != Level) continue;

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
		Row.Path = Actor->GetPathName();
		Row.LevelPath = Level->GetOutermost()->GetName();
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
		O->SetStringField(TEXT("Name"), All[i].Name);
		if (!All[i].Label.IsEmpty())
		{
			O->SetStringField(TEXT("Label"), All[i].Label);
		}
		O->SetStringField(TEXT("Class"), All[i].Class);
		O->SetStringField(TEXT("ActorPath"), All[i].Path);
		O->SetStringField(TEXT("LevelPath"), All[i].LevelPath);
		O->SetStringField(TEXT("Location"), All[i].Location);
		Page.Add(MakeShared<FJsonValueObject>(O));
	}

	Entry->SetNumberField(TEXT("ActorTotalCount"), Total);
	Entry->SetNumberField(TEXT("Offset"), Start);
	Entry->SetNumberField(TEXT("Limit"), Limit);
	Entry->SetArrayField(TEXT("Actors"), Page);
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
	ULevel* Level = ResolveLevel(World);
	AWorldSettings* WS = Level ? Level->GetWorldSettings() : nullptr;
	if (WS)
	{
		SettingsObj->SetNumberField(TEXT("GravityZ"), WS->GetGravityZ());
		if (WS->DefaultGameMode)
		{
			SettingsObj->SetStringField(TEXT("DefaultGameMode"), WS->DefaultGameMode->GetPathName());
		}
		SettingsObj->SetNumberField(TEXT("KillZ"), WS->KillZ);
		SettingsObj->SetBoolField(TEXT("bEnableWorldBoundsChecks"), WS->bEnableWorldBoundsChecks);
	}
	Entry->SetObjectField(TEXT("Settings"), SettingsObj);
#endif
}
