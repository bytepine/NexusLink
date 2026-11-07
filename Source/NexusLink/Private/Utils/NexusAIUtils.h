// Copyright byteyang. All Rights Reserved.
#pragma once
#include "CoreMinimal.h"
#include "Dom/JsonObject.h"
class AActor;
class AAIController;
struct FNexusAIUtils final
{
	static AAIController* ResolveController(AActor* Actor);
	static void BeginMove(AAIController* Controller);
	static void AcceptMove(AAIController* Controller, int32 Result);
	static void Describe(AAIController* Controller, const TSharedPtr<FJsonObject>& Result);
	static void Shutdown();
};
