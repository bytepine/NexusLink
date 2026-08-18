// Copyright byteyang. All Rights Reserved.

#include "Capabilities/Asset/Level/NexusManageAssetLevelCapability.h"
#include "Utils/NexusArgs.h"
#include "NexusCapabilityRegistry.h"
#include "NexusMcpSchemaBuilder.h"
#include "Utils/NexusAssetUtils.h"
#include "Utils/NexusPropertyUtils.h"
#include "Utils/NexusEditorLevelUtils.h"
#include "Engine/World.h"
#include "Engine/Level.h"
#include "GameFramework/WorldSettings.h"
#include "GameFramework/Actor.h"
#include "Engine/Blueprint.h"
#include "NexusMcpTool.h"

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
		OutError = FString::Printf(TEXT("className '%s' not found or not Actor subclass"), *ClassName);
		return nullptr;
	}
	OutError = TEXT("spawn_actor requires className or assetPath");
	return nullptr;
}

void FManageAssetLevelCapability::BuildDefinition(FNexusCapabilityDefinition& Out) const
{
	Out.Name = TEXT("manage_asset_level");
	Out.SearchAssetTypes = {TEXT("World")};
	Out.Description = TEXT("Batch edit level WorldSettings and Actors. action=spawn/remove/set_property.");
	TSharedPtr<FJsonObject> OpSchema = FNexusSchema::Object()
		.Prop(TEXT("action"),       FNexusSchema::Enum(TEXT("Action"),
			{ TEXT("set_property"), TEXT("spawn_actor"), TEXT("remove_actor"), TEXT("set_actor_property") }))
		.Prop(TEXT("propertyPath"), FNexusSchema::Str(TEXT("WorldSettings propertypath (set_property)")))
		.Prop(TEXT("value"),        FNexusSchema::Str(TEXT("New property value string")))
		.Prop(TEXT("className"),    FNexusSchema::Str(TEXT("Actor class name (spawn_actor)")))
		.Prop(TEXT("assetPath"),    FNexusSchema::Str(TEXT("Blueprint path (spawn_actor)")))
		.Prop(TEXT("location"),     FNexusSchema::Str(TEXT("Spawn location x,y,z (spawn_actor)")))
		.Prop(TEXT("rotation"),     FNexusSchema::Str(TEXT("Spawn rotation pitch,yaw,roll (spawn_actor, optional)")))
		.Prop(TEXT("actorName"),    FNexusSchema::Str(TEXT("Actor name or Label (remove/set_actor_property)")))
		.Required({ TEXT("action") })
		.Build();
	Out.InputSchema = FNexusSchema::Object()
		.Prop(TEXT("assetPath"),  FNexusSchema::Str(TEXT("Level asset path (e.g. /Game/Maps/MyLevel)")))
		.Prop(TEXT("operations"), FNexusSchema::ArrayOf(TEXT("Batch ops (at least one)"), OpSchema.ToSharedRef()))
		.Required({ TEXT("assetPath"), TEXT("operations") })
		.Build();
	Out.Tags = { FNexusMcpTags::Write, FNexusMcpTags::Editor };
	Out.ExtraSearchKeywords = { TEXT("level"), TEXT("map"), TEXT("world"), TEXT("worldsettings"), TEXT("spawn") };
	Out.RelatedCapabilities = { TEXT("get_asset_level"), TEXT("search_asset") };
	Out.Prerequisites = { TEXT("editor_only") };
	Out.WhenToUse = TEXT("Edit WorldSettings or level Actors; persist with save_asset");
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
		Ctx.Entry->SetBoolField(TEXT("isEditorWorld"), S->bEditorWorld);
	}
	return S;
}

static void HandleLevel_SpawnActor(const TSharedPtr<FJsonObject>& Op, FNexusActionContext& Ctx)
{
	FLevelActionState* S = LevelFrom(Ctx);
	const FNexusArgs A(Op);
	const FString ClassName = A.Str(TEXT("className"));
	const FString SpawnAssetPath = A.Str(TEXT("assetPath"));
	const FString LocationStr = A.Str(TEXT("location"));
	const FString RotationStr = A.Str(TEXT("rotation"));
	FVector Location(0.f, 0.f, 0.f);
	if (!LocationStr.IsEmpty() && !NxParseVector3Text(LocationStr, Location))
	{
		Ctx.Entry->SetStringField(TEXT("error"), TEXT("location format must be x,y,z"));
		return;
	}
	FRotator Rotation = FRotator::ZeroRotator;
	if (!RotationStr.IsEmpty() && !NxParseRotatorText(RotationStr, Rotation))
	{
		Ctx.Entry->SetStringField(TEXT("error"), TEXT("rotation format must be pitch,yaw,roll"));
		return;
	}
	FString ClassErr;
	UClass* SpawnClass = ResolveSpawnClass(ClassName, SpawnAssetPath, ClassErr);
	if (!SpawnClass)
	{
		Ctx.Entry->SetStringField(TEXT("error"), ClassErr);
		return;
	}
	AActor* Spawned = nullptr;
	FString SpawnErr;
	if (!FNexusEditorLevelUtils::SpawnActorInLevelWorld(S->World, SpawnClass, Location, Rotation, Spawned, SpawnErr))
	{
		Ctx.Entry->SetStringField(TEXT("error"), SpawnErr);
		return;
	}
	Ctx.Entry->SetStringField(TEXT("actorName"), Spawned->GetName());
	Ctx.Entry->SetStringField(TEXT("actorClass"), Spawned->GetClass()->GetName());
	Ctx.Entry->SetStringField(TEXT("note"), TEXT("persist with save_asset"));
}

static void HandleLevel_RemoveActor(const TSharedPtr<FJsonObject>& Op, FNexusActionContext& Ctx)
{
	FLevelActionState* S = LevelFrom(Ctx);
	const FString ActorName = FNexusArgs(Op).Str(TEXT("actorName"));
	if (ActorName.IsEmpty())
	{
		Ctx.Entry->SetStringField(TEXT("error"), TEXT("remove_actor requires actorName"));
		return;
	}
	AActor* Actor = FNexusEditorLevelUtils::FindLevelActorByNameOrLabel(S->World, ActorName);
	if (!Actor)
	{
		Ctx.Entry->SetStringField(TEXT("error"), FString::Printf(TEXT("Actor not found: %s"), *ActorName));
		return;
	}
	FString RemoveErr;
	if (!FNexusEditorLevelUtils::RemoveLevelActor(S->World, Actor, RemoveErr))
	{
		Ctx.Entry->SetStringField(TEXT("error"), RemoveErr);
		return;
	}
	Ctx.Entry->SetStringField(TEXT("removedActor"), ActorName);
	Ctx.Entry->SetStringField(TEXT("note"), TEXT("persist with save_asset"));
}

static void HandleLevel_SetActorProperty(const TSharedPtr<FJsonObject>& Op, FNexusActionContext& Ctx)
{
	FLevelActionState* S = LevelFrom(Ctx);
	const FNexusArgs A(Op);
	const FString ActorName = A.Str(TEXT("actorName"));
	const FString PropPath = A.Str(TEXT("propertyPath"));
	const FString Value = A.Str(TEXT("value"));
	if (ActorName.IsEmpty() || PropPath.IsEmpty() || Value.IsEmpty())
	{
		Ctx.Entry->SetStringField(TEXT("error"), TEXT("set_actor_property requires actorName、propertyPath、value"));
		return;
	}
	AActor* Actor = FNexusEditorLevelUtils::FindLevelActorByNameOrLabel(S->World, ActorName);
	if (!Actor)
	{
		Ctx.Entry->SetStringField(TEXT("error"), FString::Printf(TEXT("Actor not found: %s"), *ActorName));
		return;
	}
	FString OldVal, ActualVal, PropErr;
	if (!FNexusPropertyUtils::WritePropertyAndEcho(Actor, { PropPath }, 0, Value, OldVal, ActualVal, PropErr))
	{
		Ctx.Entry->SetStringField(TEXT("error"), PropErr);
		return;
	}
	S->World->MarkPackageDirty();
	Ctx.Entry->SetStringField(TEXT("actorName"), ActorName);
	Ctx.Entry->SetStringField(TEXT("propertyPath"), PropPath);
	if (!OldVal.IsEmpty()) Ctx.Entry->SetStringField(TEXT("oldValue"), OldVal);
	if (!ActualVal.IsEmpty()) Ctx.Entry->SetStringField(TEXT("newValue"), ActualVal);
	Ctx.Entry->SetStringField(TEXT("note"), TEXT("persist with save_asset"));
}

static void HandleLevel_SetProperty(const TSharedPtr<FJsonObject>& Op, FNexusActionContext& Ctx)
{
	FLevelActionState* S = LevelFrom(Ctx);
	AWorldSettings* WorldSettings = S->World->GetWorldSettings();
	if (!WorldSettings)
	{
		Ctx.Entry->SetStringField(TEXT("error"), TEXT("Level has no WorldSettings"));
		return;
	}
	const FNexusArgs A(Op);
	const FString PropPath = A.Str(TEXT("propertyPath"));
	const FString Value = A.Str(TEXT("value"));
	if (PropPath.IsEmpty() || Value.IsEmpty())
	{
		Ctx.Entry->SetStringField(TEXT("error"), TEXT("set_property requires propertyPath and value"));
		return;
	}
	FString OldVal, ActualVal, PropErr;
	if (!FNexusPropertyUtils::WritePropertyAndEcho(WorldSettings, { PropPath }, 0, Value, OldVal, ActualVal, PropErr))
	{
		Ctx.Entry->SetStringField(TEXT("error"), PropErr);
		return;
	}
	S->World->MarkPackageDirty();
	Ctx.Entry->SetStringField(TEXT("propertyPath"), PropPath);
	if (!OldVal.IsEmpty()) Ctx.Entry->SetStringField(TEXT("oldValue"), OldVal);
	if (!ActualVal.IsEmpty()) Ctx.Entry->SetStringField(TEXT("newValue"), ActualVal);
	Ctx.Entry->SetStringField(TEXT("note"), TEXT("persist with save_asset"));
}

bool FManageAssetLevelCapability::PrepareTarget(
	const TSharedPtr<FJsonObject>& Args,
	TSharedPtr<FJsonObject>& Entry,
	void*& OutTarget,
	FString& OutError) const
{
	const FString AssetPath = FNexusArgs(Args).Str(TEXT("assetPath"));
	Entry->SetStringField(TEXT("path"), AssetPath);
	bool bEditorWorld = false;
	FString LoadErr;
	UWorld* World = FNexusEditorLevelUtils::LoadLevelWorldForWrite(AssetPath, bEditorWorld, LoadErr);
	if (!World)
	{
		OutError = LoadErr;
		return false;
	}
	FLevelActionState* State = new FLevelActionState();
	State->World = World;
	State->bEditorWorld = bEditorWorld;
	OutTarget = State;
	return true;
}

void FManageAssetLevelCapability::FinalizeTarget(void* Target) const
{
	delete static_cast<FLevelActionState*>(Target);
}

void FManageAssetLevelCapability::RegisterActions(TMap<FString, FNexusActionHandler>& OutHandlers) const
{
	OutHandlers.Add(TEXT("spawn_actor"),         &HandleLevel_SpawnActor);
	OutHandlers.Add(TEXT("remove_actor"),        &HandleLevel_RemoveActor);
	OutHandlers.Add(TEXT("set_actor_property"),  &HandleLevel_SetActorProperty);
	OutHandlers.Add(TEXT("set_property"),        &HandleLevel_SetProperty);
}

REGISTER_MCP_CAPABILITY(FManageAssetLevelCapability)
