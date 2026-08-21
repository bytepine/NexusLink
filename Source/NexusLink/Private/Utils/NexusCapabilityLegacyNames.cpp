// Copyright byteyang. All Rights Reserved.



#include "Utils/NexusCapabilityLegacyNames.h"



static const TMap<FString, FString>& GetLegacyCapabilityNameMap()

{

	static const TMap<FString, FString> Map = {

#include "NexusCapabilityLegacyNames.generated.inl"

	};

	return Map;

}



FString FNexusCapabilityLegacyNames::Resolve(const FString& Name)

{

	FString Canonical;

	if (TryLookup(Name, Canonical))

	{

		return Canonical;

	}

	return Name.TrimStartAndEnd();

}



bool FNexusCapabilityLegacyNames::TryLookup(const FString& Name, FString& OutCanonical)

{

	const FString Trimmed = Name.TrimStartAndEnd();

	if (Trimmed.IsEmpty())

	{

		return false;

	}

	if (const FString* Canon = GetLegacyCapabilityNameMap().Find(Trimmed))

	{

		OutCanonical = *Canon;

		return true;

	}

	return false;

}


