// Copyright byteyang. All Rights Reserved.
#pragma once
#include "CoreMinimal.h"
#include "Dom/JsonObject.h"
#include "NavigationSystemTypes.h"
class UWorld;
class AActor;
class UNavigationSystemV1;
class ANavigationData;

struct FNexusNavigationContext
{
	UWorld* World = nullptr;
	AActor* Actor = nullptr;
	UNavigationSystemV1* System = nullptr;
	const ANavigationData* Data = nullptr;
	FNavAgentProperties Agent;
	FSharedConstNavQueryFilter Filter;
	FString FilterClass;
	FVector Extent = FVector(100, 100, 250);
};

struct FNexusNavigationUtils final
{
	static bool ReadPoint(const TSharedPtr<FJsonObject>& Arguments, const TCHAR* Name, FVector& Point);
	static TSharedRef<FJsonObject> PointJson(const FVector& Point);
	static bool Resolve(const TSharedPtr<FJsonObject>& Arguments, const FVector& Start, FNexusNavigationContext& Context, FString& Error);
	static void Describe(const FNexusNavigationContext& Context, const TSharedPtr<FJsonObject>& Result);
};
