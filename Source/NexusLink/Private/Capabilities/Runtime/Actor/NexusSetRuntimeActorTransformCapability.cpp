// Copyright byteyang. All Rights Reserved.

#include "Capabilities/Runtime/Actor/NexusSetRuntimeActorTransformCapability.h"
#include "Utils/NexusCapabilityResultBuilder.h"
#include "Utils/NexusArgs.h"
#include "Utils/NexusRuntimeUtils.h"
#include "NexusCapabilityRegistry.h"
#include "NexusMcpSchemaBuilder.h"
#include "NexusMcpTool.h"
#include "GameFramework/Actor.h"
#include "Engine/World.h"
#include "Misc/DefaultValueHelper.h"

static FString NxFormatVector(const FVector& Value)
{
	return FString::Printf(TEXT("%s,%s,%s"), *FString::SanitizeFloat(Value.X), *FString::SanitizeFloat(Value.Y), *FString::SanitizeFloat(Value.Z));
}

static FString NxFormatRotator(const FRotator& Value)
{
	return FString::Printf(TEXT("%s,%s,%s"), *FString::SanitizeFloat(Value.Pitch), *FString::SanitizeFloat(Value.Yaw), *FString::SanitizeFloat(Value.Roll));
}

static bool NxParseVec3(const FString& Text, FVector& OutValue)
{
	TArray<FString> Parts;
	Text.ParseIntoArray(Parts, TEXT(","), false);
	if (Parts.Num() != 3)
	{
		Text.ParseIntoArrayWS(Parts);
	}
	if (Parts.Num() != 3)
	{
		return false;
	}
	float Values[3];
	for (int32 Index = 0; Index < 3; ++Index)
	{
		if (!FDefaultValueHelper::ParseFloat(Parts[Index], Values[Index]) || !FMath::IsFinite(Values[Index]))
		{
			return false;
		}
	}
	OutValue = FVector(Values[0], Values[1], Values[2]);
	return true;
}

void FNexusSetRuntimeActorTransformCapability::BuildDefinition(FNexusCapabilityDefinition& Out) const
{
	Out.Name = TEXT("SetRuntimeActorTransform");
	Out.Description = TEXT("Move, rotate, or scale a runtime Actor with the native transform setters. Returns the previous transform.");
	Out.InputSchema = FNexusSchema::Object()
		.Required(TEXT("ActorName"), FNexusSchema::Str(TEXT("Runtime Actor name")))
		.Prop(TEXT("Location"), FNexusSchema::Str(TEXT("World location x,y,z")))
		.Prop(TEXT("Rotation"), FNexusSchema::Str(TEXT("World rotation pitch,yaw,roll")))
		.Prop(TEXT("Scale"), FNexusSchema::Str(TEXT("Actor scale x,y,z")))
		.Prop(TEXT("bSweep"), FNexusSchema::Bool(TEXT("Sweep when setting location"), true, false))
		.Prop(TEXT("bTeleport"), FNexusSchema::Bool(TEXT("Teleport physics instead of sweeping it"), true, true))
		.Build();
	Out.Tags = { FNexusMcpTags::Write, FNexusMcpTags::Runtime };
	Out.ExtraSearchKeywords = { TEXT("transform"), TEXT("move"), TEXT("teleport"), TEXT("location"), TEXT("rotation"), TEXT("scale") };
	Out.RelatedCapabilities = { TEXT("GetRuntimeActorProperty"), TEXT("SetRuntimeActorProperty") };
	Out.Prerequisites = { TEXT("PIE") };
	Out.WhenToUse = TEXT("Nudge a runtime Actor and restore it from the returned old transform");
}

FCapabilityResult FNexusSetRuntimeActorTransformCapability::Execute(const TSharedPtr<FJsonObject>& Arguments) const
{
	const FNexusArgs A(Arguments);
	const FString ActorName = A.Str(TEXT("ActorName"));
	const FString LocationText = A.Str(TEXT("Location"));
	const FString RotationText = A.Str(TEXT("Rotation"));
	const FString ScaleText = A.Str(TEXT("Scale"));
	const bool bSweep = A.Bool(TEXT("bSweep"), false);
	const bool bTeleport = A.Bool(TEXT("bTeleport"), true);
	if (LocationText.IsEmpty() && RotationText.IsEmpty() && ScaleText.IsEmpty())
	{
		return FCapabilityResult::MakeArgInvalid(TEXT("SetRuntimeActorTransform requires Location, Rotation, or Scale"));
	}

	return FNexusCapabilityResultBuilder::Build([&](auto& OutEntries, auto& OutTop, auto& OutError)
	{
		(void)OutTop;
		UWorld* World = FNexusRuntimeUtils::RequirePlayWorld(OutError);
		if (!World)
		{
			return;
		}
		AActor* Actor = FNexusRuntimeUtils::FindActorByName(World, ActorName);
		if (!Actor)
		{
			OutError = FString::Printf(TEXT("Actor not found: %s"), *ActorName);
			return;
		}

		const FVector OldLocation = Actor->GetActorLocation();
		const FRotator OldRotation = Actor->GetActorRotation();
		const FVector OldScale = Actor->GetActorScale3D();
		const ETeleportType Teleport = bTeleport ? ETeleportType::TeleportPhysics : ETeleportType::None;

		// 所有输入都通过校验后再写入，错误请求不能留下部分变换。
		FVector Location = FVector::ZeroVector;
		FVector Rotation = FVector::ZeroVector;
		FVector Scale = FVector::OneVector;
		if (!LocationText.IsEmpty() && !NxParseVec3(LocationText, Location))
		{
			OutError = TEXT("Location must contain three finite numbers: x,y,z");
			return;
		}
		if (!RotationText.IsEmpty() && !NxParseVec3(RotationText, Rotation))
		{
			OutError = TEXT("Rotation must contain three finite numbers: pitch,yaw,roll");
			return;
		}
		if (!ScaleText.IsEmpty() && !NxParseVec3(ScaleText, Scale))
		{
			OutError = TEXT("Scale must contain three finite numbers: x,y,z");
			return;
		}
		bool bLocationOk = true;
		if (!LocationText.IsEmpty())
		{
			bLocationOk = Actor->SetActorLocation(Location, bSweep, nullptr, Teleport);
		}
		if (!RotationText.IsEmpty())
		{
			Actor->SetActorRotation(FRotator(Rotation.X, Rotation.Y, Rotation.Z), Teleport);
		}
		if (!ScaleText.IsEmpty())
		{
			Actor->SetActorScale3D(Scale);
		}

		TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
		Entry->SetStringField(TEXT("ActorName"), Actor->GetName());
		Entry->SetStringField(TEXT("OldLocation"), NxFormatVector(OldLocation));
		Entry->SetStringField(TEXT("OldRotation"), NxFormatRotator(OldRotation));
		Entry->SetStringField(TEXT("OldScale"), NxFormatVector(OldScale));
		Entry->SetStringField(TEXT("NewLocation"), NxFormatVector(Actor->GetActorLocation()));
		Entry->SetStringField(TEXT("NewRotation"), NxFormatRotator(Actor->GetActorRotation()));
		Entry->SetStringField(TEXT("NewScale"), NxFormatVector(Actor->GetActorScale3D()));
		Entry->SetBoolField(TEXT("bLocationSweepHit"), !bLocationOk && bSweep);
		OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
	});
}

REGISTER_MCP_CAPABILITY(FNexusSetRuntimeActorTransformCapability)
