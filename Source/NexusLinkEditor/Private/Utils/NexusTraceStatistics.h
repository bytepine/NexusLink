// Copyright byteyang. All Rights Reserved.
#pragma once
#include "CoreMinimal.h"
#include "Dom/JsonObject.h"

struct FNexusTraceStatistics final
{
	static bool AcceptFrame(double Start, double End, bool bHasWindow, double WindowStart, double WindowEnd)
	{
		return FMath::IsFinite(Start) && FMath::IsFinite(End) && End >= Start
			&& (!bHasWindow || (Start >= WindowStart && End <= WindowEnd));
	}

	static void Write(TArray<double> Durations, FJsonObject& Result)
	{
		if (Durations.Num() == 0) return;
		Durations.Sort();
		double Total = 0;
		for (double Duration : Durations) Total += Duration;
		const int32 Count = Durations.Num();
		Result.SetNumberField(TEXT("FrameCount"), Count);
		Result.SetNumberField(TEXT("AverageSeconds"), Total / Count);
		Result.SetNumberField(TEXT("MedianSeconds"), (Durations[(Count - 1) / 2] + Durations[Count / 2]) / 2.0);
		Result.SetNumberField(TEXT("P95Seconds"), Durations[FMath::Clamp(FMath::CeilToInt(0.95 * Count) - 1, 0, Count - 1)]);
		Result.SetNumberField(TEXT("MaxSeconds"), Durations.Last());
	}
};
