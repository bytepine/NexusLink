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

void FSpawnRuntimeActorCapability::BuildDefinition(FNexusCapabilityDefinition& Out) const
{
	Out.Name = TEXT("spawn_runtime_actor");
	Out.Description = TEXT("Spawn Actor in PIE. assetPath or className; optional location/rotation.");
	Out.InputSchema = FNexusSchema::Object()
		.Prop(TEXT("assetPath"),     FNexusSchema::Str(TEXT("Blueprint path (or className)")))
		.Prop(TEXT("className"),     FNexusSchema::Str(TEXT("Native class name (or assetPath)")))
		.Prop(TEXT("locationX"),     FNexusSchema::Num(TEXT("Spawn X"), 0.0))
		.Prop(TEXT("locationY"),     FNexusSchema::Num(TEXT("Spawn Y"), 0.0))
		.Prop(TEXT("locationZ"),     FNexusSchema::Num(TEXT("Spawn Z"), 0.0))
		.Prop(TEXT("rotationPitch"), FNexusSchema::Num(TEXT("Pitch (degrees)"), 0.0))
		.Prop(TEXT("rotationYaw"),   FNexusSchema::Num(TEXT("Yaw (degrees)"),   0.0))
		.Prop(TEXT("rotationRoll"),  FNexusSchema::Num(TEXT("Roll (degrees)"),  0.0))
		.Build();
	Out.Tags = {FNexusMcpTags::Write, FNexusMcpTags::Runtime };
	Out.ExtraSearchKeywords = { TEXT("instantiate"), TEXT("place"), TEXT("create"), TEXT("level"), TEXT("world") };
	Out.RelatedCapabilities = { TEXT("destroy_runtime_actor"), TEXT("list_runtime_actors") };
	Out.Prerequisites = { TEXT("pie") };
}

FCapabilityResult FSpawnRuntimeActorCapability::Execute(const TSharedPtr<FJsonObject>& Arguments) const
{

	return FNexusCapabilityResultBuilder::Build([&](auto& OutEntries, auto& OutTop, auto& OutError)
	{
		const FNexusArgs A(Arguments);


		UWorld* World = FNexusRuntimeUtils::RequirePlayWorld(OutError);
		if (!World) return;

		TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();

		FVector Location(
			A.Num(TEXT("locationX"), 0.0),
			A.Num(TEXT("locationY"), 0.0),
			A.Num(TEXT("locationZ"), 0.0)
		);
		FRotator Rotation(
			A.Num(TEXT("rotationPitch"), 0.0),
			A.Num(TEXT("rotationYaw"), 0.0),
			A.Num(TEXT("rotationRoll"), 0.0)
		);

		UClass* SpawnClass = nullptr;
		if (Arguments->HasField(TEXT("assetPath")))
		{
			const FString BpPath = A.Str(TEXT("assetPath"));
			Entry->SetStringField(TEXT("assetPath"), BpPath);
			UBlueprint* BP = FNexusAssetUtils::LoadAssetWithFallback<UBlueprint>(BpPath);
			if (!BP || !BP->GeneratedClass)
			{
				Entry->SetStringField(TEXT("error"), FString::Printf(TEXT("Blueprint not found or not compiled: %s"), *BpPath));
				OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
				return;
			}
			SpawnClass = BP->GeneratedClass;
		}
		else if (Arguments->HasField(TEXT("className")))
		{
			const FString ClassName = A.Str(TEXT("className"));
			Entry->SetStringField(TEXT("className"), ClassName);
			SpawnClass = FNexusAssetUtils::FindClassWithUPrefix(ClassName);
			if (!SpawnClass)
			{
				const FString Prefixed = TEXT("A") + ClassName;
				SpawnClass = FNexusAssetUtils::FindClassWithUPrefix(Prefixed);
			}
			if (!SpawnClass || !SpawnClass->IsChildOf(AActor::StaticClass()))
			{
				Entry->SetStringField(TEXT("error"), FString::Printf(TEXT("Actor class not found: %s"), *ClassName));
				OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
				return;
			}
		}
		else
		{
			OutError = TEXT("assetPath or className required");
			return;
		}

		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn;
		AActor* NewActor = World->SpawnActor<AActor>(SpawnClass, Location, Rotation, Params);
		if (!NewActor)
		{
			Entry->SetStringField(TEXT("error"), FString::Printf(TEXT("Actor spawn failed (class: %s)"), *SpawnClass->GetName()));
			OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
			return;
		}

		Entry->SetStringField(TEXT("name"),     NewActor->GetName());
		Entry->SetStringField(TEXT("class"),    NewActor->GetClass()->GetName());
		Entry->SetStringField(TEXT("location"), FString::Printf(TEXT("%.1f, %.1f, %.1f"), Location.X, Location.Y, Location.Z));
		OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
	
	});
}

REGISTER_MCP_CAPABILITY(FSpawnRuntimeActorCapability)
