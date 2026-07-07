// Copyright byteyang. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Utils/NexusVersionCompat.h"

#if NX_UE_HAS_APP_STYLE

#include "EnvironmentQuery/EnvQuery.h"
#include "UObject/UnrealType.h"

// UE5.5+ 将 UEnvQuery::Options 改为 protected，通过反射访问
FORCEINLINE TArray<UEnvQueryOption*>* GetEnvQueryOptionsPtr(UEnvQuery* EQ)
{
	FArrayProperty* Prop = FindFProperty<FArrayProperty>(UEnvQuery::StaticClass(), TEXT("Options"));
	if (!Prop) return nullptr;
	return static_cast<TArray<UEnvQueryOption*>*>(Prop->ContainerPtrToValuePtr<void>(EQ));
}

#endif // NX_UE_HAS_APP_STYLE
