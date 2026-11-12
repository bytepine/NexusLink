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
#include "NavigationOctree.h"
#include "NavMesh/RecastNavMesh.h"
#include "AI/Navigation/NavAreaBase.h"
#include "Components/PrimitiveComponent.h"
#include "Components/StaticMeshComponent.h"
#include "PhysicsEngine/BodyInstance.h"
#include "PhysicsEngine/BodySetup.h"
#include "Model.h"
#include "Engine/Polys.h"
#if WITH_RECAST && NX_UE_HAS_NAV_TILE_REF
#include "Detour/DetourNavMesh.h"
#endif
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
    auto Action = bWrite ? FNexusSchema::Enum(TEXT("Scene write"),{TEXT("CreateFolder"),TEXT("RenameFolder"),TEXT("DeleteFolder"),TEXT("SetActorFolder"),TEXT("SetSelection"),TEXT("MergeActors"),TEXT("CreateLevelInstance"),TEXT("EditLevelInstance"),TEXT("CommitLevelInstance"),TEXT("BuildNavigation"),TEXT("RefreshComponentPhysics")})
        : FNexusSchema::Enum(TEXT("Scene query"),{TEXT("Folders"),TEXT("FolderActors"),TEXT("Selection"),TEXT("Trace"),TEXT("NavigationStatus"),TEXT("NavigationContributors"),TEXT("NavigationTiles"),TEXT("LevelGeometry"),TEXT("Collision")});
    return FNexusSchema::Object().Required(TEXT("Action"), Action)
        .Prop(TEXT("FolderPath"),FNexusSchema::Str(TEXT("World Outliner folder path")))
        .Prop(TEXT("NewFolderPath"),FNexusSchema::Str(TEXT("Rename destination")))
        .Prop(TEXT("ActorPath"),FNexusSchema::Str(TEXT("Actor object path from discovery")))
        .Prop(TEXT("ComponentPath"),FNexusSchema::Str(TEXT("Exact loaded primitive component path; required for RefreshComponentPhysics")))
        .Prop(TEXT("BoundsMin"),Vector.ToSharedRef()).Prop(TEXT("BoundsMax"),Vector.ToSharedRef())
        .Prop(TEXT("Offset"),FNexusSchema::Int(TEXT("Contributors pagination or tile slot scan offset"),0,0,MAX_int32))
        .Prop(TEXT("ScanLimit"),FNexusSchema::Int(TEXT("Maximum tile slots inspected per query"),4096,1,65536))
        .Prop(TEXT("WaitSeconds"),FNexusSchema::Num(TEXT("NavigationStatus: observe existing work for up to 30 seconds, never starts a build"),0))
        .Prop(TEXT("bTraceComplex"),FNexusSchema::Bool(TEXT("Trace triangle geometry instead of simple collision"),true,false))
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

static TSharedPtr<FJsonObject> NexusScenePosition(const FVector& Position)
{
    auto Result = MakeShared<FJsonObject>();
    Result->SetNumberField(TEXT("X"), Position.X); Result->SetNumberField(TEXT("Y"), Position.Y); Result->SetNumberField(TEXT("Z"), Position.Z);
    return Result;
}

static void NexusSceneBounds(const TSharedPtr<FJsonObject>& Result, const FBox& Bounds)
{
    Result->SetBoolField(TEXT("bBoundsValid"), Bounds.IsValid != 0);
    if (Bounds.IsValid) { Result->SetObjectField(TEXT("BoundsMin"), NexusScenePosition(Bounds.Min)); Result->SetObjectField(TEXT("BoundsMax"), NexusScenePosition(Bounds.Max)); }
}

static TSharedPtr<FJsonObject> NexusNavigationStatus(UWorld* World)
{
    auto Result = MakeShared<FJsonObject>();
    UNavigationSystemV1* Navigation = FNavigationSystem::GetCurrent<UNavigationSystemV1>(World);
    Result->SetBoolField(TEXT("bNavigationAvailable"), Navigation != nullptr);
    Result->SetBoolField(TEXT("bNavigationBuilding"), Navigation && Navigation->IsNavigationBuildInProgress());
    Result->SetNumberField(TEXT("RemainingTasks"), Navigation ? Navigation->GetNumRemainingBuildTasks() : 0);
    Result->SetNumberField(TEXT("RunningTasks"), Navigation ? Navigation->GetNumRunningBuildTasks() : 0);
    Result->SetBoolField(TEXT("bBuildingLocked"), Navigation && Navigation->IsNavigationBuildingLocked(static_cast<uint8>(~ENavigationBuildLock::NoUpdateInEditor)));
    Result->SetStringField(TEXT("WorldPath"), World->GetPathName());
    return Result;
}

static FCapabilityResult NexusObserveNavigation(UWorld* World, double WaitSeconds)
{
    auto Operation = FNexusEditorOperations::Create(TEXT("EngineNavigationObserver"));
    if (!Operation) return FNexusEditorToolSchema::Result(Operation);
    auto Read = [Operation](UWorld* Current, bool bTimedOut) {
        auto Data = NexusNavigationStatus(Current); Data->SetBoolField(TEXT("bTimedOut"), bTimedOut);
        auto Result = MakeShared<FJsonObject>(); Result->SetObjectField(TEXT("CompatibilityResult"), Data);
        FNexusEditorOperations::Finish(Operation, TEXT("Completed"), Result);
    };
    if (WaitSeconds <= 0 || !NexusNavigationStatus(World)->GetBoolField(TEXT("bNavigationBuilding"))) { Read(World, false); return FNexusEditorToolSchema::Result(Operation); }
    const TWeakObjectPtr<UWorld> WeakWorld(World);
    const double Deadline = FPlatformTime::Seconds() + WaitSeconds;
    FNexusEditorControlServices::AddTicker(FTickerDelegate::CreateLambda([Operation, WeakWorld, Deadline, Read](float) {
        if (!Operation->IsRunning()) return false;
        UWorld* Current = WeakWorld.Get();
        if (!Current || !GEditor || GEditor->GetEditorWorldContext().World() != Current) { FNexusEditorOperations::Finish(Operation, TEXT("Failed"), nullptr, TEXT("Editor world changed while observing navigation")); return false; }
        const auto Status = NexusNavigationStatus(Current);
        Operation->Message = FString::Printf(TEXT("Navigation: %.0f remaining, %.0f running"), Status->GetNumberField(TEXT("RemainingTasks")), Status->GetNumberField(TEXT("RunningTasks")));
        const bool bBuilding = Status->GetBoolField(TEXT("bNavigationBuilding"));
        if (bBuilding && FPlatformTime::Seconds() < Deadline) return true;
        Read(Current, bBuilding); return false;
    }), 0.1f);
    return FNexusEditorToolSchema::Result(Operation);
}

static TSharedPtr<FJsonObject> NexusSceneDiagnostics(UWorld* World, const FString& Action, const TSharedPtr<FJsonObject>& Args, FString& Error)
{
    const FNexusArgs A(Args);
    const int32 Limit = static_cast<int32>(A.Num(TEXT("Limit"),100)), Offset = static_cast<int32>(A.Num(TEXT("Offset")));
    TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>(); TArray<TSharedPtr<FJsonValue>> Items;
    if (Action == TEXT("NavigationContributors") || Action == TEXT("NavigationTiles")) {
        FVector Minimum, Maximum;
        if (!NexusSceneVector(Args,TEXT("BoundsMin"),Minimum) || !NexusSceneVector(Args,TEXT("BoundsMax"),Maximum)
            || Minimum.X >= Maximum.X || Minimum.Y >= Maximum.Y || Minimum.Z >= Maximum.Z) { Error = TEXT("Explicit finite BoundsMin < BoundsMax are required"); return Data; }
        const FBox Bounds(Minimum,Maximum);
        UNavigationSystemV1* Navigation = FNavigationSystem::GetCurrent<UNavigationSystemV1>(World);
        Data = NexusNavigationStatus(World);
        if (Action == TEXT("NavigationContributors")) {
            const FNavigationOctree* Octree = Navigation ? Navigation->GetNavOctree() : nullptr;
            Data->SetBoolField(TEXT("bOctreeAvailable"), Octree != nullptr);
            int32 Total = 0;
            if (Octree) Octree->FindElementsWithBoundsTest(FBoxCenterAndExtent(Bounds), [&](const FNavigationOctreeElement& Element) {
                if (Total++ < Offset || Items.Num() >= Limit) return;
                auto Item = MakeShared<FJsonObject>(); NexusSceneBounds(Item, Element.Bounds.GetBox());
#if NX_UE_HAS_NAV_SOURCE_ELEMENT
                const UObject* Owner = Element.GetSourceElement()->GetWeakUObject().Get();
#else
                const UObject* Owner = Element.GetOwner();
#endif
                if (Owner) {
                    Item->SetStringField(TEXT("OwnerPath"),Owner->GetPathName()); Item->SetStringField(TEXT("OwnerClass"),Owner->GetClass()->GetPathName());
                    const UActorComponent* Component = Cast<UActorComponent>(Owner);
                    if (Component) Item->SetStringField(TEXT("ComponentPath"),Component->GetPathName());
                    const AActor* Actor = Component ? Component->GetOwner() : Cast<AActor>(Owner);
                    if (Actor) Item->SetStringField(TEXT("ActorPath"),Actor->GetPathName());
                }
                Item->SetBoolField(TEXT("bHasGeometry"),Element.Data->HasGeometry()); Item->SetBoolField(TEXT("bHasModifiers"),Element.Data->HasModifiers());
                Item->SetBoolField(TEXT("bPendingGeometry"),Element.Data->IsPendingLazyGeometryGathering());
                Item->SetBoolField(TEXT("bPendingModifiers"),Element.Data->IsPendingLazyModifiersGathering());
                TArray<TSharedPtr<FJsonValue>> Areas;
                for (const auto& Area : Element.Data->Modifiers.GetAreas()) {
                    if (Areas.Num() >= 16) break;
                    auto AreaData = MakeShared<FJsonObject>(); NexusSceneBounds(AreaData,Area.GetBounds());
                    const UClass* AreaClass = Area.GetAreaClass(); if (AreaClass) AreaData->SetStringField(TEXT("AreaClass"),AreaClass->GetPathName());
                    Areas.Add(MakeShared<FJsonValueObject>(AreaData));
                }
                Item->SetArrayField(TEXT("Areas"),Areas); Item->SetBoolField(TEXT("bAreasTruncated"),Element.Data->Modifiers.GetAreas().Num() > Areas.Num());
                Items.Add(MakeShared<FJsonValueObject>(Item));
            });
            Data->SetNumberField(TEXT("Total"),Total); Data->SetBoolField(TEXT("bTruncated"),Total > Offset + Items.Num());
        } else {
#if WITH_RECAST
            const ARecastNavMesh* Mesh = Navigation ? Cast<ARecastNavMesh>(Navigation->GetDefaultNavDataInstance(FNavigationSystem::DontCreate)) : nullptr;
            Data->SetBoolField(TEXT("bRecastAvailable"),Mesh != nullptr);
            int32 TileIndex = Offset;
            if (Mesh) {
                const int32 Count = Mesh->GetNavMeshTilesCount(), EndIndex = Offset + FMath::Min(static_cast<int32>(A.Num(TEXT("ScanLimit"),4096)),FMath::Max(0,Count-Offset));
                for (; TileIndex < EndIndex && Items.Num() < Limit; ++TileIndex) {
#if NX_UE_HAS_NAV_TILE_REF
                    // 逐槽转换为新引用，避免为一次有界查询枚举全部瓦片。
                    const dtNavMesh* Recast=Mesh->GetRecastMesh(); const dtMeshTile* Tile=Recast?Recast->getTile(TileIndex):nullptr;
                    const FBox TileBounds = Tile && Tile->header ? Mesh->GetNavMeshTileBounds(FNavTileRef(Recast->getTileRef(Tile))) : FBox(ForceInit);
#else
                    const FBox TileBounds = Mesh->GetNavMeshTileBounds(TileIndex);
#endif
                    if (!TileBounds.IsValid || !TileBounds.Intersect(Bounds)) continue;
                    auto Item = MakeShared<FJsonObject>(); Item->SetNumberField(TEXT("TileIndex"),TileIndex); NexusSceneBounds(Item,TileBounds); Items.Add(MakeShared<FJsonValueObject>(Item));
                }
                Data->SetStringField(TEXT("NavDataPath"),Mesh->GetPathName()); Data->SetNumberField(TEXT("TileSlotCount"),Count); Data->SetBoolField(TEXT("bTruncated"),TileIndex < Count);
            }
            Data->SetNumberField(TEXT("NextOffset"),TileIndex);
#else
            Data->SetBoolField(TEXT("bRecastAvailable"),false);
#endif
        }
    } else if (Action == TEXT("LevelGeometry")) {
        ULevel* Level = World->PersistentLevel;
        const FString Path = A.Str(TEXT("AssetPath"));
        if (!Path.IsEmpty()) { Level = nullptr; for (ULevel* Candidate : World->GetLevels()) if (Candidate && Candidate->GetOutermost()->GetName() == Path) Level = Candidate; }
        if (!Level) { Error = TEXT("AssetPath must identify a loaded level package"); return Data; }
        Data->SetStringField(TEXT("LevelPath"),Level->GetOutermost()->GetName());
        const UModel* Model = Level->Model; Data->SetBoolField(TEXT("bHasModel"),Model != nullptr);
        if (Model) { Data->SetStringField(TEXT("ModelPath"),Model->GetPathName()); NexusSceneBounds(Data,Model->Bounds.GetBox());
            Data->SetNumberField(TEXT("NodeCount"),Model->Nodes.Num()); Data->SetNumberField(TEXT("SurfaceCount"),Model->Surfs.Num()); Data->SetNumberField(TEXT("PointCount"),Model->Points.Num()); Data->SetNumberField(TEXT("PolygonCount"),Model->Polys ? Model->Polys->Element.Num() : 0);
            for (int32 NodeIndex=Offset; NodeIndex<Model->Nodes.Num() && Items.Num()<Limit; ++NodeIndex) {
                const FBspNode& Node=Model->Nodes[NodeIndex]; FBox Bounds(ForceInit);
                for (int32 Vertex=0; Vertex<Node.NumVertices; ++Vertex) if (Model->Verts.IsValidIndex(Node.iVertPool+Vertex)) { const int32 Point=Model->Verts[Node.iVertPool+Vertex].pVertex; if (Model->Points.IsValidIndex(Point)) Bounds+=FVector(Model->Points[Point]); }
                auto Item=MakeShared<FJsonObject>(); Item->SetNumberField(TEXT("NodeIndex"),NodeIndex); Item->SetNumberField(TEXT("SurfaceIndex"),Node.iSurf); NexusSceneBounds(Item,Bounds); Items.Add(MakeShared<FJsonValueObject>(Item));
            }
            Data->SetBoolField(TEXT("bTruncated"),Model->Nodes.Num()>Offset+Items.Num());
        }
    } else {
        const FString ComponentPath=A.Str(TEXT("ComponentPath")), ActorPath=A.Str(TEXT("ActorPath"));
        UPrimitiveComponent* Component=ComponentPath.IsEmpty()?nullptr:FindObject<UPrimitiveComponent>(nullptr,*ComponentPath);
        AActor* Actor=ActorPath.IsEmpty()?nullptr:FindObject<AActor>(nullptr,*ActorPath);
        if ((!Component && !Actor) || (Component && Component->GetWorld()!=World) || (Actor && Actor->GetWorld()!=World) || (Component && Actor && Component->GetOwner()!=Actor)) { Error=TEXT("Expected an exact loaded editor ActorPath or primitive ComponentPath"); return Data; }
        if (Action==TEXT("RefreshComponentPhysics") && (!Component || GEditor->IsPlayingSessionInEditor())) { Error=TEXT("RefreshComponentPhysics requires one exact ComponentPath and stopped PIE"); return Data; }
        TArray<UPrimitiveComponent*> Components; if (Component) Components.Add(Component); else Actor->GetComponents(Components);
        for (UPrimitiveComponent* Current : Components) {
            if (Items.Num()>=Limit) break;
            auto Item=MakeShared<FJsonObject>(); Item->SetStringField(TEXT("ComponentPath"),Current->GetPathName()); NexusSceneBounds(Item,Current->Bounds.GetBox());
            if (Action==TEXT("RefreshComponentPhysics")) { if (!Current->IsRegistered()) { Error=TEXT("Component is not registered; physics was not refreshed"); return Data; } Current->RecreatePhysicsState(); }
            Item->SetBoolField(TEXT("bRegistered"),Current->IsRegistered()); Item->SetBoolField(TEXT("bPhysicsStateCreated"),Current->IsPhysicsStateCreated());
            const FBodyInstance* Body=Current->GetBodyInstance(); Item->SetBoolField(TEXT("bValidBodyInstance"),Body && Body->IsValidBodyInstance());
            if (const UBodySetup* Setup=Current->GetBodySetup()) { Item->SetStringField(TEXT("BodySetupPath"),Setup->GetPathName()); Item->SetNumberField(TEXT("CollisionTraceFlag"),static_cast<int32>(Setup->CollisionTraceFlag)); }
            Item->SetNumberField(TEXT("CollisionEnabled"),static_cast<int32>(Current->GetCollisionEnabled())); Item->SetNumberField(TEXT("VisibilityResponse"),static_cast<int32>(Current->GetCollisionResponseToChannel(ECC_Visibility)));
            Item->SetBoolField(TEXT("bCanEverAffectNavigation"),Current->CanEverAffectNavigation());
            if (const UStaticMeshComponent* Mesh=Cast<UStaticMeshComponent>(Current)) if (Mesh->GetStaticMesh()) Item->SetStringField(TEXT("AssetPath"),Mesh->GetStaticMesh()->GetPathName());
            Items.Add(MakeShared<FJsonValueObject>(Item));
        }
        Data->SetBoolField(TEXT("bTruncated"),Components.Num()>Items.Num());
    }
    Data->SetArrayField(TEXT("Items"),Items); return Data;
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
    if (!Navigation || Navigation->IsNavigationBuildInProgress()) {
        auto Result = FCapabilityResult::MakeStateConflict(TEXT("Navigation unavailable or already building; inspect GetEditorScene Action=NavigationStatus with optional WaitSeconds before starting a build"));
        Result.TopFields = NexusNavigationStatus(World); return Result;
    }
    if (Navigation->IsNavigationBuildingLocked(static_cast<uint8>(~ENavigationBuildLock::NoUpdateInEditor)))
        return FCapabilityResult::MakeStateConflict(TEXT("Navigation building is locked; no build was started"));
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
        if (CurrentNavigation->IsNavigationBuildInProgress()) {
            Operation->Message = FString::Printf(TEXT("Navigation: %d remaining, %d running"),CurrentNavigation->GetNumRemainingBuildTasks(),CurrentNavigation->GetNumRunningBuildTasks());
            return true;
        }
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
    if (!bWrite && Action == TEXT("NavigationStatus")) {
        const double Wait=A.Num(TEXT("WaitSeconds")); if (!FMath::IsFinite(Wait) || Wait<0 || Wait>30) return FCapabilityResult::MakeArgInvalid(TEXT("WaitSeconds must be within [0,30]"));
        return NexusObserveNavigation(World,Wait);
    }
    if ((!bWrite && (Action==TEXT("NavigationContributors") || Action==TEXT("NavigationTiles") || Action==TEXT("LevelGeometry") || Action==TEXT("Collision"))) || (bWrite && Action==TEXT("RefreshComponentPhysics"))) {
        auto Operation=FNexusEditorOperations::Create(TEXT("Independent")); if (!Operation) return FNexusEditorToolSchema::Result(Operation);
        FString Error; const auto Data=NexusSceneDiagnostics(World,Action,Args,Error);
        if (!Error.IsEmpty()) { FNexusEditorOperations::Finish(Operation,TEXT("Failed"),nullptr,Error); return FCapabilityResult::MakeArgInvalid(Error); }
        auto Result=MakeShared<FJsonObject>(); Result->SetObjectField(TEXT("CompatibilityResult"),Data); FNexusEditorOperations::Finish(Operation,TEXT("Completed"),Result);
        return FNexusEditorToolSchema::Result(Operation);
    }
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
    else if (Action==TEXT("Trace")) {
        FCollisionQueryParams Query(SCENE_QUERY_STAT(NexusSceneTrace),A.Bool(TEXT("bTraceComplex"))); Query.bReturnFaceIndex=true;
        FHitResult Hit; const bool bHit=World->LineTraceSingleByChannel(Hit,Start,End,ECC_Visibility,Query);
        Data->SetBoolField(TEXT("bHit"),bHit); Data->SetBoolField(TEXT("bTraceComplex"),Query.bTraceComplex);
        if (Hit.GetActor()) Data->SetStringField(TEXT("ActorPath"),Hit.GetActor()->GetPathName());
        if (Hit.GetComponent()) Data->SetStringField(TEXT("ComponentPath"),Hit.GetComponent()->GetPathName());
        Data->SetNumberField(TEXT("FaceIndex"),Hit.FaceIndex); Data->SetObjectField(TEXT("ImpactPoint"),NexusScenePosition(Hit.ImpactPoint)); Data->SetObjectField(TEXT("ImpactNormal"),NexusScenePosition(Hit.ImpactNormal)); Data->SetNumberField(TEXT("Distance"),Hit.Distance);
    }
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
