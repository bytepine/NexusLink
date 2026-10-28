// Copyright byteyang. All Rights Reserved.
#include "Utils/NexusMcpImageUtils.h"
#include "Dom/JsonValue.h"
#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Base64.h"

int32 FNexusMcpImageUtils::AppendContent(const TArray<FString>& GeneratedFiles, const TSharedPtr<FJsonObject>& Result)
{
	if (!Result.IsValid() || GeneratedFiles.Num() == 0) return 0;
	TArray<TSharedPtr<FJsonValue>> Content;
	const TArray<TSharedPtr<FJsonValue>>* Existing = nullptr;
	if (Result->TryGetArrayField(TEXT("content"), Existing)) Content = *Existing;
	int64 Remaining = 4 * 1024 * 1024;
	int32 Count = 0, Omitted = 0, EncodedCharacters = 0;
	TSet<FString> Seen;
	for (const FString& Path : GeneratedFiles)
	{
		if (Seen.Contains(Path)) continue;
		Seen.Add(Path);
		const int64 Size = IFileManager::Get().FileSize(*Path);
		if (Count >= 4 || Size <= 0 || Size > Remaining) { ++Omitted; continue; }
		TArray<uint8> Bytes;
		if (!FFileHelper::LoadFileToArray(Bytes, *Path) || Bytes.Num() == 0 || Bytes.Num() > Remaining) { ++Omitted; continue; }
		FString Mime;
		static const uint8 Png[] = {0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a};
		if (Bytes.Num() >= 8 && FMemory::Memcmp(Bytes.GetData(), Png, 8) == 0) Mime = TEXT("image/png");
		else if (Bytes.Num() >= 4 && Bytes[0] == 0xff && Bytes[1] == 0xd8 && Bytes[2] == 0xff && Bytes[Bytes.Num() - 2] == 0xff && Bytes.Last() == 0xd9) Mime = TEXT("image/jpeg");
		if (Mime.IsEmpty()) { ++Omitted; continue; }
		auto Image = MakeShared<FJsonObject>();
		Image->SetStringField(TEXT("type"), TEXT("image")); Image->SetStringField(TEXT("mimeType"), Mime);
		const FString Data = FBase64::Encode(Bytes); Image->SetStringField(TEXT("data"), Data);
		Content.Add(MakeShared<FJsonValueObject>(Image)); EncodedCharacters += Data.Len(); Remaining -= Bytes.Num(); ++Count;
	}
	if (Omitted)
	{
		auto Note = MakeShared<FJsonObject>(); Note->SetStringField(TEXT("type"), TEXT("text"));
		Note->SetStringField(TEXT("text"), FString::Printf(TEXT("%d generated image(s) omitted: missing/unsupported file or response limit (4 images, 4 MiB total). FilePath metadata is retained."), Omitted));
		Content.Add(MakeShared<FJsonValueObject>(Note));
	}
	Result->SetArrayField(TEXT("content"), Content);
	return EncodedCharacters;
}
