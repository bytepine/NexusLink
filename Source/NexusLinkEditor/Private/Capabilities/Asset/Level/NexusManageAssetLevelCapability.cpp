// Copyright byteyang. All Rights Reserved.

#include "Capabilities/Asset/Level/NexusManageAssetLevelCapability.h"
#include "Utils/NexusBlueprintEditUtils.h"
#include "Utils/NexusArgs.h"
#include "NexusCapabilityRegistry.h"
#include "NexusMcpSchemaBuilder.h"
#include "Utils/NexusAssetUtils.h"
#include "Utils/NexusPropertyUtils.h"
#include "Utils/NexusAssetEditUtils.h"
#include "Utils/NexusEditorLevelUtils.h"
#include "Engine/World.h"
#include "Engine/Level.h"
#include "Model.h"
#include "GameFramework/WorldSettings.h"
#include "GameFramework/Actor.h"
#include "Engine/Blueprint.h"
#include "NexusMcpTool.h"
#include "Editor.h"
#include "LevelUtils.h"
#include "NavMesh/NavMeshBoundsVolume.h"
#include "ActorFactories/ActorFactoryBoxVolume.h"
#include "Builders/CubeBuilder.h"
#include "Components/SceneComponent.h"

/** 解析 "x,y,z" 或 "x y z" 为 FVector。 */
static bool NxParseVector3Text(const FString& Text, FVector& OutVec)
{
	TArray<FString> Parts;
	Text.ParseIntoArray(Parts, TEXT(","), true);
	if (Parts.Num() != 3)
	{
		Text.ParseIntoArrayWS(Parts);
	}
	if (Parts.Num() != 3)
	{
		return false;
	}
	OutVec.X = FCString::Atof(*Parts[0]);
	OutVec.Y = FCString::Atof(*Parts[1]);
	OutVec.Z = FCString::Atof(*Parts[2]);
	return true;
}

/** 解析 "pitch,yaw,roll" 为 FRotator。 */
static bool NxParseRotatorText(const FString& Text, FRotator& OutRot)
{
	TArray<FString> Parts;
	Text.ParseIntoArray(Parts, TEXT(","), true);
	if (Parts.Num() != 3)
	{
		Text.ParseIntoArrayWS(Parts);
	}
	if (Parts.Num() != 3)
	{
		return false;
	}
	OutRot.Pitch = FCString::Atof(*Parts[0]);
	OutRot.Yaw   = FCString::Atof(*Parts[1]);
	OutRot.Roll  = FCString::Atof(*Parts[2]);
	return true;
}

static UClass* ResolveSpawnClass(const FString& ClassName, const FString& AssetPath, FString& OutError)
{
	if (!AssetPath.IsEmpty())
	{
		if (UBlueprint* BP = FNexusAssetUtils::LoadAssetWithFallback<UBlueprint>(AssetPath))
		{
			if (BP->GeneratedClass)
			{
				return BP->GeneratedClass;
			}
			OutError = TEXT("Blueprint no GeneratedClass");
			return nullptr;
		}
		OutError = FString::Printf(TEXT("Blueprint not found: %s"), *AssetPath);
		return nullptr;
	}
	if (!ClassName.IsEmpty())
	{
		UClass* Class = FNexusAssetUtils::FindClassWithUPrefix(ClassName);
		if (Class && Class->IsChildOf(AActor::StaticClass()))
		{
			return Class;
		}
		OutError = FString::Printf(TEXT("ClassName '%s' not found or not Actor subclass"), *ClassName);
		return nullptr;
	}
	OutError = TEXT("SpawnActor requires ClassName or AssetPath");
	return nullptr;
}

void FNexusManageAssetLevelCapability::BuildDefinition(FNexusCapabilityDefinition& Out) const
{
	Out.Name = TEXT("ManageAssetLevel");
	Out.SearchAssetTypes = {TEXT("World")};
	Out.Description = TEXT("Batch edit level WorldSettings and Actors. Action=spawn/remove/SetProperty/ResetActorPropertyToDefault.");
	TSharedPtr<FJsonObject> OpSchema = FNexusSchema::Object()
		.Prop(TEXT("Action"),       FNexusSchema::Enum(TEXT("Action"),
			{ TEXT("SetProperty"), TEXT("SpawnActor"), TEXT("CreateNavMeshBoundsVolume"), TEXT("ResizeNavMeshBoundsVolume"), TEXT("RemoveActor"), TEXT("SetActorProperty"), TEXT("ResetActorPropertyToDefault") }))
		.Prop(TEXT("BoundsMin"), FNexusSchema::Object().Required(TEXT("X"), FNexusSchema::Num(TEXT("Minimum X"))).Required(TEXT("Y"), FNexusSchema::Num(TEXT("Minimum Y"))).Required(TEXT("Z"), FNexusSchema::Num(TEXT("Minimum Z"))).Build().ToSharedRef())
		.Prop(TEXT("BoundsMax"), FNexusSchema::Object().Required(TEXT("X"), FNexusSchema::Num(TEXT("Maximum X"))).Required(TEXT("Y"), FNexusSchema::Num(TEXT("Maximum Y"))).Required(TEXT("Z"), FNexusSchema::Num(TEXT("Maximum Z"))).Build().ToSharedRef())
		.Prop(TEXT("PropertyPath"), FNexusSchema::Str(TEXT("WorldSettings propertypath (SetProperty)")))
		.Prop(TEXT("Value"),        FNexusSchema::Str(TEXT("New property value string")))
		.Prop(TEXT("ExpectedValue"), FNexusSchema::Str(TEXT("Expected previous scalar Actor property value")))
		.Prop(TEXT("ClassName"),    FNexusSchema::Str(TEXT("Actor class name (SpawnActor)")))
		.Prop(TEXT("AssetPath"),    FNexusSchema::Str(TEXT("Blueprint path (SpawnActor)")))
		.Prop(TEXT("Location"),     FNexusSchema::Str(TEXT("Spawn location x,y,z (SpawnActor)")))
		.Prop(TEXT("Rotation"),     FNexusSchema::Str(TEXT("Spawn rotation pitch,yaw,roll (SpawnActor, optional)")))
		.Prop(TEXT("ActorName"),    FNexusSchema::Str(TEXT("Actor name or Label (remove/SetActorProperty)")))
		.Required({ TEXT("Action") })
		.Build();
	Out.InputSchema = FNexusSchema::Object()
		.Prop(TEXT("AssetPath"),  FNexusSchema::Str(TEXT("Level asset path (e.g. /Game/Maps/MyLevel)")))
		.Prop(TEXT("bVerify"), FNexusSchema::Bool(TEXT("Verify scalar SetActorProperty before saving; ResetActorPropertyToDefault may share the batch without ExpectedValue"), true, false))
		.Prop(TEXT("Operations"), FNexusSchema::ArrayOf(TEXT("Batch ops (at least one)"), OpSchema.ToSharedRef()))
		.Required({ TEXT("AssetPath"), TEXT("Operations") })
		.Build();
	Out.Tags = { FNexusMcpTags::Write, FNexusMcpTags::Editor };
	Out.ExtraSearchKeywords = { TEXT("Level"), TEXT("map"), TEXT("World"), TEXT("worldsettings"), TEXT("spawn"), TEXT("navigation"), TEXT("navmesh"), TEXT("bounds volume"), TEXT("brush"), TEXT("rebuild"), TEXT("resize") };
	Out.RelatedCapabilities = { TEXT("GetAssetLevel"), TEXT("SearchAsset") };
	Out.Prerequisites = { TEXT("EditorOnly") };
	Out.WhenToUse = TEXT("Edit WorldSettings or level Actors; persist with SaveAsset");
}

struct FLevelActionState
{
	UWorld* World = nullptr;
	bool bEditorWorld = false;
};

static FLevelActionState* LevelFrom(FNexusActionContext& Ctx)
{
	FLevelActionState* S = static_cast<FLevelActionState*>(Ctx.Target);
	if (S)
	{
		Ctx.Entry->SetBoolField(TEXT("bIsEditorWorld"), S->bEditorWorld);
	}
	return S;
}

static void HandleLevel_CreateNavBounds(const TSharedPtr<FJsonObject>& Op, FNexusActionContext& Ctx)
{
	UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	if (!World || GEditor->IsPlayingSessionInEditor())
	{ Ctx.Entry->SetStringField(TEXT("Error"), TEXT("An editor world with PIE stopped is required")); return; }
	ULevel* TargetLevel = nullptr;
	const FString Package = FNexusEditorLevelUtils::NormalizeLevelPackagePath(Ctx.AssetPath);
	for (ULevel* Level : World->GetLevels())
		if (Level && Level->GetOutermost()->GetName() == Package) TargetLevel = Level;
	if (!TargetLevel || FLevelUtils::IsLevelLocked(TargetLevel))
	{ Ctx.Entry->SetStringField(TEXT("Error"), TEXT("AssetPath must identify an unlocked loaded editor level from GetEditorContext.CurrentMap.Levels")); return; }
	FVector Minimum, Maximum;
	const auto ReadVector = [&](const TCHAR* Key, FVector& Value)
	{
		const TSharedPtr<FJsonObject>* Vector = nullptr;
		double X = 0, Y = 0, Z = 0;
		if (!Op->TryGetObjectField(Key, Vector) || !(*Vector)->TryGetNumberField(TEXT("X"), X)
			|| !(*Vector)->TryGetNumberField(TEXT("Y"), Y) || !(*Vector)->TryGetNumberField(TEXT("Z"), Z)) return false;
		Value = FVector(X, Y, Z);
		return !Value.ContainsNaN();
	};
	if (!ReadVector(TEXT("BoundsMin"), Minimum) || !ReadVector(TEXT("BoundsMax"), Maximum)
		|| Minimum.X >= Maximum.X || Minimum.Y >= Maximum.Y || Minimum.Z >= Maximum.Z)
	{ Ctx.Entry->SetStringField(TEXT("Error"), TEXT("BoundsMin/BoundsMax require finite XYZ with positive size on every axis")); return; }
	const FVector Size = Maximum - Minimum;
	if (Size.GetMax() > 10000000 || Size.GetMin() < 1)
	{ Ctx.Entry->SetStringField(TEXT("Error"), TEXT("Bounds dimensions must be between 1 and 10000000 Unreal units")); return; }
	const bool bResize = FNexusArgs(Op).Str(TEXT("Action")) == TEXT("ResizeNavMeshBoundsVolume");
	ANavMeshBoundsVolume* Volume = nullptr;
	if (bResize)
	{
		FLevelActionState* State = LevelFrom(Ctx);
		Volume = State ? Cast<ANavMeshBoundsVolume>(FNexusEditorLevelUtils::FindLevelActorByNameOrLabel(State->World, FNexusArgs(Op).Str(TEXT("ActorName")), false)) : nullptr;
		if (!Volume) { Ctx.Entry->SetStringField(TEXT("Error"), TEXT("Resize requires an exact NavMeshBoundsVolume ActorName in the requested level")); return; }
	}
	TargetLevel->Modify();
	if (!bResize)
	{
		UActorFactoryBoxVolume* Factory = NewObject<UActorFactoryBoxVolume>();
		Factory->NewActorClass = ANavMeshBoundsVolume::StaticClass();
		Volume = Cast<ANavMeshBoundsVolume>(Factory->CreateActor(nullptr, TargetLevel, FTransform((Minimum + Maximum) * 0.5)));
	}
	if (!Volume) { Ctx.Entry->SetStringField(TEXT("Error"), TEXT("Volume actor factory failed")); return; }
	if (!Volume->Brush)
	{
		if (!bResize) World->EditorDestroyActor(Volume, true);
		Ctx.Entry->SetStringField(TEXT("Error"), TEXT("Target volume has no brush model to rebuild")); return;
	}
	Volume->Modify();
	Volume->Brush->Modify();
	UCubeBuilder* Builder = NewObject<UCubeBuilder>(Volume, NAME_None, RF_Transactional);
	Builder->X = Size.X; Builder->Y = Size.Y; Builder->Z = Size.Z;
	Volume->BrushBuilder = Builder;
	if (!Builder->Build(World, Volume))
	{
		if (!bResize) World->EditorDestroyActor(Volume, true);
		Ctx.Entry->SetStringField(TEXT("Error"), TEXT("Cube brush geometry build failed")); return;
	}
	Volume->SetActorTransform(FTransform(FQuat::Identity, (Minimum + Maximum) * 0.5, FVector::OneVector));
	const FString Label = FNexusArgs(Op).Str(TEXT("ActorName"));
	if (!bResize && !Label.IsEmpty()) Volume->SetActorLabel(Label);
	FNexusEditorLevelUtils::RefreshActorAfterPropertyEdit(Volume);
	const FBox ActualBounds = Volume->GetComponentsBoundingBox(true);
	for (const auto& Pair : {TPair<FString, FVector>(TEXT("BoundsMin"), ActualBounds.Min), TPair<FString, FVector>(TEXT("BoundsMax"), ActualBounds.Max)})
	{
		auto Vector = MakeShared<FJsonObject>();
		Vector->SetNumberField(TEXT("X"), Pair.Value.X); Vector->SetNumberField(TEXT("Y"), Pair.Value.Y); Vector->SetNumberField(TEXT("Z"), Pair.Value.Z);
		Ctx.Entry->SetObjectField(Pair.Key, Vector);
	}
	Ctx.Entry->SetStringField(TEXT("ActorName"), Volume->GetName());
	Ctx.Entry->SetStringField(TEXT("ActorPath"), Volume->GetPathName());
	Ctx.Entry->SetStringField(TEXT("LevelPath"), Package);
	Ctx.Entry->SetBoolField(TEXT("bBrushCreated"), true);
	Ctx.Entry->SetStringField(TEXT("Note"), TEXT("Save this level, then ControlEditorScene Action=BuildNavigation; creation does not build navigation"));
}

static void HandleLevel_SpawnActor(const TSharedPtr<FJsonObject>& Op, FNexusActionContext& Ctx)
{
	FLevelActionState* S = LevelFrom(Ctx);
	const FNexusArgs A(Op);
	const FString ClassName = A.Str(TEXT("ClassName"));
	const FString SpawnAssetPath = A.Str(TEXT("AssetPath"));
	const FString LocationStr = A.Str(TEXT("Location"));
	const FString RotationStr = A.Str(TEXT("Rotation"));
	FVector Location(0.f, 0.f, 0.f);
	if (!LocationStr.IsEmpty() && !NxParseVector3Text(LocationStr, Location))
	{
		Ctx.Entry->SetStringField(TEXT("Error"), TEXT("location format must be x,y,z"));
		return;
	}
	FRotator Rotation = FRotator::ZeroRotator;
	if (!RotationStr.IsEmpty() && !NxParseRotatorText(RotationStr, Rotation))
	{
		Ctx.Entry->SetStringField(TEXT("Error"), TEXT("rotation format must be pitch,yaw,roll"));
		return;
	}
	FString ClassErr;
	UClass* SpawnClass = ResolveSpawnClass(ClassName, SpawnAssetPath, ClassErr);
	if (!SpawnClass)
	{
		Ctx.Entry->SetStringField(TEXT("Error"), ClassErr);
		return;
	}
	AActor* Spawned = nullptr;
	FString SpawnErr;
	if (!FNexusEditorLevelUtils::SpawnActorInLevelWorld(S->World, SpawnClass, Location, Rotation, Spawned, SpawnErr))
	{
		Ctx.Entry->SetStringField(TEXT("Error"), SpawnErr);
		return;
	}
	Ctx.Entry->SetStringField(TEXT("ActorName"), Spawned->GetName());
	Ctx.Entry->SetStringField(TEXT("ActorClass"), Spawned->GetClass()->GetName());
	Ctx.Entry->SetStringField(TEXT("Note"), TEXT("persist with SaveAsset"));
}

static void HandleLevel_RemoveActor(const TSharedPtr<FJsonObject>& Op, FNexusActionContext& Ctx)
{
	FLevelActionState* S = LevelFrom(Ctx);
	const FString ActorName = FNexusArgs(Op).Str(TEXT("ActorName"));
	if (ActorName.IsEmpty())
	{
		Ctx.Entry->SetStringField(TEXT("Error"), TEXT("RemoveActor requires ActorName"));
		return;
	}
	AActor* Actor = FNexusEditorLevelUtils::FindLevelActorByNameOrLabel(S->World, ActorName);
	if (!Actor)
	{
		Ctx.Entry->SetStringField(TEXT("Error"), FString::Printf(TEXT("Actor not found: %s"), *ActorName));
		return;
	}
	FString RemoveErr;
	if (!FNexusEditorLevelUtils::RemoveLevelActor(S->World, Actor, RemoveErr))
	{
		Ctx.Entry->SetStringField(TEXT("Error"), RemoveErr);
		return;
	}
	Ctx.Entry->SetStringField(TEXT("RemovedActor"), ActorName);
	Ctx.Entry->SetStringField(TEXT("Note"), TEXT("persist with SaveAsset"));
}

static void HandleLevel_SetActorProperty(const TSharedPtr<FJsonObject>& Op, FNexusActionContext& Ctx)
{
	FLevelActionState* S = LevelFrom(Ctx);
	const FNexusArgs A(Op);
	const FString ActorName = A.Str(TEXT("ActorName"));
	const FString PropPath = A.Str(TEXT("PropertyPath"));
	const FString Value = A.Str(TEXT("Value"));
	if (ActorName.IsEmpty() || PropPath.IsEmpty() || !Op->HasTypedField<EJson::String>(TEXT("Value")))
	{
		Ctx.Entry->SetStringField(TEXT("Error"), TEXT("SetActorProperty requires ActorName、PropertyPath、value"));
		return;
	}
	if (FNexusAssetEditUtils::IsVerified(Ctx.Args) || Op->HasField(TEXT("ExpectedValue")))
	{
		FNexusAssetEditUtils::Apply(S->World, TEXT("ManageAssetLevel"), Op, FNexusAssetEditUtils::IsVerified(Ctx.Args), Ctx.Entry);
		return;
	}
	AActor* Actor = FNexusEditorLevelUtils::FindLevelActorByNameOrLabel(S->World, ActorName);
	if (!Actor)
	{
		Ctx.Entry->SetStringField(TEXT("Error"), FString::Printf(TEXT("Actor not found: %s"), *ActorName));
		return;
	}
	Actor->Modify();
	TInlineComponentArray<UActorComponent*> Components(Actor);
	for (UActorComponent* Component : Components) if (Component) Component->Modify();
	FString OldVal, ActualVal, PropErr;
	if (!FNexusPropertyUtils::WritePropertyAndEcho(Actor, { PropPath }, 0, Value, OldVal, ActualVal, PropErr))
	{
		Ctx.Entry->SetStringField(TEXT("Error"), PropErr);
		return;
	}
	S->World->MarkPackageDirty();
	FNexusEditorLevelUtils::RefreshActorAfterPropertyEdit(Actor);
	Ctx.Entry->SetStringField(TEXT("ActorName"), ActorName);
	Ctx.Entry->SetStringField(TEXT("PropertyPath"), PropPath);
	if (!OldVal.IsEmpty()) Ctx.Entry->SetStringField(TEXT("OldValue"), OldVal);
	if (!ActualVal.IsEmpty()) Ctx.Entry->SetStringField(TEXT("NewValue"), ActualVal);
	Ctx.Entry->SetStringField(TEXT("Note"), TEXT("persist with SaveAsset"));
}

static void HandleLevel_ResetActorPropertyToDefault(const TSharedPtr<FJsonObject>& Op, FNexusActionContext& Ctx)
{
	FLevelActionState* S = LevelFrom(Ctx);
	const FNexusArgs A(Op);
	const FString ActorName = A.Str(TEXT("ActorName"));
	const FString PropPath = A.Str(TEXT("PropertyPath"));
	if (ActorName.IsEmpty() || PropPath.IsEmpty())
	{
		Ctx.Entry->SetStringField(TEXT("Error"), TEXT("ResetActorPropertyToDefault requires ActorName and PropertyPath"));
		return;
	}
	AActor* Actor = FNexusEditorLevelUtils::FindLevelActorByNameOrLabel(S->World, ActorName);
	if (!Actor)
	{
		Ctx.Entry->SetStringField(TEXT("Error"), FString::Printf(TEXT("Actor not found: %s"), *ActorName));
		return;
	}
	TArray<FString> Segments;
	PropPath.ParseIntoArray(Segments, TEXT("."), true);
	FProperty* Prop = nullptr;
	void* ValPtr = nullptr;
	FString PropErr;
	if (!FNexusBlueprintEditUtils::ResolveActorDefaultProperty(Actor, Segments, Prop, ValPtr, PropErr))
	{
		Ctx.Entry->SetStringField(TEXT("Error"), PropErr);
		return;
	}
	FString CdoText;
	FNexusPropertyUtils::ExportText(Prop, CdoText, ValPtr);
	Actor->Modify();
	TInlineComponentArray<UActorComponent*> Components(Actor);
	for (UActorComponent* Component : Components) if (Component) Component->Modify();
	FString OldVal, ActualVal;
	if (!FNexusPropertyUtils::WritePropertyAndEcho(Actor, Segments, 0, CdoText, OldVal, ActualVal, PropErr))
	{
		Ctx.Entry->SetStringField(TEXT("Error"), PropErr);
		return;
	}
	S->World->MarkPackageDirty();
	FNexusEditorLevelUtils::RefreshActorAfterPropertyEdit(Actor);
	Ctx.Entry->SetStringField(TEXT("ActorName"), ActorName);
	Ctx.Entry->SetStringField(TEXT("PropertyPath"), PropPath);
	if (!OldVal.IsEmpty()) Ctx.Entry->SetStringField(TEXT("OldValue"), OldVal);
	Ctx.Entry->SetStringField(TEXT("NewValue"), ActualVal);
	Ctx.Entry->SetStringField(TEXT("Note"), TEXT("persist with SaveAsset"));
}

static void HandleLevel_SetProperty(const TSharedPtr<FJsonObject>& Op, FNexusActionContext& Ctx)
{
	FLevelActionState* S = LevelFrom(Ctx);
	ULevel* Level = FNexusEditorLevelUtils::ResolveLevel(S->World);
	AWorldSettings* WorldSettings = Level ? Level->GetWorldSettings() : nullptr;
	if (!WorldSettings)
	{
		Ctx.Entry->SetStringField(TEXT("Error"), TEXT("Level has no WorldSettings"));
		return;
	}
	const FNexusArgs A(Op);
	const FString PropPath = A.Str(TEXT("PropertyPath"));
	const FString Value = A.Str(TEXT("Value"));
	if (PropPath.IsEmpty() || !Op->HasTypedField<EJson::String>(TEXT("Value")))
	{
		Ctx.Entry->SetStringField(TEXT("Error"), TEXT("SetProperty requires PropertyPath and value"));
		return;
	}
	FString OldVal, ActualVal, PropErr;
	if (!FNexusPropertyUtils::WritePropertyAndEcho(WorldSettings, { PropPath }, 0, Value, OldVal, ActualVal, PropErr))
	{
		Ctx.Entry->SetStringField(TEXT("Error"), PropErr);
		return;
	}
	S->World->MarkPackageDirty();
	Ctx.Entry->SetStringField(TEXT("PropertyPath"), PropPath);
	if (!OldVal.IsEmpty()) Ctx.Entry->SetStringField(TEXT("OldValue"), OldVal);
	if (!ActualVal.IsEmpty()) Ctx.Entry->SetStringField(TEXT("NewValue"), ActualVal);
	Ctx.Entry->SetStringField(TEXT("Note"), TEXT("persist with SaveAsset"));
}

bool FNexusManageAssetLevelCapability::PrepareTarget(
	const TSharedPtr<FJsonObject>& Args,
	TSharedPtr<FJsonObject>& Entry,
	void*& OutTarget,
	FString& OutError) const
{
	if (!FNexusAssetEditUtils::ValidateOperations(TEXT("ManageAssetLevel"), Args, OutError)) return false;
	const FString AssetPath = FNexusArgs(Args).Str(TEXT("AssetPath"));
	Entry->SetStringField(TEXT("Path"), AssetPath);
	bool bEditorWorld = false;
	FString LoadErr;
	UWorld* World = FNexusEditorLevelUtils::LoadLevelWorldForWrite(AssetPath, bEditorWorld, LoadErr);
	if (!World)
	{
		OutError = LoadErr;
		return false;
	}
	ULevel* Level = FNexusEditorLevelUtils::ResolveLevel(World);
	if (!Level || FLevelUtils::IsLevelLocked(Level))
	{ OutError = TEXT("Requested level is unavailable or locked"); return false; }
	FLevelActionState* State = new FLevelActionState();
	State->World = World;
	State->bEditorWorld = bEditorWorld;
	OutTarget = State;
	return true;
}

void FNexusManageAssetLevelCapability::FinalizeTarget(void* Target) const
{
	delete static_cast<FLevelActionState*>(Target);
}

void FNexusManageAssetLevelCapability::RegisterActions(TMap<FString, FNexusActionHandler>& OutHandlers) const
{
	OutHandlers.Add(TEXT("SpawnActor"),         &HandleLevel_SpawnActor);
	OutHandlers.Add(TEXT("CreateNavMeshBoundsVolume"), &HandleLevel_CreateNavBounds);
	OutHandlers.Add(TEXT("ResizeNavMeshBoundsVolume"), &HandleLevel_CreateNavBounds);
	OutHandlers.Add(TEXT("RemoveActor"),        &HandleLevel_RemoveActor);
	OutHandlers.Add(TEXT("SetActorProperty"),  &HandleLevel_SetActorProperty);
	OutHandlers.Add(TEXT("ResetActorPropertyToDefault"), &HandleLevel_ResetActorPropertyToDefault);
	OutHandlers.Add(TEXT("SetProperty"),        &HandleLevel_SetProperty);
}

REGISTER_MCP_CAPABILITY(FNexusManageAssetLevelCapability)
