// Copyright byteyang. All Rights Reserved.
#pragma once
#include "CoreMinimal.h"
#include "Dom/JsonObject.h"
class UObject;
struct FNexusAssetExportUtils final
{
	static TSharedRef<FJsonObject> Describe(UObject* Asset);
};
