// Copyright byteyang. All Rights Reserved.

#include "Utils/NexusCapabilityResultBuilder.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "UObject/Object.h"

void FNexusCapabilityResultBuilder::AddEntryError(TArray<TSharedPtr<FJsonValue>>& OutEntries,
                                                   const FString& Msg)
{
	TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
	Entry->SetStringField(TEXT("error"), Msg);
	OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
}

void FNexusCapabilityResultBuilder::AddAssetNotFound(TArray<TSharedPtr<FJsonValue>>& OutEntries,
                                                      const FString& AssetPath)
{
	TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
	Entry->SetStringField(TEXT("assetPath"), AssetPath);
	Entry->SetStringField(TEXT("error"), FString::Printf(TEXT("Asset not found: %s"), *AssetPath));
	OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
}

void FNexusCapabilityResultBuilder::AddEnvUnavailable(TArray<TSharedPtr<FJsonValue>>& OutEntries,
                                                       const FString& Reason)
{
	TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
	Entry->SetStringField(TEXT("error"), FString::Printf(TEXT("Environment unavailable: %s"), *Reason));
	OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
}

void FNexusCapabilityResultBuilder::AddEntry(TArray<TSharedPtr<FJsonValue>>& OutEntries,
                                              const TSharedPtr<FJsonObject>& Entry)
{
	if (Entry.IsValid())
		OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
}

TSharedPtr<FJsonObject> FNexusCapabilityResultBuilder::MakeCreatedEntry(const UObject* Obj)
{
	TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
	if (!Obj)
	{
		Entry->SetStringField(TEXT("error"), TEXT("Create failed"));
		return Entry;
	}
	Entry->SetStringField(TEXT("name"), Obj->GetName());
	Entry->SetStringField(TEXT("path"), Obj->GetPathName());
	return Entry;
}

void FNexusCapabilityResultBuilder::AddCreatedEntry(TArray<TSharedPtr<FJsonValue>>& OutEntries, const UObject* Obj)
{
	OutEntries.Add(MakeShared<FJsonValueObject>(MakeCreatedEntry(Obj)));
}
