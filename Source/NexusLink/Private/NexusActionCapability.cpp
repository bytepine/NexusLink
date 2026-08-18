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
	const FString AssetPath = FNexusArgs(Args).Str(TEXT("assetPath"));
	Entry->SetStringField(TEXT("path"), AssetPath);
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
			else if (PrepEntry->HasField(TEXT("error")))
			{
				OutEntries.Add(MakeShared<FJsonValueObject>(PrepEntry));
			}
			return;
		}

		AfterPrepareTarget(Target, Arguments, OutTop);

		const FString AssetPath = FNexusArgs(Arguments).Str(TEXT("assetPath"));

		for (const TSharedPtr<FJsonValue>& OpVal : Ops)
		{
			const TSharedPtr<FJsonObject>* OpObjPtr = nullptr;
			if (!OpVal.IsValid() || !OpVal->TryGetObject(OpObjPtr) || !OpObjPtr)
			{
				continue;
			}
			const TSharedPtr<FJsonObject>& Op = *OpObjPtr;

			TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
			Entry->SetStringField(TEXT("path"), AssetPath);

			const FString Action = FNexusArgs(Op).Str(TEXT("action")).ToLower();
			Entry->SetStringField(TEXT("action"), Action);

			if (Action.IsEmpty())
			{
				Entry->SetStringField(TEXT("error"), TEXT("Missing action"));
				OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
				continue;
			}

			const FNexusActionHandler* Handler = Handlers.Find(Action);
			if (!Handler)
			{
				Entry->SetStringField(TEXT("error"), FString::Printf(TEXT("Unknown action: %s"), *Action));
				OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
				continue;
			}

			FNexusActionContext Ctx;
			Ctx.Args      = Arguments;
			Ctx.Entry     = Entry;
			Ctx.AssetPath = AssetPath;
			Ctx.Action    = Action;
			Ctx.Target    = Target;
			Ctx.Entries   = &OutEntries;

			(*Handler)(Op, Ctx);
			OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
		}

		FinalizeTarget(Target);
	});
}
