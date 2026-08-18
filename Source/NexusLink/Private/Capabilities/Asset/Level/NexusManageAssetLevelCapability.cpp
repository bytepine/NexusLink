// Copyright byteyang. All Rights Reserved.

#include "Capabilities/Asset/Level/NexusManageAssetLevelCapability.h"
#include "Utils/NexusCapabilityResultBuilder.h"
#include "NexusCapabilityRegistry.h"
#include "NexusMcpSchemaBuilder.h"
#include "Utils/NexusAssetUtils.h"
#include "Utils/NexusJsonUtils.h"
#include "Utils/NexusPropertyUtils.h"
#include "Utils/NexusEditorLevelUtils.h"
#include "Engine/World.h"
#include "Engine/Level.h"
#include "GameFramework/WorldSettings.h"
#include "GameFramework/Actor.h"
#include "Engine/Blueprint.h"
#include "NexusMcpTool.h"

namespace
{
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

FCapabilityResult FManageAssetLevelCapability::Execute(const TSharedPtr<FJsonObject>& Arguments) const
{
	return FNexusCapabilityResultBuilder::Build([&](auto& OutEntries, auto& OutTop, auto& OutError)
	{
		FString AssetPath;
		if (!FNexusCapability::RequireString(Arguments, TEXT("assetPath"), AssetPath, OutEntries, {})) return;

		bool bEditorWorld = false;
		FString LoadErr;
		UWorld* World = FNexusEditorLevelUtils::LoadLevelWorldForWrite(AssetPath, bEditorWorld, LoadErr);
		if (!World)
		{
			FNexusCapability::EmitError(OutEntries, {{TEXT("path"), AssetPath}}, LoadErr);
			return;
		}

		const TArray<TSharedPtr<FJsonValue>> Ops = FNexusJsonUtils::ExtractOperations(Arguments);
		if (Ops.Num() == 0)
		{
			FNexusCapability::EmitError(OutEntries, {{TEXT("path"), AssetPath}}, TEXT("Missing or empty operations"));
			return;
		}

		for (const TSharedPtr<FJsonValue>& OpVal : Ops)
		{
		const TSharedPtr<FJsonObject>* OpObjPtr = nullptr;
		if (!OpVal.IsValid() || !OpVal->TryGetObject(OpObjPtr) || !OpObjPtr) continue;
		const TSharedPtr<FJsonObject>& OpArgs = *OpObjPtr;

		FString Action;
		OpArgs->TryGetStringField(TEXT("action"), Action);

		TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
		Entry->SetStringField(TEXT("path"), AssetPath);
		Entry->SetStringField(TEXT("action"), Action);
		Entry->SetBoolField(TEXT("isEditorWorld"), bEditorWorld);

		if (Action.Equals(TEXT("spawn_actor"), ESearchCase::IgnoreCase))
		{
			FString ClassName, SpawnAssetPath, LocationStr, RotationStr;
			if (OpArgs.IsValid())
			{
				OpArgs->TryGetStringField(TEXT("className"), ClassName);
				OpArgs->TryGetStringField(TEXT("assetPath"), SpawnAssetPath);
				OpArgs->TryGetStringField(TEXT("location"), LocationStr);
				OpArgs->TryGetStringField(TEXT("rotation"), RotationStr);
			}
			FVector Location(0.f, 0.f, 0.f);
			if (!LocationStr.IsEmpty() && !NxParseVector3Text(LocationStr, Location))
			{
				Entry->SetStringField(TEXT("error"), TEXT("location format must be x,y,z"));
				OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
				continue;
			}
			FRotator Rotation = FRotator::ZeroRotator;
			if (!RotationStr.IsEmpty() && !NxParseRotatorText(RotationStr, Rotation))
			{
				Entry->SetStringField(TEXT("error"), TEXT("rotation format must be pitch,yaw,roll"));
				OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
				continue;
			}
			FString ClassErr;
			UClass* SpawnClass = ResolveSpawnClass(ClassName, SpawnAssetPath, ClassErr);
			if (!SpawnClass)
			{
				Entry->SetStringField(TEXT("error"), ClassErr);
				OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
				continue;
			}
			AActor* Spawned = nullptr;
			FString SpawnErr;
			if (!FNexusEditorLevelUtils::SpawnActorInLevelWorld(World, SpawnClass, Location, Rotation, Spawned, SpawnErr))
			{
				Entry->SetStringField(TEXT("error"), SpawnErr);
				OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
				continue;
			}
			Entry->SetStringField(TEXT("actorName"), Spawned->GetName());
			Entry->SetStringField(TEXT("actorClass"), Spawned->GetClass()->GetName());
			Entry->SetStringField(TEXT("note"), TEXT("persist with save_asset"));
		}
		else if (Action.Equals(TEXT("remove_actor"), ESearchCase::IgnoreCase))
		{
			FString ActorName;
			if (!OpArgs.IsValid() || !OpArgs->TryGetStringField(TEXT("actorName"), ActorName) || ActorName.IsEmpty())
			{
				Entry->SetStringField(TEXT("error"), TEXT("remove_actor requires actorName"));
				OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
				continue;
			}
			AActor* Actor = FNexusEditorLevelUtils::FindLevelActorByNameOrLabel(World, ActorName);
			if (!Actor)
			{
				Entry->SetStringField(TEXT("error"), FString::Printf(TEXT("Actor not found: %s"), *ActorName));
				OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
				continue;
			}
			FString RemoveErr;
			if (!FNexusEditorLevelUtils::RemoveLevelActor(World, Actor, RemoveErr))
			{
				Entry->SetStringField(TEXT("error"), RemoveErr);
				OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
				continue;
			}
			Entry->SetStringField(TEXT("removedActor"), ActorName);
			Entry->SetStringField(TEXT("note"), TEXT("persist with save_asset"));
		}
		else if (Action.Equals(TEXT("set_actor_property"), ESearchCase::IgnoreCase))
		{
			FString ActorName, PropPath, Value;
			if (!OpArgs.IsValid()
				|| !OpArgs->TryGetStringField(TEXT("actorName"), ActorName) || ActorName.IsEmpty()
				|| !OpArgs->TryGetStringField(TEXT("propertyPath"), PropPath) || PropPath.IsEmpty()
				|| !OpArgs->TryGetStringField(TEXT("value"), Value) || Value.IsEmpty())
			{
				Entry->SetStringField(TEXT("error"), TEXT("set_actor_property requires actorName、propertyPath、value"));
				OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
				continue;
			}
			AActor* Actor = FNexusEditorLevelUtils::FindLevelActorByNameOrLabel(World, ActorName);
			if (!Actor)
			{
				Entry->SetStringField(TEXT("error"), FString::Printf(TEXT("Actor not found: %s"), *ActorName));
				OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
				continue;
			}
			FString OldVal, ActualVal, PropErr;
			if (!FNexusPropertyUtils::WritePropertyAndEcho(Actor, { PropPath }, 0, Value, OldVal, ActualVal, PropErr))
			{
				Entry->SetStringField(TEXT("error"), PropErr);
				OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
				continue;
			}
			World->MarkPackageDirty();
			Entry->SetStringField(TEXT("actorName"), ActorName);
			Entry->SetStringField(TEXT("propertyPath"), PropPath);
			if (!OldVal.IsEmpty()) Entry->SetStringField(TEXT("oldValue"), OldVal);
			if (!ActualVal.IsEmpty()) Entry->SetStringField(TEXT("newValue"), ActualVal);
			Entry->SetStringField(TEXT("note"), TEXT("persist with save_asset"));
		}
		else if (Action.Equals(TEXT("set_property"), ESearchCase::IgnoreCase))
		{
			AWorldSettings* WorldSettings = World->GetWorldSettings();
			if (!WorldSettings)
			{
				Entry->SetStringField(TEXT("error"), TEXT("Level has no WorldSettings"));
				OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
				continue;
			}
			FString PropPath, Value;
			if (!OpArgs.IsValid()
				|| !OpArgs->TryGetStringField(TEXT("propertyPath"), PropPath) || PropPath.IsEmpty()
				|| !OpArgs->TryGetStringField(TEXT("value"), Value) || Value.IsEmpty())
			{
				Entry->SetStringField(TEXT("error"), TEXT("set_property requires propertyPath and value"));
				OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
				continue;
			}
			FString OldVal, ActualVal, PropErr;
			if (!FNexusPropertyUtils::WritePropertyAndEcho(WorldSettings, { PropPath }, 0, Value, OldVal, ActualVal, PropErr))
			{
				Entry->SetStringField(TEXT("error"), PropErr);
				OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
				continue;
			}
			World->MarkPackageDirty();
			Entry->SetStringField(TEXT("propertyPath"), PropPath);
			if (!OldVal.IsEmpty()) Entry->SetStringField(TEXT("oldValue"), OldVal);
			if (!ActualVal.IsEmpty()) Entry->SetStringField(TEXT("newValue"), ActualVal);
			Entry->SetStringField(TEXT("note"), TEXT("persist with save_asset"));
		}
		else
		{
			Entry->SetStringField(TEXT("error"), FString::Printf(TEXT("Unknown action: %s"), *Action));
		}

		OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
		}
	});
}

REGISTER_MCP_CAPABILITY(FManageAssetLevelCapability)
