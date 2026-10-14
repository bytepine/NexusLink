// Copyright byteyang. All Rights Reserved.

#include "Capabilities/Runtime/Actor/NexusSpawnRuntimeActorCapability.h"
#include "Utils/NexusCapabilityResultBuilder.h"
#include "Utils/NexusArgs.h"
#include "NexusCapabilityRegistry.h"
#include "NexusMcpSchemaBuilder.h"
#include "Utils/NexusRuntimeUtils.h"
#include "Utils/NexusAssetUtils.h"
#include "EngineUtils.h"
#include "GameFramework/Actor.h"
#include "Engine/Blueprint.h"
#include "Engine/World.h"
#include "NexusMcpTool.h"

void FNexusSpawnRuntimeActorCapability::BuildDefinition(FNexusCapabilityDefinition& Out) const
{
	Out.Name = TEXT("SpawnRuntimeActor");
	Out.Description = TEXT("Spawn Actor in PIE. AssetPath or ClassName; optional location/rotation.");
	Out.InputSchema = FNexusSchema::Object()
		.Prop(TEXT("AssetPath"),     FNexusSchema::Str(TEXT("Blueprint path (or ClassName)")))
		.Prop(TEXT("ClassName"),     FNexusSchema::Str(TEXT("Native class name (or AssetPath)")))
		.Prop(TEXT("LocationX"),     FNexusSchema::Num(TEXT("Spawn X"), 0.0))
		.Prop(TEXT("LocationY"),     FNexusSchema::Num(TEXT("Spawn Y"), 0.0))
		.Prop(TEXT("LocationZ"),     FNexusSchema::Num(TEXT("Spawn Z"), 0.0))
		.Prop(TEXT("RotationPitch"), FNexusSchema::Num(TEXT("Pitch (degrees)"), 0.0))
		.Prop(TEXT("RotationYaw"),   FNexusSchema::Num(TEXT("Yaw (degrees)"),   0.0))
		.Prop(TEXT("RotationRoll"),  FNexusSchema::Num(TEXT("Roll (degrees)"),  0.0))
		.Build();
	Out.Tags = {FNexusMcpTags::Write, FNexusMcpTags::Runtime };
	Out.ExtraSearchKeywords = { TEXT("instantiate"), TEXT("place"), TEXT("create"), TEXT("Level"), TEXT("World") };
	Out.RelatedCapabilities = { TEXT("DestroyRuntimeActor"), TEXT("ListRuntimeActors") };
	Out.Prerequisites = { TEXT("PIE") };
}

FCapabilityResult FNexusSpawnRuntimeActorCapability::Execute(const TSharedPtr<FJsonObject>& Arguments) const
{

	return FNexusCapabilityResultBuilder::Build([&](auto& OutEntries, auto& OutTop, auto& OutError)
	{
		const FNexusArgs A(Arguments);


		UWorld* World = FNexusRuntimeUtils::RequirePlayWorld(OutError);
		if (!World) return;

		TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();

		FVector Location(
			A.Num(TEXT("LocationX"), 0.0),
			A.Num(TEXT("LocationY"), 0.0),
			A.Num(TEXT("LocationZ"), 0.0)
		);
		FRotator Rotation(
			A.Num(TEXT("RotationPitch"), 0.0),
			A.Num(TEXT("RotationYaw"), 0.0),
			A.Num(TEXT("RotationRoll"), 0.0)
		);

		UClass* SpawnClass = nullptr;
		if (Arguments->HasField(TEXT("AssetPath")))
		{
			const FString BpPath = A.Str(TEXT("AssetPath"));
			Entry->SetStringField(TEXT("AssetPath"), BpPath);
			UBlueprint* BP = FNexusAssetUtils::LoadAssetWithFallback<UBlueprint>(BpPath);
			if (!BP || !BP->GeneratedClass)
			{
				Entry->SetStringField(TEXT("Error"), FString::Printf(TEXT("Blueprint not found or not compiled: %s"), *BpPath));
				OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
				return;
			}
			SpawnClass = BP->GeneratedClass;
		}
		else if (Arguments->HasField(TEXT("ClassName")))
		{
			const FString ClassName = A.Str(TEXT("ClassName"));
			Entry->SetStringField(TEXT("ClassName"), ClassName);
			SpawnClass = FNexusAssetUtils::FindClassWithUPrefix(ClassName);
			if (!SpawnClass)
			{
				const FString Prefixed = TEXT("A") + ClassName;
				SpawnClass = FNexusAssetUtils::FindClassWithUPrefix(Prefixed);
			}
			if (!SpawnClass || !SpawnClass->IsChildOf(AActor::StaticClass()))
			{
				Entry->SetStringField(TEXT("Error"), FString::Printf(TEXT("Actor class not found: %s"), *ClassName));
				OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
				return;
			}
		}
		else
		{
			OutError = TEXT("AssetPath or ClassName required");
			return;
		}

		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn;
		AActor* NewActor = World->SpawnActor<AActor>(SpawnClass, Location, Rotation, Params);
		if (!NewActor)
		{
			Entry->SetStringField(TEXT("Error"), FString::Printf(TEXT("Actor spawn failed (class: %s)"), *SpawnClass->GetName()));
			OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
			return;
		}

		Entry->SetStringField(TEXT("name"),     NewActor->GetName());
		Entry->SetStringField(TEXT("Class"),    NewActor->GetClass()->GetName());
		Entry->SetStringField(TEXT("Location"), FString::Printf(TEXT("%.1f, %.1f, %.1f"), Location.X, Location.Y, Location.Z));
		OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
	
	});
}

REGISTER_MCP_CAPABILITY(FNexusSpawnRuntimeActorCapability)
