// Copyright byteyang. All Rights Reserved.
#include "Utils/NexusSceneTools.h"
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
    auto Action = bWrite ? FNexusSchema::Enum(TEXT("Scene write"),{TEXT("CreateFolder"),TEXT("RenameFolder"),TEXT("DeleteFolder"),TEXT("SetActorFolder"),TEXT("SetSelection"),TEXT("MergeActors"),TEXT("CreateLevelInstance"),TEXT("EditLevelInstance"),TEXT("CommitLevelInstance")})
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

FCapabilityResult FNexusSceneTools::Execute(bool bWrite, const TSharedPtr<FJsonObject>& Args)
{
    const FNexusArgs A(Args); const FString Action = A.Str(TEXT("Action")), FolderPath = A.Str(TEXT("FolderPath"));
    UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
    if (!World || World->WorldType != EWorldType::Editor) return FCapabilityResult::MakeFatal(TEXT("An editor world is required"));
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
    auto Input=MakeShared<FJsonObject>(); FString Tool;
    if (Action==TEXT("Folders")) Tool=TEXT("get_folders");
    else if (Action==TEXT("FolderActors")) { Tool=TEXT("get_actors_in_folder"); Input->SetStringField(TEXT("folder_path"),FolderPath); Input->SetBoolField(TEXT("recursive"),A.Bool(TEXT("bRecursive"))); }
    else if (Action==TEXT("SetActorFolder")) { Tool=TEXT("set_actor_folder"); Input->SetObjectField(TEXT("actor"),FNexusNativeEditorTools::Reference(Actor->GetPathName())); Input->SetStringField(TEXT("folder_path"),FolderPath); }
    else if (Action==TEXT("RenameFolder")) { Tool=TEXT("rename_folder"); Input->SetStringField(TEXT("old_path"),FolderPath); Input->SetStringField(TEXT("new_path"),A.Str(TEXT("NewFolderPath"))); }
    else if (Action==TEXT("DeleteFolder")) { Tool=TEXT("delete_folder"); Input->SetStringField(TEXT("folder_path"),FolderPath); }
    FVector Start=FVector::ZeroVector,End=FVector::ZeroVector,Location=FVector::ZeroVector;
    if (Action==TEXT("Trace")) {
        if (!NexusSceneVector(Args,TEXT("Start"),Start) || !NexusSceneVector(Args,TEXT("End"),End)) return FCapabilityResult::MakeArgInvalid(TEXT("Trace requires finite Start/End vectors"));
        Tool=TEXT("trace_world");
        for (const auto& Pair : {TPair<FString,FVector>(TEXT("start"),Start),TPair<FString,FVector>(TEXT("end"),End)}) { auto Value=MakeShared<FJsonObject>(); Value->SetNumberField(TEXT("x"),Pair.Value.X); Value->SetNumberField(TEXT("y"),Pair.Value.Y); Value->SetNumberField(TEXT("z"),Pair.Value.Z); Input->SetObjectField(Pair.Key,Value); }
    }
    TArray<AStaticMeshActor*> MeshActors;
    if (Action==TEXT("MergeActors")) {
        if (Actors.Num()<2 || !FPackageName::IsValidLongPackageName(A.Str(TEXT("AssetPath"))) || FPackageName::DoesPackageExist(A.Str(TEXT("AssetPath")))) return FCapabilityResult::MakeArgInvalid(TEXT("Merge requires at least two meshes and an unused output package"));
        for (AActor* Item : Actors) { auto* MeshActor=Cast<AStaticMeshActor>(Item); if (!MeshActor) return FCapabilityResult::MakeArgInvalid(TEXT("Merge inputs must be StaticMeshActors")); MeshActors.Add(MeshActor); }
        Tool=TEXT("merge_actors"); TArray<TSharedPtr<FJsonValue>> Refs; for (AActor* Item : Actors) Refs.Add(MakeShared<FJsonValueObject>(FNexusNativeEditorTools::Reference(Item->GetPathName())));
        Input->SetArrayField(TEXT("actors"),Refs); Input->SetStringField(TEXT("output_path"),A.Str(TEXT("AssetPath"))); Input->SetStringField(TEXT("name"),A.Str(TEXT("ActorName"))); Input->SetBoolField(TEXT("destroy_source_actors"),A.Bool(TEXT("bDestroySourceActors")));
    }
    UWorld* LevelAsset=nullptr;
    if (Action==TEXT("CreateLevelInstance")) {
        LevelAsset=FNexusAssetUtils::LoadAssetWithFallback<UWorld>(A.Str(TEXT("AssetPath")));
        if (!LevelAsset || LevelAsset==World || (Args->HasField(TEXT("Location")) && !NexusSceneVector(Args,TEXT("Location"),Location))) return FCapabilityResult::MakeArgInvalid(TEXT("An existing different level and finite location are required"));
        Tool=TEXT("create_level_instance"); Input->SetStringField(TEXT("level_path"),A.Str(TEXT("AssetPath"))); Input->SetStringField(TEXT("name"),A.Str(TEXT("ActorName")));
        auto Transform=MakeShared<FJsonObject>(),Position=MakeShared<FJsonObject>(),Rotation=MakeShared<FJsonObject>(),Scale=MakeShared<FJsonObject>();
        Position->SetNumberField(TEXT("x"),Location.X); Position->SetNumberField(TEXT("y"),Location.Y); Position->SetNumberField(TEXT("z"),Location.Z);
        for (const auto& Key : {TEXT("pitch"),TEXT("yaw"),TEXT("roll")}) Rotation->SetNumberField(Key,0);
        for (const auto& Key : {TEXT("x"),TEXT("y"),TEXT("z")}) Scale->SetNumberField(Key,1);
        Transform->SetObjectField(TEXT("location"),Position); Transform->SetObjectField(TEXT("rotation"),Rotation); Transform->SetObjectField(TEXT("scale"),Scale); Input->SetObjectField(TEXT("xform"),Transform);
    }
    if (Action==TEXT("EditLevelInstance")) { Tool=TEXT("edit_level_instance"); Input->SetObjectField(TEXT("level_instance"),FNexusNativeEditorTools::Reference(Actor->GetPathName())); }
    // 原生 commit 会保存所有脏包，局部提交始终使用下方的引擎接口。
    auto Operation=FNexusEditorOperations::Create(TEXT("EngineCompatibility")); if (!Operation) return FNexusEditorToolSchema::Result(Operation);
    if (!Tool.IsEmpty() && FNexusNativeEditorTools::UseNative(TEXT("editor_toolset.toolsets.scene.SceneTools"))) { Operation->Backend=TEXT("OfficialToolset"); FNexusNativeEditorTools::Call(TEXT("editor_toolset.toolsets.scene.SceneTools"),Tool,Input,Operation); return FNexusEditorToolSchema::Result(Operation); }
    auto Data=MakeShared<FJsonObject>(); TArray<TSharedPtr<FJsonValue>> Items; FString Error;
    const int32 Limit=FMath::Clamp(static_cast<int32>(A.Num(TEXT("Limit"),100)),1,500);
    if (Action==TEXT("Folders")) {
#if NX_UE_HAS_EDITOR_FOLDER_OBJECT
        FActorFolders::Get().ForEachFolder(*World,[&](const FFolder& Folder){ if (Items.Num()<Limit) Items.Add(MakeShared<FJsonValueString>(Folder.GetPath().ToString())); return true; });
#else
        for (const auto& Pair : FActorFolders::Get().GetFolderPropertiesForWorld(*World)) if (Items.Num()<Limit) Items.Add(MakeShared<FJsonValueString>(Pair.Key.ToString()));
#endif
    } else if (Action==TEXT("Selection")) {
        for (FSelectionIterator It(*GEditor->GetSelectedActors());It && Items.Num()<Limit;++It) if (auto* Selected=Cast<AActor>(*It)) Items.Add(MakeShared<FJsonValueObject>(FNexusNativeEditorTools::Reference(Selected->GetPathName())));
    } else if (Action==TEXT("FolderActors")) {
        for (TActorIterator<AActor> It(World);It && Items.Num()<Limit;++It) { const FString Path=It->GetFolderPath().ToString(); if (Path==FolderPath || (A.Bool(TEXT("bRecursive")) && Path.StartsWith(FolderPath+TEXT("/")))) Items.Add(MakeShared<FJsonValueObject>(FNexusNativeEditorTools::Reference(It->GetPathName()))); }
    } else if (Action==TEXT("SetSelection")) { GEditor->SelectNone(false,true,false); for (auto* Item : Actors) GEditor->SelectActor(Item,true,false,true); GEditor->NoteSelectionChange(); }
    else if (Action==TEXT("Trace")) { FHitResult Hit; const bool bHit=World->LineTraceSingleByChannel(Hit,Start,End,ECC_Visibility); Data->SetBoolField(TEXT("bHit"),bHit); if (Hit.GetActor()) Data->SetStringField(TEXT("ActorPath"),Hit.GetActor()->GetPathName()); Data->SetNumberField(TEXT("Distance"),Hit.Distance); }
    else if (Action==TEXT("SetActorFolder")) { Actor->Modify(); Actor->SetFolderPath(FName(*FolderPath)); Actor->MarkPackageDirty(); }
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
        const bool bMerged=GEditor->GetEditorSubsystem<UStaticMeshEditorSubsystem>()->MergeStaticMeshActors(MeshActors,Options,Merged);
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
