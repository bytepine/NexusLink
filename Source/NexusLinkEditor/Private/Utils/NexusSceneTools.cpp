// Copyright byteyang. All Rights Reserved.
#include "Utils/NexusSceneTools.h"
#include "Utils/NexusEditorDomainTools.h"
#include "Utils/NexusEditorToolSchema.h"
#include "Utils/NexusArgs.h"
#include "Utils/NexusAssetUtils.h"
#include "Utils/NexusVersionCompat.h"
#include "NexusMcpSchemaBuilder.h"
#include "Editor.h"
#include "EngineUtils.h"
#include "Engine/Selection.h"
#include "Engine/StaticMeshActor.h"
#include "EditorActorFolders.h"
#include "FileHelpers.h"
#include "Misc/PackageName.h"
#include "NavigationSystem.h"
#include "NavMesh/NavMeshBoundsVolume.h"
#include "AI/NavigationSystemBase.h"
#if NX_UE_HAS_STATIC_MESH_EDITOR_SUBSYSTEM
#include "StaticMeshEditorSubsystem.h"
#else
#include "EditorLevelLibrary.h"
#endif
#if NX_UE_HAS_LEVEL_INSTANCE_EDITOR
#include "LevelInstance/LevelInstanceActor.h"
#include "LevelInstance/LevelInstanceSubsystem.h"
#endif

TSharedPtr<FJsonObject> FNexusSceneTools::Schema(bool bWrite)
{
    auto Vector = FNexusSchema::Object().Required(TEXT("X"),FNexusSchema::Num(TEXT("X"))).Required(TEXT("Y"),FNexusSchema::Num(TEXT("Y"))).Required(TEXT("Z"),FNexusSchema::Num(TEXT("Z"))).Build();
    auto Action = bWrite ? FNexusSchema::Enum(TEXT("Scene write"),{TEXT("CreateFolder"),TEXT("RenameFolder"),TEXT("DeleteFolder"),TEXT("SetActorFolder"),TEXT("SetSelection"),TEXT("MergeActors"),TEXT("CreateLevelInstance"),TEXT("EditLevelInstance"),TEXT("CommitLevelInstance"),TEXT("BuildNavigation")})
        : FNexusSchema::Enum(TEXT("Scene query"),{TEXT("Folders"),TEXT("FolderActors"),TEXT("Selection"),TEXT("Trace")});
    return FNexusSchema::Object().Required(TEXT("Action"), Action)
        .Prop(TEXT("FolderPath"),FNexusSchema::Str(TEXT("World Outliner folder path")))
        .Prop(TEXT("NewFolderPath"),FNexusSchema::Str(TEXT("Rename destination")))
        .Prop(TEXT("ActorPath"),FNexusSchema::Str(TEXT("Actor object path from discovery")))
        .Prop(TEXT("ActorPaths"),FNexusSchema::StrArr(TEXT("Selection or mesh-merge inputs")))
        .Prop(TEXT("AssetPath"),FNexusSchema::Str(TEXT("Merge output package or existing level asset")))
        .Prop(TEXT("ActorName"),FNexusSchema::Str(TEXT("New actor label")))
        .Prop(TEXT("Location"),Vector.ToSharedRef())
        .Prop(TEXT("Start"),Vector.ToSharedRef()).Prop(TEXT("End"),Vector.ToSharedRef())
        .Prop(TEXT("bRecursive"),FNexusSchema::Bool(TEXT("Include subfolder actors"),true,false))
        .Prop(TEXT("bDestroySourceActors"),FNexusSchema::Bool(TEXT("Remove actors after a successful mesh merge"),true,false))
        .Prop(TEXT("bDiscard"),FNexusSchema::Bool(TEXT("Discard LevelInstance edits"),true,false))
        .Prop(TEXT("bSaveToDisk"),FNexusSchema::Bool(TEXT("BuildNavigation: save generated loaded-level packages after completion; requires all loaded levels clean before starting"),true,false))
        .Prop(TEXT("Limit"),FNexusSchema::Int(TEXT("Maximum query results"),100,1,500)).Build();
}

static bool NexusSceneVector(const TSharedPtr<FJsonObject>& Args, const TCHAR* Key, FVector& Out)
{
    const TSharedPtr<FJsonObject>* Value = nullptr; if (!Args->TryGetObjectField(Key,Value)) return false;
    const FNexusArgs A(*Value); if (!(*Value)->HasField(TEXT("X")) || !(*Value)->HasField(TEXT("Y")) || !(*Value)->HasField(TEXT("Z"))) return false;
    Out = FVector(A.Num(TEXT("X")),A.Num(TEXT("Y")),A.Num(TEXT("Z"))); return !Out.ContainsNaN();
}

static bool NexusSceneFolderValid(const FString& Path)
{
    return !Path.IsEmpty() && !Path.StartsWith(TEXT("/")) && !Path.EndsWith(TEXT("/")) && !Path.Contains(TEXT("..")) && !Path.Contains(TEXT("\\")) && !Path.Contains(TEXT(":")) && !Path.Contains(TEXT("//"));
}

static FCapabilityResult NexusBuildNavigation(UWorld* World, bool bSave)
{
    if (GEditor->IsPlayingSessionInEditor()) return FCapabilityResult::MakeArgInvalid(TEXT("Stop PIE before building navigation"));
    bool bHasBounds = false;
    for (TActorIterator<ANavMeshBoundsVolume> It(World); It; ++It) { bHasBounds = true; break; }
    if (!bHasBounds) return FCapabilityResult::MakeArgInvalid(TEXT("Create a NavMeshBoundsVolume with ManageAssetLevel first"));
    if (bSave) {
        TArray<TSharedPtr<FJsonValue>> DirtyPaths;
        for (ULevel* Level : World->GetLevels())
            if (Level && Level->GetOutermost()->IsDirty()) DirtyPaths.Add(MakeShared<FJsonValueString>(Level->GetOutermost()->GetName()));
        if (DirtyPaths.Num()) {
            auto Result = FCapabilityResult::MakeArgInvalid(TEXT("Save loaded levels explicitly before BuildNavigation with bSaveToDisk=true; unrelated unsaved edits are not saved implicitly"));
            if (!Result.TopFields.IsValid()) Result.TopFields = MakeShared<FJsonObject>();
            Result.TopFields->SetArrayField(TEXT("DirtyLevelPaths"), DirtyPaths);
            return Result;
        }
    }
    UNavigationSystemV1* Navigation = FNavigationSystem::GetCurrent<UNavigationSystemV1>(World);
    if (!Navigation) {
        FNavigationSystem::AddNavigationSystemToWorld(*World, FNavigationSystemRunMode::EditorMode);
        Navigation = FNavigationSystem::GetCurrent<UNavigationSystemV1>(World);
    }
    if (!Navigation || Navigation->IsNavigationBuildInProgress()) return FCapabilityResult::MakeArgInvalid(TEXT("Navigation system unavailable or already building; inspect existing work before starting another build"));
    if (Navigation->IsNavigationBuildingLocked(static_cast<uint8>(~ENavigationBuildLock::NoUpdateInEditor)))
        return FCapabilityResult::MakeArgInvalid(TEXT("Navigation building is locked; no build was started"));
    if (!Navigation->IsThereAnywhereToBuildNavigation())
        return FCapabilityResult::MakeArgInvalid(TEXT("No registered navigation bounds are available; check volume geometry and level visibility"));
    auto Operation = FNexusEditorOperations::Create(TEXT("EngineNavigation"));
    if (!Operation) return FNexusEditorToolSchema::Result(Operation);
    const TWeakObjectPtr<UWorld> WeakWorld(World);
    const TWeakObjectPtr<UNavigationSystemV1> WeakNavigation(Navigation);
    FNexusEditorControlServices::AddTicker(FTickerDelegate::CreateLambda([Operation, WeakWorld, WeakNavigation, bSave, bStarted = false](float) mutable {
        UWorld* CurrentWorld = WeakWorld.Get(); UNavigationSystemV1* CurrentNavigation = WeakNavigation.Get();
        if (!Operation->IsRunning()) return false;
        if (!CurrentWorld || !CurrentNavigation || !GEditor || GEditor->GetEditorWorldContext().World() != CurrentWorld || GEditor->IsPlayingSessionInEditor()) {
            FNexusEditorOperations::Finish(Operation, TEXT("Failed"), nullptr, TEXT("Editor world changed; navigation operation cannot be verified")); return false;
        }
        if (!bStarted) { bStarted = true; CurrentNavigation->Build(); return true; }
        if (CurrentNavigation->IsNavigationBuildInProgress()) return true;
        auto Data = MakeShared<FJsonObject>();
        const bool bHasData = CurrentNavigation->GetDefaultNavDataInstance(FNavigationSystem::DontCreate) != nullptr;
        Data->SetBoolField(TEXT("bBuildComplete"), true); Data->SetBoolField(TEXT("bHasNavigationData"), bHasData);
        Data->SetStringField(TEXT("Outcome"), bHasData ? TEXT("Success") : TEXT("NoNavigationData"));
        FString Error;
        if (bSave && bHasData) {
            TArray<UPackage*> Packages;
            for (ULevel* Level : CurrentWorld->GetLevels()) if (Level && Level->GetOutermost()->IsDirty()) Packages.AddUnique(Level->GetOutermost());
            const bool bSaved = Packages.Num() == 0 || UEditorLoadingAndSavingUtils::SavePackages(Packages, true);
            Data->SetBoolField(TEXT("bSaved"), bSaved);
            if (!bSaved) Error = TEXT("Navigation completed but generated level packages could not be saved");
        }
        auto Result = MakeShared<FJsonObject>(); Result->SetObjectField(TEXT("CompatibilityResult"), Data);
        if (!bHasData) Error = TEXT("Build ended without navigation data; inspect world navigation configuration and geometry");
        FNexusEditorOperations::Finish(Operation, Error.IsEmpty() ? TEXT("Completed") : TEXT("Failed"), Result, Error);
        return false;
    }), 0.1f);
    return FNexusEditorToolSchema::Result(Operation);
}

FCapabilityResult FNexusSceneTools::Execute(bool bWrite, const TSharedPtr<FJsonObject>& Args)
{
    const FNexusArgs A(Args); const FString Action = A.Str(TEXT("Action")), FolderPath = A.Str(TEXT("FolderPath"));
    UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
    if (!World || World->WorldType != EWorldType::Editor) return FCapabilityResult::MakeFatal(TEXT("An editor world is required"));
    if (bWrite && Action == TEXT("BuildNavigation")) return NexusBuildNavigation(World, A.Bool(TEXT("bSaveToDisk")));
    const TSet<FString> Writes = {TEXT("CreateFolder"),TEXT("RenameFolder"),TEXT("DeleteFolder"),TEXT("SetActorFolder"),TEXT("SetSelection"),TEXT("MergeActors"),TEXT("CreateLevelInstance"),TEXT("EditLevelInstance"),TEXT("CommitLevelInstance")};
    const TSet<FString> Reads = {TEXT("Folders"),TEXT("FolderActors"),TEXT("Selection"),TEXT("Trace")};
    if (!(bWrite ? Writes : Reads).Contains(Action)) return FCapabilityResult::MakeArgInvalid(TEXT("Unknown scene action"));
    AActor* Actor = Cast<AActor>(StaticFindObject(AActor::StaticClass(),nullptr,*A.Str(TEXT("ActorPath"))));
    if (Actor && Actor->GetWorld()!=World) return FCapabilityResult::MakeArgInvalid(TEXT("Actor does not belong to the editor world"));
    TArray<AActor*> Actors; TSet<AActor*> Unique;
    const auto Paths = A.StrArr(TEXT("ActorPaths")); if (Paths.Num()>128) return FCapabilityResult::MakeArgInvalid(TEXT("At most 128 actors may participate"));
    for (const auto& Path : Paths) {
        AActor* Found = Cast<AActor>(StaticFindObject(AActor::StaticClass(),nullptr,*Path));
        if (!Found || Found->GetWorld()!=World || Unique.Contains(Found)) return FCapabilityResult::MakeArgInvalid(TEXT("ActorPaths must be unique editor-world actors"));
        Actors.Add(Found); Unique.Add(Found);
    }
    if ((Action.Contains(TEXT("Folder")) && Action!=TEXT("Folders")) && !NexusSceneFolderValid(FolderPath)) return FCapabilityResult::MakeArgInvalid(TEXT("Expected a relative World Outliner folder path"));
    if (Action==TEXT("RenameFolder") && (!NexusSceneFolderValid(A.Str(TEXT("NewFolderPath"))) || A.Str(TEXT("NewFolderPath")).StartsWith(FolderPath+TEXT("/")))) return FCapabilityResult::MakeArgInvalid(TEXT("Invalid folder rename destination"));
    if ((Action==TEXT("SetActorFolder") || Action==TEXT("EditLevelInstance") || Action==TEXT("CommitLevelInstance")) && !Actor) return FCapabilityResult::MakeArgInvalid(TEXT("ActorPath is required"));
    FVector Start=FVector::ZeroVector,End=FVector::ZeroVector,Location=FVector::ZeroVector;
    if (Action==TEXT("Trace") && (!NexusSceneVector(Args,TEXT("Start"),Start) || !NexusSceneVector(Args,TEXT("End"),End))) return FCapabilityResult::MakeArgInvalid(TEXT("Trace requires finite Start/End vectors"));
    TArray<AStaticMeshActor*> MeshActors;
    if (Action==TEXT("MergeActors")) {
        if (Actors.Num()<2 || !FPackageName::IsValidLongPackageName(A.Str(TEXT("AssetPath"))) || FPackageName::DoesPackageExist(A.Str(TEXT("AssetPath")))) return FCapabilityResult::MakeArgInvalid(TEXT("Merge requires at least two meshes and an unused output package"));
        for (AActor* Item : Actors) { auto* MeshActor=Cast<AStaticMeshActor>(Item); if (!MeshActor) return FCapabilityResult::MakeArgInvalid(TEXT("Merge inputs must be StaticMeshActors")); MeshActors.Add(MeshActor); }

    }
    UWorld* LevelAsset=nullptr;
    if (Action==TEXT("CreateLevelInstance")) {
        LevelAsset=FNexusAssetUtils::LoadAssetWithFallback<UWorld>(A.Str(TEXT("AssetPath")));
        if (!LevelAsset || LevelAsset==World || (Args->HasField(TEXT("Location")) && !NexusSceneVector(Args,TEXT("Location"),Location))) return FCapabilityResult::MakeArgInvalid(TEXT("An existing different level and finite location are required"));
    }

    auto Operation=FNexusEditorOperations::Create(TEXT("Independent")); if (!Operation) return FNexusEditorToolSchema::Result(Operation);
    auto Data=MakeShared<FJsonObject>(); TArray<TSharedPtr<FJsonValue>> Items; FString Error;
    const int32 Limit=FMath::Clamp(static_cast<int32>(A.Num(TEXT("Limit"),100)),1,500);
    if (Action==TEXT("Folders")) {
#if NX_UE_HAS_EDITOR_FOLDER_OBJECT
        FActorFolders::Get().ForEachFolder(*World,[&](const FFolder& Folder){ if (Items.Num()<Limit) Items.Add(MakeShared<FJsonValueString>(Folder.GetPath().ToString())); return true; });
#else
        for (const auto& Pair : FActorFolders::Get().GetFolderPropertiesForWorld(*World)) if (Items.Num()<Limit) Items.Add(MakeShared<FJsonValueString>(Pair.Key.ToString()));
#endif
    } else if (Action==TEXT("Selection")) {
        for (FSelectionIterator It(*GEditor->GetSelectedActors());It && Items.Num()<Limit;++It) if (auto* Selected=Cast<AActor>(*It)) Items.Add(MakeShared<FJsonValueObject>(FNexusEditorDomainTools::Reference(Selected->GetPathName())));
    } else if (Action==TEXT("FolderActors")) {
        for (TActorIterator<AActor> It(World);It && Items.Num()<Limit;++It) { const FString Path=It->GetFolderPath().ToString(); if (Path==FolderPath || (A.Bool(TEXT("bRecursive")) && Path.StartsWith(FolderPath+TEXT("/")))) Items.Add(MakeShared<FJsonValueObject>(FNexusEditorDomainTools::Reference(It->GetPathName()))); }
    } else if (Action==TEXT("SetSelection")) { GEditor->SelectNone(false,true,false); for (auto* Item : Actors) GEditor->SelectActor(Item,true,false,true); GEditor->NoteSelectionChange(); }
    else if (Action==TEXT("Trace")) { FHitResult Hit; const bool bHit=World->LineTraceSingleByChannel(Hit,Start,End,ECC_Visibility); Data->SetBoolField(TEXT("bHit"),bHit); if (Hit.GetActor()) Data->SetStringField(TEXT("ActorPath"),Hit.GetActor()->GetPathName()); Data->SetNumberField(TEXT("Distance"),Hit.Distance); }
    else if (Action==TEXT("SetActorFolder")) {
        if (!Actor) Error=TEXT("ActorPath is required");
        else { Actor->Modify(); Actor->SetFolderPath(FName(*FolderPath)); Actor->MarkPackageDirty(); }
    }
    else if (Action==TEXT("CreateFolder") || Action==TEXT("DeleteFolder") || Action==TEXT("RenameFolder")) {
        World->Modify();
#if NX_UE_HAS_EDITOR_FOLDER_OBJECT
#if NX_UE_HAS_WORLD_FOLDER_ROOT_HELPER
        const FFolder Folder(FFolder::GetWorldRootFolder(World).GetRootObject(),FName(*FolderPath));
        const FFolder Destination(Folder.GetRootObject(),FName(*A.Str(TEXT("NewFolderPath"))));
#else
        const auto RootObject=FFolder::GetOptionalFolderRootObject(World->PersistentLevel).Get(FFolder::GetDefaultRootObject());
        const FFolder Folder(FName(*FolderPath),RootObject);
        const FFolder Destination(FName(*A.Str(TEXT("NewFolderPath"))),RootObject);
#endif
        if (Action==TEXT("CreateFolder")) FActorFolders::Get().CreateFolder(*World,Folder);
        else if (Action==TEXT("DeleteFolder")) FActorFolders::Get().DeleteFolder(*World,Folder);
        else if (!FActorFolders::Get().RenameFolderInWorld(*World,Folder,Destination)) Error=TEXT("Could not rename folder");
#else
        if (Action==TEXT("CreateFolder")) FActorFolders::Get().CreateFolder(*World,FName(*FolderPath));
        else if (Action==TEXT("DeleteFolder")) FActorFolders::Get().DeleteFolder(*World,FName(*FolderPath));
        else if (!FActorFolders::Get().RenameFolderInWorld(*World,FName(*FolderPath),FName(*A.Str(TEXT("NewFolderPath"))))) Error=TEXT("Could not rename folder");
#endif
    } else if (Action==TEXT("MergeActors")) {
        AStaticMeshActor* Merged=nullptr;
#if NX_UE_HAS_STATIC_MESH_EDITOR_SUBSYSTEM
        FMergeStaticMeshActorsOptions Options;
#else
        FEditorScriptingMergeStaticMeshActorsOptions Options;
#endif
        Options.BasePackageName=A.Str(TEXT("AssetPath")); Options.NewActorLabel=A.Str(TEXT("ActorName")); Options.bDestroySourceActors=A.Bool(TEXT("bDestroySourceActors"));
#if NX_UE_HAS_STATIC_MESH_EDITOR_SUBSYSTEM
        UStaticMeshEditorSubsystem* MeshEditor=GEditor?GEditor->GetEditorSubsystem<UStaticMeshEditorSubsystem>():nullptr;
        if (!MeshEditor) return FCapabilityResult::MakeFatal(TEXT("Static mesh editor subsystem is unavailable"));
        const bool bMerged=MeshEditor->MergeStaticMeshActors(MeshActors,Options,Merged);
#else
        const bool bMerged=UEditorLevelLibrary::MergeStaticMeshActors(MeshActors,Options,Merged);
#endif
        if (!bMerged || !Merged) Error=TEXT("Static mesh merge failed"); else Data->SetStringField(TEXT("ActorPath"),Merged->GetPathName());
    } else {
#if NX_UE_HAS_LEVEL_INSTANCE_EDITOR
        ULevelInstanceSubsystem* Subsystem=World->GetSubsystem<ULevelInstanceSubsystem>();
        ALevelInstance* Instance=Cast<ALevelInstance>(Actor);
        if (Action==TEXT("CreateLevelInstance")) { FActorSpawnParameters Params; Params.ObjectFlags|=RF_Transactional; Instance=World->SpawnActor<ALevelInstance>(Location,FRotator::ZeroRotator,Params); if (!Instance || !Instance->SetWorldAsset(LevelAsset)) { if (Instance) World->DestroyActor(Instance); Error=TEXT("Could not create level instance"); } else { if (!A.Str(TEXT("ActorName")).IsEmpty()) Instance->SetActorLabel(A.Str(TEXT("ActorName"))); Data->SetStringField(TEXT("ActorPath"),Instance->GetPathName()); } }
        else if (!Instance || !Subsystem) Error=TEXT("Actor must be a LevelInstance");
        else if (Action==TEXT("EditLevelInstance")) { FText Reason; if (!Subsystem->CanEditLevelInstance(Instance,&Reason)) Error=Reason.ToString(); else Subsystem->EditLevelInstance(Instance); }
        else { FText Reason;
#if NX_UE_HAS_LEVEL_INSTANCE_DISCARD_CHECK
            const bool bCanCommit=Subsystem->CanCommitLevelInstance(Instance,A.Bool(TEXT("bDiscard")),&Reason);
#else
            const bool bCanCommit=Subsystem->CanCommitLevelInstance(Instance,&Reason);
#endif
            if (!bCanCommit) Error=Reason.ToString(); else {
                ULevel* Level=Subsystem->GetLevelInstanceLevel(Instance);
                if (!A.Bool(TEXT("bDiscard")) && Level && !UEditorLoadingAndSavingUtils::SavePackages({Level->GetOutermost()},true)) Error=TEXT("Could not save the edited instance package");
                if (Error.IsEmpty() && !Subsystem->CommitLevelInstance(Instance,A.Bool(TEXT("bDiscard")))) Error=TEXT("Could not commit level instance");
            } }
#else
        Error=TEXT("LevelInstance editing is not available on this engine");
#endif
    }
    Data->SetArrayField(TEXT("Items"),Items); auto Result=MakeShared<FJsonObject>(); Result->SetObjectField(TEXT("CompatibilityResult"),Data);
    FNexusEditorOperations::Finish(Operation,Error.IsEmpty()?TEXT("Completed"):TEXT("Failed"),Result,Error); return FNexusEditorToolSchema::Result(Operation);
}
