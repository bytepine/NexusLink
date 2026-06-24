// Copyright byteyang. All Rights Reserved.

#include "Utils/NexusCapabilityResultBuilder.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"

void FNexusCapabilityResultBuilder::AddEntryError(TArray<TSharedPtr<FJsonValue>>& OutEntries,
                                                   const FString& Msg)
{
	TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
	Entry->SetStringField(TEXT("error"), Msg);
	OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
}

void FNexusCapabilityResultBuilder::AddEntry(TArray<TSharedPtr<FJsonValue>>& OutEntries,
                                              const TSharedPtr<FJsonObject>& Entry)
{
	if (Entry.IsValid())
		OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
}
