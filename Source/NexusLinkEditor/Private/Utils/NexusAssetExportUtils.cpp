// Copyright byteyang. All Rights Reserved.
#include "Utils/NexusAssetExportUtils.h"
#include "Exporters/Exporter.h"
#include "UObject/UObjectIterator.h"

TSharedRef<FJsonObject> FNexusAssetExportUtils::Describe(UObject* Asset)
{
	auto Result = MakeShared<FJsonObject>();
	TArray<TSharedPtr<FJsonValue>> Formats;
	TSet<FString> Seen;
	for (TObjectIterator<UClass> Class; Class; ++Class)
	{
		if (!Asset || !Class->IsChildOf(UExporter::StaticClass()) || Class->HasAnyClassFlags(CLASS_Abstract)) continue;
		const UExporter* Exporter = Cast<UExporter>(Class->GetDefaultObject());
		if (!Exporter || !Exporter->SupportedClass || !Asset->IsA(Exporter->SupportedClass)) continue;
		for (int32 FormatIndex = 0; FormatIndex < Exporter->FormatExtension.Num() && Formats.Num() < 128; ++FormatIndex)
		{
			const FString Extension = Exporter->FormatExtension[FormatIndex].ToLower();
			const FString Key = Class->GetPathName() + TEXT(":") + Extension;
			if (Seen.Contains(Key)) continue;
			Seen.Add(Key);
			auto Format = MakeShared<FJsonObject>();
			Format->SetStringField(TEXT("Extension"), Extension);
			Format->SetStringField(TEXT("Exporter"), Class->GetPathName());
			if (Exporter->FormatDescription.IsValidIndex(FormatIndex)) Format->SetStringField(TEXT("Description"), Exporter->FormatDescription[FormatIndex]);
			Formats.Add(MakeShared<FJsonValueObject>(Format));
		}
	}
	Result->SetArrayField(TEXT("Formats"), Formats);
	Result->SetBoolField(TEXT("bExportAvailable"), Formats.Num() > 0);
	Result->SetStringField(TEXT("Note"), TEXT("Registered exporter/class compatibility only; format-specific options may constrain execution. ConflictPolicy covers the primary output path; exporters may create sidecar files"));
	return Result;
}
