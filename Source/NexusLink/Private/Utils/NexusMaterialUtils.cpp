// Copyright byteyang. All Rights Reserved.

#include "Utils/NexusMaterialUtils.h"

FString FNexusMaterialUtils::DomainToString(EMaterialDomain D)
{
	UEnum* E = StaticEnum<EMaterialDomain>();
	if (E)
	{
		FString N = E->GetNameStringByValue(static_cast<int64>(D));
		N.RemoveFromStart(TEXT("MD_"));
		if (!N.IsEmpty()) return N;
	}
	return TEXT("Unknown");
}

FString FNexusMaterialUtils::BlendModeToString(EBlendMode M)
{
	switch (M)
	{
	case BLEND_Opaque:      return TEXT("Opaque");
	case BLEND_Masked:      return TEXT("Masked");
	case BLEND_Translucent: return TEXT("Translucent");
	case BLEND_Additive:    return TEXT("Additive");
	case BLEND_Modulate:    return TEXT("Modulate");
	default:                return TEXT("Unknown");
	}
}

bool FNexusMaterialUtils::TryParseMaterialDomain(const FString& InStr, EMaterialDomain& OutDomain, FString& OutErr)
{
	const FString L = InStr.TrimStartAndEnd().ToLower();
	if (L.IsEmpty() || L == TEXT("surface"))
	{
		OutDomain = EMaterialDomain::MD_Surface;
		return true;
	}
	if (L == TEXT("deferreddecal") || L == TEXT("decal"))
	{
		OutDomain = EMaterialDomain::MD_DeferredDecal;
		return true;
	}
	if (L == TEXT("lightfunction"))
	{
		OutDomain = EMaterialDomain::MD_LightFunction;
		return true;
	}
	if (L == TEXT("volume"))
	{
		OutDomain = EMaterialDomain::MD_Volume;
		return true;
	}
	if (L == TEXT("postprocess"))
	{
		OutDomain = EMaterialDomain::MD_PostProcess;
		return true;
	}
	if (L == TEXT("ui"))
	{
		OutDomain = EMaterialDomain::MD_UI;
		return true;
	}
	if (L == TEXT("runtimevirtualtexture") || L == TEXT("rvt"))
	{
		OutDomain = EMaterialDomain::MD_RuntimeVirtualTexture;
		return true;
	}
	OutErr = FString::Printf(
		TEXT("Unknown materialDomain '%s' (surface|deferredDecal|lightFunction|volume|postProcess|ui|runtimeVirtualTexture)"),
		*InStr);
	return false;
}
