// Copyright byteyang. All Rights Reserved.

#include "NexusActionCapability.h"
#include "Utils/NexusArgs.h"
#include "Utils/NexusCapabilityResultBuilder.h"
#include "Utils/NexusJsonUtils.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"

// ─────────────────────────────────────────────────────────────────────────────

bool FNexusActionCapability::PrepareTarget(
	const TSharedPtr<FJsonObject>& Args,
	TSharedPtr<FJsonObject>& Entry,
	void*& OutTarget,
	FString& OutError) const
{
	const FString AssetPath = FNexusArgs(Args).Str(TEXT("AssetPath"));
	Entry->SetStringField(TEXT("Path"), AssetPath);
	OutTarget = nullptr;
	OutError.Reset();
	return true;
}

FCapabilityResult FNexusActionCapability::Execute(const TSharedPtr<FJsonObject>& Arguments) const
{
	return FNexusCapabilityResultBuilder::Build([&](auto& OutEntries, auto& OutTop, auto& OutError)
	{
		const TArray<TSharedPtr<FJsonValue>> Ops = FNexusJsonUtils::ExtractOperations(Arguments);
		if (Ops.Num() == 0)
		{
			OutError = TEXT("Missing or empty operations");
			return;
		}

		TMap<FString, FNexusActionHandler> Handlers;
		RegisterActions(Handlers);

		TSharedPtr<FJsonObject> PrepEntry = MakeShared<FJsonObject>();
		void* Target = nullptr;
		FString PrepareError;
		if (!PrepareTarget(Arguments, PrepEntry, Target, PrepareError))
		{
			if (!PrepareError.IsEmpty())
			{
				OutError = PrepareError;
			}
			else if (PrepEntry->HasField(TEXT("Error")))
			{
				OutEntries.Add(MakeShared<FJsonValueObject>(PrepEntry));
			}
			return;
		}

		AfterPrepareTarget(Target, Arguments, OutTop);

		const FString AssetPath = FNexusArgs(Arguments).Str(TEXT("AssetPath"));

		for (const TSharedPtr<FJsonValue>& OpVal : Ops)
		{
			const TSharedPtr<FJsonObject>* OpObjPtr = nullptr;
			if (!OpVal.IsValid() || !OpVal->TryGetObject(OpObjPtr) || !OpObjPtr)
			{
				TSharedPtr<FJsonObject> BadEntry = MakeShared<FJsonObject>();
				BadEntry->SetStringField(TEXT("Path"), AssetPath);
				BadEntry->SetStringField(TEXT("Error"), TEXT("Invalid operation (expected object)"));
				OutEntries.Add(MakeShared<FJsonValueObject>(BadEntry));
				continue;
			}
			const TSharedPtr<FJsonObject>& Op = *OpObjPtr;

			TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
			Entry->SetStringField(TEXT("Path"), AssetPath);

			const FString RequestedAction = FNexusArgs(Op).Str(TEXT("Action"));
			const FNexusActionHandler* Handler = nullptr;
			FString Action;
			for (const TPair<FString, FNexusActionHandler>& Pair : Handlers)
			{
				if (Pair.Key.Equals(RequestedAction, ESearchCase::IgnoreCase))
				{
					Action = Pair.Key;
					Handler = &Pair.Value;
					break;
				}
			}
			Entry->SetStringField(TEXT("Action"), Action.IsEmpty() ? RequestedAction : Action);

			if (RequestedAction.IsEmpty())
			{
				Entry->SetStringField(TEXT("Error"), TEXT("Missing action"));
				OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
				continue;
			}

			if (!Handler)
			{
				Entry->SetStringField(TEXT("Error"), FString::Printf(TEXT("Unknown action: %s"), *RequestedAction));
				OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
				continue;
			}

			FNexusActionContext Ctx;
			Ctx.Args      = Arguments;
			Ctx.Entry     = Entry;
			Ctx.AssetPath = AssetPath;
			Ctx.Action    = Action;
			Ctx.Target    = Target;

			(*Handler)(Op, Ctx);
			OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
		}

		FinalizeTarget(Target);
	});
}
